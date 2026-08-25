#pragma once

#if !__has_include(<ESPressio_State.hpp>)
#error "ESPNowStateTransport requires ESPressio State. Include the State working branch when using this optional adapter."
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <tuple>

#include <ESPressio_State.hpp>

#include "ESPressio_ESPNowTransport.hpp"

#ifndef ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS
#define ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS 100
#endif

namespace ESPressio {
namespace ESPNow {

template<
    typename TContract,
    std::size_t TMaximumRemoteDevices,
    std::size_t TSubscriptionCapacity,
    std::size_t TMaximumPeers
>
class ESPNowStateTransport final :
    public State::IStatePublisherObserver,
    public State::IStateSubscriptionRegistryObserver {
public:
    using Publisher = State::StatePublisher<TContract>;
    using RemoteManager = State::RemoteStateManager<TContract, TMaximumRemoteDevices>;
    using Subscriptions = State::StateSubscriptionRegistry<TSubscriptionCapacity>;
    static constexpr std::size_t MaximumStatePayloadBytes = MaximumFrameSize - 8;
    static constexpr std::size_t MaximumPending = TMaximumPeers * TContract::StateCount;

private:
    struct PeerRecord {
        bool Used = false;
        MacAddress Address{};
        State::DeviceIdentifier Device{};
    };

    struct PendingRecord {
        bool Used = false;
        State::DeviceIdentifier Destination{};
        State::StateTypeId TypeId = 0;
        State::StateEpoch Epoch = 0;
        State::StateRevision Revision = 0;
        uint64_t LastSendMilliseconds = 0;
        std::size_t Size = 0;
        std::array<uint8_t, MaximumStatePayloadBytes> Payload{};
    };

    ESPNowTransport& _transport;
    Publisher& _publisher;
    RemoteManager& _remote;
    Subscriptions& _subscriptions;
    State::StateSubscriberRegistry<TContract, TMaximumPeers> _subscribers;
    std::array<PeerRecord, TMaximumPeers> _peers{};
    std::array<PendingRecord, MaximumPending> _pending{};
    Observable::ObserverHandlePtr _publisherHandle;
    Observable::ObserverHandlePtr _subscriptionHandle;
    mutable std::recursive_mutex _mutex;
    bool _initialized = false;

    static State::DeviceIdentifier DeviceFromMac(const MacAddress& address) {
        return State::DeviceIdentifier::FromMacAddress(address.Bytes);
    }

    static bool MacFromDevice(const State::DeviceIdentifier& device, MacAddress& address) {
        uint8_t bytes[MacAddressLength] = {};
        if (!device.TryGetMacAddress(bytes)) return false;
        address = MacAddress(bytes);
        return true;
    }

    PeerRecord* FindPeerLocked(const State::DeviceIdentifier& device) {
        for (auto& peer : _peers) if (peer.Used && peer.Device == device) return &peer;
        return nullptr;
    }

    bool RememberPeer(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (auto* existing = FindPeerLocked(device)) {
            existing->Address = address;
            return true;
        }
        for (auto& peer : _peers) {
            if (!peer.Used) {
                peer.Used = true;
                peer.Address = address;
                peer.Device = device;
                return true;
            }
        }
        return false;
    }

    PendingRecord* FindPendingLocked(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId
    ) {
        for (auto& pending : _pending) {
            if (pending.Used && pending.Destination == destination && pending.TypeId == typeId) return &pending;
        }
        return nullptr;
    }

    PendingRecord* FindOrCreatePendingLocked(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId
    ) {
        if (auto* existing = FindPendingLocked(destination, typeId)) return existing;
        for (auto& pending : _pending) {
            if (!pending.Used) {
                pending.Used = true;
                pending.Destination = destination;
                pending.TypeId = typeId;
                return &pending;
            }
        }
        return nullptr;
    }

    bool SendRaw(
        const State::DeviceIdentifier& destination,
        const uint8_t* payload,
        std::size_t size
    ) {
        MacAddress peer;
        if (!MacFromDevice(destination, peer)) return false;
        return _transport.Send(
            peer,
            static_cast<uint8_t>(ESPNowProtocol::StateTransport),
            payload,
            size
        );
    }

    bool SendControl(
        const State::DeviceIdentifier& destination,
        State::StateProtocol::MessageType type,
        State::StateTypeId typeId
    ) {
        State::StateProtocol::ControlMessage control;
        control.Type = type;
        control.Device = _publisher.Origin();
        control.TypeId = typeId;
        std::array<uint8_t, State::StateProtocol::ControlSize> payload{};
        std::size_t size = 0;
        if (!State::StateProtocol::EncodeControl(control, payload.data(), payload.size(), size)) return false;
        return SendRaw(destination, payload.data(), size);
    }

    bool SendAcknowledgement(
        const State::DeviceIdentifier& destination,
        const State::StateUpdateHeader& update
    ) {
        State::StateAcknowledgement acknowledgement;
        acknowledgement.Origin = update.Origin;
        acknowledgement.TypeId = update.TypeId;
        acknowledgement.Epoch = update.Epoch;
        acknowledgement.Revision = update.Revision;
        std::array<uint8_t, State::StateProtocol::AcknowledgementSize> payload{};
        std::size_t size = 0;
        if (!State::StateProtocol::EncodeAcknowledgement(acknowledgement, payload.data(), payload.size(), size)) return false;
        return SendRaw(destination, payload.data(), size);
    }

    template<typename TDefinition>
    bool QueueLatestForSubscriber(const State::DeviceIdentifier& destination) {
        State::StateUpdate<State::StateValueType<TDefinition>> update;
        if (!_publisher.template Snapshot<TDefinition>(update)) return false;

        std::array<uint8_t, MaximumStatePayloadBytes> encoded{};
        std::size_t size = 0;
        if (!State::StateProtocol::template EncodeUpdate<TDefinition>(
                update, encoded.data(), encoded.size(), size)) return false;

        PendingRecord* pending = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            pending = FindOrCreatePendingLocked(destination, State::StateTypeIdOf<TDefinition>);
            if (pending == nullptr) return false;
            pending->Epoch = update.Header.Epoch;
            pending->Revision = update.Header.Revision;
            pending->Size = size;
            std::memcpy(pending->Payload.data(), encoded.data(), size);
            pending->LastSendMilliseconds = _transport.GetMonotonicTimestampNanoseconds() / 1000000ULL;
        }
        return SendRaw(destination, encoded.data(), size);
    }

    template<std::size_t TIndex = 0>
    bool QueueLatestByType(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<TIndex, typename TContract::Definitions>::type;
            if (typeId == State::StateTypeIdOf<Definition>) {
                return QueueLatestForSubscriber<Definition>(destination);
            }
            return QueueLatestByType<TIndex + 1>(destination, typeId);
        }
        return false;
    }

    template<std::size_t TIndex = 0>
    bool ApplyIncoming(
        const State::StateProtocol::ParsedUpdate& parsed,
        bool& exactDuplicate
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<TIndex, typename TContract::Definitions>::type;
            if (parsed.Header.TypeId == State::StateTypeIdOf<Definition>) {
                State::StateValueType<Definition> value{};
                if (!State::StateProtocol::template DecodeValue<Definition>(parsed, value)) return false;

                State::RemoteStateSnapshot<State::StateValueType<Definition>> current;
                if (_remote.template Read<Definition>(parsed.Header.Origin, current) && current.HasValue) {
                    exactDuplicate =
                        current.Epoch == parsed.Header.Epoch &&
                        current.Revision == parsed.Header.Revision;
                }
                if (exactDuplicate) return true;
                return _remote.template Apply<Definition>(
                    parsed.Header.Origin,
                    parsed.Header.Epoch,
                    parsed.Header.Revision,
                    value
                );
            }
            return ApplyIncoming<TIndex + 1>(parsed, exactDuplicate);
        }
        return false;
    }

    void HandleUpdate(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::ParsedUpdate parsed;
        if (!State::StateProtocol::DecodeUpdate(frame.Payload, frame.PayloadLength, parsed)) return;
        const auto source = DeviceFromMac(frame.Source);
        if (parsed.Header.Origin != source) return;
        RememberPeer(frame.Source);
        _remote.SetAvailability(source, State::RemoteDeviceAvailability::Connected);

        bool duplicate = false;
        const bool accepted = ApplyIncoming(parsed, duplicate);
        if (accepted || duplicate) {
            (void)SendAcknowledgement(source, parsed.Header);
        }
    }

    void HandleAcknowledgement(const ESPNowReceivedFrame& frame) {
        State::StateAcknowledgement acknowledgement;
        if (!State::StateProtocol::DecodeAcknowledgement(frame.Payload, frame.PayloadLength, acknowledgement)) return;
        const auto source = DeviceFromMac(frame.Source);
        RememberPeer(frame.Source);

        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* pending = FindPendingLocked(source, acknowledgement.TypeId);
        if (pending == nullptr) return;
        if (
            acknowledgement.Origin != _publisher.Origin() ||
            acknowledgement.Epoch != pending->Epoch ||
            acknowledgement.Revision < pending->Revision
        ) return;
        *pending = PendingRecord{};
    }

    void HandleControl(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::ControlMessage control;
        if (!State::StateProtocol::DecodeControl(frame.Payload, frame.PayloadLength, control)) return;
        const auto source = DeviceFromMac(frame.Source);
        if (control.Device != source) return;
        RememberPeer(frame.Source);

        switch (control.Type) {
            case State::StateProtocol::MessageType::Subscribe:
                if (_subscribers.Subscribe(source, control.TypeId)) {
                    (void)QueueLatestByType(source, control.TypeId);
                }
                break;
            case State::StateProtocol::MessageType::Unsubscribe:
                (void)_subscribers.Unsubscribe(source, control.TypeId);
                break;
            case State::StateProtocol::MessageType::Resynchronize:
                if (control.TypeId != 0) {
                    if (_subscribers.IsSubscribed(source, control.TypeId)) {
                        (void)QueueLatestByType(source, control.TypeId);
                    }
                } else {
                    _subscribers.ForEachSubscribedType(source, [&](State::StateTypeId typeId) {
                        (void)QueueLatestByType(source, typeId);
                    });
                }
                break;
            case State::StateProtocol::MessageType::Disconnect:
                _subscribers.Remove(source);
                _remote.SetAvailability(source, State::RemoteDeviceAvailability::Disconnected);
                break;
            default:
                break;
        }
    }

    void HandleFrame(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::MessageType type;
        if (!State::StateProtocol::GetMessageType(frame.Payload, frame.PayloadLength, type)) return;
        switch (type) {
            case State::StateProtocol::MessageType::Update:
                HandleUpdate(frame);
                break;
            case State::StateProtocol::MessageType::Acknowledgement:
                HandleAcknowledgement(frame);
                break;
            case State::StateProtocol::MessageType::Subscribe:
            case State::StateProtocol::MessageType::Unsubscribe:
            case State::StateProtocol::MessageType::Resynchronize:
            case State::StateProtocol::MessageType::Disconnect:
                HandleControl(frame);
                break;
        }
    }

    void Maintain(uint64_t nowMilliseconds) {
        std::array<PendingRecord, MaximumPending> retries{};
        std::size_t count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (auto& pending : _pending) {
                if (!pending.Used || pending.Size == 0) continue;
                if (nowMilliseconds - pending.LastSendMilliseconds < ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS) continue;
                pending.LastSendMilliseconds = nowMilliseconds;
                retries[count++] = pending;
            }
        }
        for (std::size_t index = 0; index < count; ++index) {
            (void)SendRaw(retries[index].Destination, retries[index].Payload.data(), retries[index].Size);
        }
    }

    void SendSubscriptionToPeer(
        const State::DeviceIdentifier& peer,
        State::StateTypeId typeId,
        bool subscribe
    ) {
        (void)SendControl(
            peer,
            subscribe
                ? State::StateProtocol::MessageType::Subscribe
                : State::StateProtocol::MessageType::Unsubscribe,
            typeId
        );
    }

public:
    ESPNowStateTransport(
        Publisher& publisher,
        RemoteManager& remote,
        Subscriptions& subscriptions,
        ESPNowTransport& transport = ESPNowTransport::GetInstance()
    ) : _transport(transport),
        _publisher(publisher),
        _remote(remote),
        _subscriptions(subscriptions) {}

    bool Initialize() {
        if (_initialized) return true;
        if (!_transport.GetIsInitialized()) return false;
        if (!_transport.RegisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::StateTransport),
                [this](const ESPNowReceivedFrame& frame) { HandleFrame(frame); })) return false;
        if (!_transport.RegisterMaintenanceHandler(
                this,
                [this](uint64_t nowMilliseconds) { Maintain(nowMilliseconds); })) {
            _transport.UnregisterProtocolHandler(static_cast<uint8_t>(ESPNowProtocol::StateTransport));
            return false;
        }
        _publisherHandle = _publisher.RegisterObserver(this);
        _subscriptionHandle = _subscriptions.RegisterObserver(this);
        _initialized = static_cast<bool>(_publisherHandle) && static_cast<bool>(_subscriptionHandle);
        if (!_initialized) Shutdown();
        return _initialized;
    }

    void Shutdown() {
        _subscriptionHandle.reset();
        _publisherHandle.reset();
        _transport.UnregisterMaintenanceHandler(this);
        _transport.UnregisterProtocolHandler(static_cast<uint8_t>(ESPNowProtocol::StateTransport));
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _peers = {};
        _pending = {};
        _initialized = false;
    }

    bool IsInitialized() const noexcept { return _initialized; }

    bool PeerAvailable(const MacAddress& address) {
        if (!_initialized || address.IsZero() || !RememberPeer(address)) return false;
        const auto device = DeviceFromMac(address);
        _remote.SetAvailability(device, State::RemoteDeviceAvailability::Connected);
        _subscriptions.ForEach([&](const typename Subscriptions::Descriptor& subscription) {
            if (
                subscription.Scope == State::StateSubscriptionScope::AnyDevice ||
                subscription.Device == device
            ) {
                SendSubscriptionToPeer(device, subscription.TypeId, true);
            }
        });
        return true;
    }

    bool PeerStale(const MacAddress& address) {
        return _remote.SetAvailability(DeviceFromMac(address), State::RemoteDeviceAvailability::Stale);
    }

    bool PeerLost(const MacAddress& address) {
        return _remote.SetAvailability(DeviceFromMac(address), State::RemoteDeviceAvailability::ConnectionLost);
    }

    bool RequestResynchronization(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        return SendControl(device, State::StateProtocol::MessageType::Resynchronize, 0);
    }

    bool DisconnectPeer(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        const bool sent = SendControl(device, State::StateProtocol::MessageType::Disconnect, 0);
        _remote.SetAvailability(device, State::RemoteDeviceAvailability::Disconnected);
        return sent;
    }

    void OnStatePublished(
        State::StateTypeId typeId,
        State::StateEpoch,
        State::StateRevision
    ) override {
        _subscribers.ForEachSubscriber(typeId, [&](const State::DeviceIdentifier& subscriber) {
            (void)QueueLatestByType(subscriber, typeId);
        });
    }

    void OnStateSubscribed(
        State::StateTypeId typeId,
        State::StateSubscriptionScope scope,
        const State::DeviceIdentifier& device
    ) override {
        if (scope == State::StateSubscriptionScope::SpecificDevice) {
            SendSubscriptionToPeer(device, typeId, true);
            return;
        }
        std::array<State::DeviceIdentifier, TMaximumPeers> peers{};
        std::size_t count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (const auto& peer : _peers) if (peer.Used) peers[count++] = peer.Device;
        }
        for (std::size_t index = 0; index < count; ++index) {
            SendSubscriptionToPeer(peers[index], typeId, true);
        }
    }

    void OnStateUnsubscribed(
        State::StateTypeId typeId,
        State::StateSubscriptionScope scope,
        const State::DeviceIdentifier& device
    ) override {
        if (scope == State::StateSubscriptionScope::SpecificDevice) {
            SendSubscriptionToPeer(device, typeId, false);
            return;
        }
        std::array<State::DeviceIdentifier, TMaximumPeers> peers{};
        std::size_t count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (const auto& peer : _peers) if (peer.Used) peers[count++] = peer.Device;
        }
        for (std::size_t index = 0; index < count; ++index) {
            SendSubscriptionToPeer(peers[index], typeId, false);
        }
    }
};

} // namespace ESPNow
} // namespace ESPressio
