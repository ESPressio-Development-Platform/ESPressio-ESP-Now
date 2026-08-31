#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_IRadio.hpp>

#include "ESPressio_ESPNowTransport.hpp"

#ifndef ESPRESSIO_ESPNOW_RADIO_MAX_AUTO_PEERS
#define ESPRESSIO_ESPNOW_RADIO_MAX_AUTO_PEERS 20
#endif

#ifndef ESPRESSIO_ESPNOW_RADIO_RX_QUEUE_DEPTH
#define ESPRESSIO_ESPNOW_RADIO_RX_QUEUE_DEPTH 4
#endif

namespace ESPressio::ESPNow {

/// <summary>Configuration for exposing the existing ESP-NOW transport as an ESPressio packet radio.</summary>
struct ESPNowRadioConfiguration {
    ESPNowTransportConfig Transport{};
    ESPNowWiFiInterface LocalInterface = ESPNowWiFiInterface::Station;
    uint8_t Protocol = 0xF0u;
    bool AutomaticallyAddPeers = true;
    bool ShutdownOwnedTransportOnStop = true;
};

/// <summary>
/// ESP-NOW concrete for ESPressio-Radio. It transports opaque radio packets through one reserved ESP-NOW protocol ID;
/// it does not inspect ESPressio primitives or establish application-message authenticity. ESP-NOW callbacks copy into
/// a bounded queue and wake RadioWorker so onward transport never executes in the ESP-NOW callback context.
/// </summary>
class ESPNowRadio final : public Radio::IRadio {
private:
    static constexpr std::size_t ESPNowTransportWireHeaderBytes = 8;
    static constexpr std::size_t MaximumPayloadBytes = MaximumFrameSize - ESPNowTransportWireHeaderBytes;

    struct ReceivedPacket {
        Radio::RadioAddress Source{};
        uint16_t Length = 0;
        uint64_t TimestampNanoseconds = 0;
        std::array<uint8_t, MaximumPayloadBytes> Payload{};
    };

    ESPNowRadioConfiguration _configuration{};
    ESPNowTransport& _transport;
    Radio::IRadioReceiver* _receiver = nullptr;
    std::atomic<Radio::IRadioWorkSignal*> _workSignal{nullptr};
    Radio::RadioObserverSubscriptions _observers{};
    Radio::RadioAddress _localAddress{};
    std::atomic<bool> _started{false};
    bool _ownsTransport = false;
    std::array<MacAddress, ESPRESSIO_ESPNOW_RADIO_MAX_AUTO_PEERS> _knownPeers{};
    std::array<bool, ESPRESSIO_ESPNOW_RADIO_MAX_AUTO_PEERS> _knownPeerUsed{};
    std::array<ReceivedPacket, ESPRESSIO_ESPNOW_RADIO_RX_QUEUE_DEPTH> _receiveQueue{};
    std::atomic<uint8_t> _writeIndex{0};
    std::atomic<uint8_t> _readIndex{0};

    static Radio::RadioAddress ToRadioAddress(const MacAddress& address) noexcept {
        return Radio::RadioAddress::FromBytes(address.Bytes, static_cast<uint8_t>(MacAddressLength));
    }

    static MacAddress ToMacAddress(const Radio::RadioAddress& address) noexcept {
        return MacAddress(address.Bytes.data());
    }

    bool IsKnownPeer(const MacAddress& address) const noexcept {
        for (std::size_t i = 0; i < _knownPeers.size(); ++i) {
            if (_knownPeerUsed[i] && _knownPeers[i] == address) return true;
        }
        return false;
    }

    bool RememberPeer(const MacAddress& address) noexcept {
        if (IsKnownPeer(address)) return true;
        for (std::size_t i = 0; i < _knownPeers.size(); ++i) {
            if (!_knownPeerUsed[i]) {
                _knownPeers[i] = address;
                _knownPeerUsed[i] = true;
                return true;
            }
        }
        return false;
    }

    bool EnsurePeer(const MacAddress& address) {
        if (IsKnownPeer(address)) return true;
        if (!_configuration.AutomaticallyAddPeers) return false;
        ESPNowPeerConfig peer;
        peer.Address = address;
        peer.Channel = _configuration.Transport.Channel;
        peer.Interface = _configuration.LocalInterface;
        peer.Encrypt = false;
        if (!_transport.AddPeer(peer)) return false;
        return RememberPeer(address);
    }

    void Receive(const ESPNowReceivedFrame& frame) noexcept {
        if (!_started.load(std::memory_order_acquire) || frame.PayloadLength > MaximumPayloadBytes) return;

        const uint8_t write = _writeIndex.load(std::memory_order_relaxed);
        const uint8_t next = static_cast<uint8_t>((write + 1u) % _receiveQueue.size());
        if (next == _readIndex.load(std::memory_order_acquire)) return;

        auto& queued = _receiveQueue[write];
        queued.Source = ToRadioAddress(frame.Source);
        queued.Length = static_cast<uint16_t>(frame.PayloadLength);
        queued.TimestampNanoseconds = frame.ReceiveMonotonicNanoseconds;
        if (frame.PayloadLength != 0 && frame.Payload != nullptr) {
            std::memcpy(queued.Payload.data(), frame.Payload, frame.PayloadLength);
        }
        _writeIndex.store(next, std::memory_order_release);

        Radio::IRadioWorkSignal* signal = _workSignal.load(std::memory_order_acquire);
        if (signal != nullptr) signal->OnRadioWorkAvailable(*this);
    }

public:
    explicit ESPNowRadio(
        ESPNowRadioConfiguration configuration = {},
        ESPNowTransport& transport = ESPNowTransport::GetInstance()
    ) : _configuration(configuration), _transport(transport) {}

    bool Start() override {
        if (_started.load(std::memory_order_acquire)) return true;
        if (_configuration.Protocol < static_cast<uint8_t>(ESPNowProtocol::UserBase)) return false;

        _ownsTransport = !_transport.GetIsInitialized();
        if (_ownsTransport && !_transport.Initialize(_configuration.Transport)) {
            _ownsTransport = false;
            return false;
        }
        if (!_transport.GetIsInitialized()) return false;

        const MacAddress local = _transport.GetLocalEndpointAddress(_configuration.LocalInterface);
        if (local.IsZero()) {
            if (_ownsTransport && _configuration.ShutdownOwnedTransportOnStop) _transport.Shutdown();
            _ownsTransport = false;
            return false;
        }
        _localAddress = ToRadioAddress(local);
        _readIndex.store(0, std::memory_order_relaxed);
        _writeIndex.store(0, std::memory_order_relaxed);

        if (!_transport.RegisterProtocolHandler(_configuration.Protocol, [this](const ESPNowReceivedFrame& frame) {
            Receive(frame);
        })) {
            if (_ownsTransport && _configuration.ShutdownOwnedTransportOnStop) _transport.Shutdown();
            _ownsTransport = false;
            return false;
        }
        _started.store(true, std::memory_order_release);
        _observers.NotifyStarted(*this);
        return true;
    }

    void Stop() noexcept override {
        if (!_started.exchange(false, std::memory_order_acq_rel)) return;
        _transport.UnregisterProtocolHandler(_configuration.Protocol);
        if (_ownsTransport && _configuration.ShutdownOwnedTransportOnStop) _transport.Shutdown();
        _ownsTransport = false;
        _knownPeerUsed.fill(false);
        _readIndex.store(0, std::memory_order_relaxed);
        _writeIndex.store(0, std::memory_order_relaxed);
        _observers.NotifyStopped(*this);
    }

    bool IsStarted() const noexcept override { return _started.load(std::memory_order_acquire); }

    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {
            Radio::RadioCapability::Broadcast |
            Radio::RadioCapability::ChannelSelection |
            Radio::RadioCapability::HardwareAddressing |
            Radio::RadioCapability::ReceiveTimestamp,
            static_cast<uint16_t>(MaximumPayloadBytes),
            static_cast<uint8_t>(MacAddressLength)
        };
    }

    Radio::RadioAddress LocalAddress() const noexcept override { return _localAddress; }

    Radio::RadioSendResult Send(
        const Radio::RadioAddress& destination,
        const uint8_t* payload,
        std::size_t payloadSize
    ) override {
        const auto complete = [&](Radio::RadioSendResult result) {
            _observers.NotifySendCompleted(*this, destination, payloadSize, result);
            return result;
        };
        if (!IsStarted()) return complete({Radio::RadioSendStatus::NotStarted, 0});
        if (!destination.IsValid() || destination.Length != MacAddressLength)
            return complete({Radio::RadioSendStatus::InvalidAddress, 0});
        if ((payload == nullptr && payloadSize != 0) || payloadSize > MaximumPayloadBytes)
            return complete({Radio::RadioSendStatus::PayloadTooLarge, 0});

        const MacAddress peer = ToMacAddress(destination);
        if (!EnsurePeer(peer)) return complete({Radio::RadioSendStatus::NativeFailure, 0});
        const ESPNowSendResult result = _transport.SendDetailed(peer, _configuration.Protocol, payload, payloadSize);
        if (result.Success) return complete(Radio::RadioSendResult::Accepted());
        switch (result.Failure) {
            case ESPNowSendFailure::NotInitialized:
                return complete({Radio::RadioSendStatus::NotStarted, result.NativeError});
            case ESPNowSendFailure::InvalidArgument:
                return complete({Radio::RadioSendStatus::InvalidAddress, result.NativeError});
            case ESPNowSendFailure::NoMemory:
                return complete({Radio::RadioSendStatus::NoMemory, result.NativeError});
            default:
                return complete({Radio::RadioSendStatus::NativeFailure, result.NativeError});
        }
    }

    void SetReceiver(Radio::IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetWorkSignal(Radio::IRadioWorkSignal* signal) noexcept override {
        _workSignal.store(signal, std::memory_order_release);
    }
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

    void DrainInbound() override {
        while (true) {
            const uint8_t read = _readIndex.load(std::memory_order_relaxed);
            if (read == _writeIndex.load(std::memory_order_acquire)) return;

            const auto& queued = _receiveQueue[read];
            Radio::RadioPacketView packet;
            packet.Source = queued.Source;
            packet.Destination = _localAddress;
            packet.Payload = queued.Length == 0 ? nullptr : queued.Payload.data();
            packet.PayloadSize = queued.Length;
            packet.ReceiveTimestampNanoseconds = queued.TimestampNanoseconds;
            if (_receiver != nullptr) _receiver->OnRadioPacket(*this, packet);

            _readIndex.store(
                static_cast<uint8_t>((read + 1u) % _receiveQueue.size()),
                std::memory_order_release
            );
        }
    }
};

} // namespace ESPressio::ESPNow
