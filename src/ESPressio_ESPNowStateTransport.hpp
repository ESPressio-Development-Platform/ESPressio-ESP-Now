#pragma once

#if !__has_include(<ESPressio_State.hpp>)
#error "ESPNowStateTransport requires ESPressio State. Include the State working branch when using this optional adapter."
#endif

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <tuple>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_State.hpp>

#include "ESPressio_ESPNowTransport.hpp"

#ifndef ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS
#define ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS 100
#endif

namespace ESPressio {
namespace ESPNow {

template<typename TContract, std::size_t TMaximumRemoteDevices, std::size_t TSubscriptionCapacity, std::size_t TMaximumPeers>
class ESPNowStateTransport final :
    public State::IStatePublisherObserver,
    public State::StatePublishedObserverPack<
        ESPNowStateTransport<TContract, TMaximumRemoteDevices, TSubscriptionCapacity, TMaximumPeers>,
        TContract
    >,
    public State::IStateSubscriptionRegistryObserver {
public:
    using Publisher = State::StatePublisher<TContract>;
    using RemoteManager = State::RemoteStateManager<TContract, TMaximumRemoteDevices>;
    using Subscriptions = State::StateSubscriptionRegistry<TSubscriptionCapacity>;
    static constexpr std::size_t MaximumPayloadBytes = MaximumFrameSize - 8;
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
    };

    struct PendingSubscriptionRecord {
        bool Used = false;
        State::DeviceIdentifier Destination{};
        State::StateTypeId TypeId = 0;
        bool DesiredSubscribed = false;
        uint64_t LastSendMilliseconds = 0;
    };

    using PeerStorage = System::Memory::Vector<
        PeerRecord,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;
    using PendingStorage = System::Memory::Vector<
        PendingRecord,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;
    using PendingSubscriptionStorage = System::Memory::Vector<
        PendingSubscriptionRecord,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;
    using DeviceSnapshotStorage = System::Memory::Vector<
        State::DeviceIdentifier,
        System::Memory::MemoryPolicy::ExternalPreferred
    >;

    ESPNowTransport& _transport;
    Publisher& _publisher;
    RemoteManager& _remote;
    Subscriptions& _subscriptions;
    State::StateSubscriberRegistry<TContract, TMaximumPeers> _subscribers;
    PeerStorage _peers;
    PendingStorage _pending;
    PendingSubscriptionStorage _pendingSubscriptions;
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

    bool RememberPeer(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& peer : _peers) {
            if (peer.Used && peer.Device == device) {
                peer.Address = address;
                return true;
            }
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

    PendingRecord* PendingLocked(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId,
        bool create
    ) {
        for (auto& record : _pending) {
            if (record.Used && record.Destination == destination && record.TypeId == typeId) return &record;
        }
        if (!create) return nullptr;
        for (auto& record : _pending) {
            if (!record.Used) {
                record.Used = true;
                record.Destination = destination;
                record.TypeId = typeId;
                return &record;
            }
        }
        return nullptr;
    }

    PendingSubscriptionRecord* PendingSubscriptionLocked(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId,
        bool create
    ) {
        for (auto& record : _pendingSubscriptions) {
            if (record.Used && record.Destination == destination && record.TypeId == typeId) return &record;
        }
        if (!create) return nullptr;
        for (auto& record : _pendingSubscriptions) {
            if (!record.Used) {
                record.Used = true;
                record.Destination = destination;
                record.TypeId = typeId;
                return &record;
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
        State::StateProtocol::ControlMessage control{type, _publisher.Origin(), typeId};
        std::array<uint8_t, State::StateProtocol::ControlSize> payload{};
        std::size_t size = 0;
        return State::StateProtocol::EncodeControl(
                   control, payload.data(), payload.size(), size
               ) && SendRaw(destination, payload.data(), size);
    }

    bool SendAck(
        const State::DeviceIdentifier& destination,
        const State::StateUpdateHeader& header
    ) {
        State::StateAcknowledgement ack{
            header.Origin, header.TypeId, header.Epoch, header.Revision
        };
        std::array<uint8_t, State::StateProtocol::AcknowledgementSize> payload{};
        std::size_t size = 0;
        return State::StateProtocol::EncodeAcknowledgement(
                   ack, payload.data(), payload.size(), size
               ) && SendRaw(destination, payload.data(), size);
    }

    bool SetSubscriptionIntent(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId,
        bool subscribe
    ) {
        if (destination.IsZero() || typeId == 0) return false;
        const uint64_t nowMilliseconds =
            _transport.GetMonotonicTimestampNanoseconds() / 1000000ULL;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto* pending = PendingSubscriptionLocked(destination, typeId, true);
            if (pending == nullptr) return false;
            pending->DesiredSubscribed = subscribe;
            pending->LastSendMilliseconds = nowMilliseconds;
        }
        return SendControl(
            destination,
            subscribe
                ? State::StateProtocol::MessageType::Subscribe
                : State::StateProtocol::MessageType::Unsubscribe,
            typeId
        );
    }

    template<typename TDefinition>
    bool QueueUpdate(
        const State::DeviceIdentifier& destination,
        const State::StateUpdate<State::StateValueType<TDefinition>>& update
    ) {
        std::array<uint8_t, MaximumPayloadBytes> encoded{};
        std::size_t size = 0;
        if (!State::StateProtocol::template EncodeUpdate<TDefinition>(
                update, encoded.data(), encoded.size(), size)) return false;

        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto* pending = PendingLocked(
                destination,
                State::StateTypeIdOf<TDefinition>,
                true
            );
            if (pending == nullptr) return false;
            pending->Epoch = update.Header.Epoch;
            pending->Revision = update.Header.Revision;
            pending->LastSendMilliseconds =
                _transport.GetMonotonicTimestampNanoseconds() / 1000000ULL;
        }
        return SendRaw(destination, encoded.data(), size);
    }

    template<typename TDefinition>
    bool QueueSnapshot(const State::DeviceIdentifier& destination) {
        State::StateUpdate<State::StateValueType<TDefinition>> update;
        return _publisher.template Snapshot<TDefinition>(update) &&
            QueueUpdate<TDefinition>(destination, update);
    }

    template<std::size_t TIndex = 0>
    bool QueueSnapshotByType(
        const State::DeviceIdentifier& destination,
        State::StateTypeId typeId
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex, typename TContract::Definitions
            >::type;
            if (typeId == State::StateTypeIdOf<Definition>) {
                return QueueSnapshot<Definition>(destination);
            }
            return QueueSnapshotByType<TIndex + 1>(destination, typeId);
        }
        return false;
    }

    template<std::size_t TIndex = 0>
    bool ApplyIncoming(
        const State::StateProtocol::ParsedUpdate& parsed,
        bool& exactDuplicate
    ) {
        if constexpr (TIndex < TContract::StateCount) {
            using Definition = typename std::tuple_element<
                TIndex, typename TContract::Definitions
            >::type;
            if (parsed.Header.TypeId == State::StateTypeIdOf<Definition>) {
                State::StateValueType<Definition> value{};
                if (!State::StateProtocol::template DecodeValue<Definition>(parsed, value)) return false;
                State::RemoteStateSnapshot<State::StateValueType<Definition>> current;
                if (_remote.template Read<Definition>(parsed.Header.Origin, current) && current.HasValue) {
                    exactDuplicate = current.Epoch == parsed.Header.Epoch &&
                        current.Revision == parsed.Header.Revision;
                }
                return exactDuplicate || _remote.template Apply<Definition>(
                    parsed.Header.Origin,
                    parsed.Header.Epoch,
                    parsed.Header.Revision,
                    std::move(value)
                );
            }
            return ApplyIncoming<TIndex + 1>(parsed, exactDuplicate);
        }
        return false;
    }

    void HandleUpdate(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::ParsedUpdate parsed;
        if (!State::StateProtocol::DecodeUpdate(
                frame.Payload, frame.PayloadLength, parsed)) return;
        const auto source = DeviceFromMac(frame.Source);
        if (parsed.Header.Origin != source) return;
        if (!_subscriptions.IsSubscribed(source, parsed.Header.TypeId)) return;
        (void)RememberPeer(frame.Source);
        (void)_remote.SetAvailability(
            source, State::RemoteDeviceAvailability::Connected
        );
        bool duplicate = false;
        if (ApplyIncoming(parsed, duplicate)) (void)SendAck(source, parsed.Header);
    }

    void HandleAcknowledgement(const ESPNowReceivedFrame& frame) {
        State::StateAcknowledgement ack;
        if (!State::StateProtocol::DecodeAcknowledgement(
                frame.Payload, frame.PayloadLength, ack)) return;
        const auto source = DeviceFromMac(frame.Source);
        (void)RememberPeer(frame.Source);
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* pending = PendingLocked(source, ack.TypeId, false);
        if (pending == nullptr) return;
        if (
            ack.Origin != _publisher.Origin() ||
            ack.Epoch != pending->Epoch ||
            ack.Revision < pending->Revision
        ) return;
        *pending = PendingRecord{};
    }

    void HandleSubscriptionAcknowledgement(
        const ESPNowReceivedFrame& frame,
        const State::StateProtocol::ControlMessage& control
    ) {
        const auto source = DeviceFromMac(frame.Source);
        if (control.Device != source) return;
        const bool acknowledgedSubscribed =
            control.Type == State::StateProtocol::MessageType::SubscribeAcknowledgement;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* pending = PendingSubscriptionLocked(source, control.TypeId, false);
        if (pending == nullptr || pending->DesiredSubscribed != acknowledgedSubscribed) return;
        *pending = PendingSubscriptionRecord{};
    }

    void ClearPendingState(
        const State::DeviceIdentifier& device,
        State::StateTypeId typeId
    ) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* pending = PendingLocked(device, typeId, false);
        if (pending != nullptr) *pending = PendingRecord{};
    }

    void ClearPeerPending(const State::DeviceIdentifier& device) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& pending : _pending) {
            if (pending.Used && pending.Destination == device) pending = PendingRecord{};
        }
        for (auto& pending : _pendingSubscriptions) {
            if (pending.Used && pending.Destination == device) pending = PendingSubscriptionRecord{};
        }
    }

    void HandleControl(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::ControlMessage control;
        if (!State::StateProtocol::DecodeControl(frame.Payload, frame.PayloadLength, control)) return;
        const auto source = DeviceFromMac(frame.Source);
        if (control.Device != source) return;
        (void)RememberPeer(frame.Source);

        if (control.Type == State::StateProtocol::MessageType::SubscribeAcknowledgement ||
            control.Type == State::StateProtocol::MessageType::UnsubscribeAcknowledgement) {
            HandleSubscriptionAcknowledgement(frame, control);
            return;
        }

        switch (control.Type) {
            case State::StateProtocol::MessageType::Subscribe:
                (void)_subscribers.Subscribe(source, control.TypeId);
                (void)QueueSnapshotByType(source, control.TypeId);
                (void)SendControl(source, State::StateProtocol::MessageType::SubscribeAcknowledgement, control.TypeId);
                break;
            case State::StateProtocol::MessageType::Unsubscribe:
                (void)_subscribers.Unsubscribe(source, control.TypeId);
                ClearPendingState(source, control.TypeId);
                (void)SendControl(source, State::StateProtocol::MessageType::UnsubscribeAcknowledgement, control.TypeId);
                break;
            case State::StateProtocol::MessageType::Resynchronize:
                if (control.TypeId != 0) {
                    if (_subscribers.IsSubscribed(source, control.TypeId)) {
                        (void)QueueSnapshotByType(source, control.TypeId);
                    }
                } else {
                    _subscribers.ForEachSubscribedType(source, [&](State::StateTypeId typeId) {
                        (void)QueueSnapshotByType(source, typeId);
                    });
                }
                break;
            case State::StateProtocol::MessageType::Disconnect:
                (void)_subscribers.Remove(source);
                ClearPeerPending(source);
                (void)_remote.SetAvailability(source, State::RemoteDeviceAvailability::Disconnected);
                break;
            default:
                break;
        }
    }

    void HandleFrame(const ESPNowReceivedFrame& frame) {
        State::StateProtocol::MessageType type;
        if (!State::StateProtocol::GetMessageType(frame.Payload, frame.PayloadLength, type)) return;
        if (type == State::StateProtocol::MessageType::Update) HandleUpdate(frame);
        else if (type == State::StateProtocol::MessageType::Acknowledgement) HandleAcknowledgement(frame);
        else HandleControl(frame);
    }

    void Maintain(uint64_t nowMilliseconds) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& pending : _pending) {
            if (!pending.Used) continue;
            if (!_subscribers.IsSubscribed(pending.Destination, pending.TypeId)) {
                pending = PendingRecord{};
                continue;
            }
            if (nowMilliseconds - pending.LastSendMilliseconds < ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS) continue;
            const auto destination = pending.Destination;
            const auto typeId = pending.TypeId;
            if (!QueueSnapshotByType(destination, typeId)) pending = PendingRecord{};
        }
        for (auto& pending : _pendingSubscriptions) {
            if (!pending.Used) continue;
            if (nowMilliseconds - pending.LastSendMilliseconds < ESPRESSIO_ESPNOW_STATE_RETRY_INTERVAL_MS) continue;
            pending.LastSendMilliseconds = nowMilliseconds;
            (void)SendControl(
                pending.Destination,
                pending.DesiredSubscribed
                    ? State::StateProtocol::MessageType::Subscribe
                    : State::StateProtocol::MessageType::Unsubscribe,
                pending.TypeId
            );
        }
    }

    void SendSubscriptionToPeer(
        const State::DeviceIdentifier& peer,
        State::StateTypeId typeId,
        bool subscribe
    ) {
        (void)SetSubscriptionIntent(peer, typeId, subscribe);
    }

    DeviceSnapshotStorage PeerDeviceSnapshot() const {
        DeviceSnapshotStorage peers;
        peers.reserve(TMaximumPeers);
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& peer : _peers) {
            if (peer.Used) peers.push_back(peer.Device);
        }
        return peers;
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
        _subscriptions(subscriptions),
        _peers(TMaximumPeers),
        _pending(MaximumPending),
        _pendingSubscriptions(MaximumPending) {}

    bool Initialize() {
        if (_initialized) return true;
        if (!_transport.GetIsInitialized()) return false;
        if (!_transport.RegisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::StateTransport),
                [this](const ESPNowReceivedFrame& frame) { HandleFrame(frame); })) {
            return false;
        }
        if (!_transport.RegisterMaintenanceHandler(
                this, [this](uint64_t now) { Maintain(now); })) {
            _transport.UnregisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::StateTransport)
            );
            return false;
        }
        _publisherHandle = _publisher.RegisterContractObserver(this);
        _subscriptionHandle = _subscriptions.RegisterObserver(
            static_cast<State::IStateSubscriptionRegistryObserver*>(this)
        );
        _initialized = static_cast<bool>(_publisherHandle) && static_cast<bool>(_subscriptionHandle);
        if (!_initialized) Shutdown();
        return _initialized;
    }

    void Shutdown() {
        _subscriptionHandle.reset();
        _publisherHandle.reset();
        _transport.UnregisterMaintenanceHandler(this);
        _transport.UnregisterProtocolHandler(
            static_cast<uint8_t>(ESPNowProtocol::StateTransport)
        );
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::fill(_peers.begin(), _peers.end(), PeerRecord{});
        std::fill(_pending.begin(), _pending.end(), PendingRecord{});
        std::fill(
            _pendingSubscriptions.begin(),
            _pendingSubscriptions.end(),
            PendingSubscriptionRecord{}
        );
        _initialized = false;
    }

    bool IsInitialized() const noexcept { return _initialized; }

    bool PeerAvailable(const MacAddress& address) {
        if (!_initialized || address.IsZero() || !RememberPeer(address)) return false;
        const auto device = DeviceFromMac(address);
        (void)_remote.SetAvailability(device, State::RemoteDeviceAvailability::Connected);
        _subscriptions.ForEach([&](const typename Subscriptions::Descriptor& subscription) {
            if (subscription.Scope == State::StateSubscriptionScope::AnyDevice ||
                subscription.Device == device) {
                SendSubscriptionToPeer(device, subscription.TypeId, true);
            }
        });
        return true;
    }

    bool PeerStale(const MacAddress& address) {
        return _remote.SetAvailability(
            DeviceFromMac(address), State::RemoteDeviceAvailability::Stale
        );
    }

    bool PeerLost(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        ClearPeerPending(device);
        (void)_subscribers.Remove(device);
        return _remote.SetAvailability(device, State::RemoteDeviceAvailability::ConnectionLost);
    }

    bool RequestResynchronization(const MacAddress& address) {
        return SendControl(DeviceFromMac(address), State::StateProtocol::MessageType::Resynchronize, 0);
    }

    bool DisconnectPeer(const MacAddress& address) {
        const auto device = DeviceFromMac(address);
        const bool sent = SendControl(device, State::StateProtocol::MessageType::Disconnect, 0);
        ClearPeerPending(device);
        (void)_subscribers.Remove(device);
        (void)_remote.SetAvailability(device, State::RemoteDeviceAvailability::Disconnected);
        return sent;
    }

    template<typename TDefinition>
    void OnTypedStatePublished(
        const State::StateUpdate<State::StateValueType<TDefinition>>& update
    ) {
        if (!_subscribers.template HasSubscribers<TDefinition>()) return;
        _subscribers.ForEachSubscriber(
            State::StateTypeIdOf<TDefinition>,
            [&](const State::DeviceIdentifier& subscriber) {
                (void)QueueUpdate<TDefinition>(subscriber, update);
            }
        );
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
        auto peers = PeerDeviceSnapshot();
        for (const auto& peer : peers) SendSubscriptionToPeer(peer, typeId, true);
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
        auto peers = PeerDeviceSnapshot();
        for (const auto& peer : peers) SendSubscriptionToPeer(peer, typeId, false);
    }
};

} // namespace ESPNow
} // namespace ESPressio
