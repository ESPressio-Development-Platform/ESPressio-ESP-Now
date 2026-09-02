#pragma once

#include <ESPressio_ESPNowTransport.hpp>
#include <ESPressio_IESPNowTransportObserver.hpp>

#include "ESPressio_ESPNowEvents.hpp"

namespace ESPressio::Event {

/// <summary>Bridges ESP-NOW transport observer notifications into local ESPressio Event instances.</summary>
/// <remarks>The bridge is a singleton observer; generated Events are queued through the ordinary EventManager path.</remarks>
class ESPNowTransportEventBridge final :
    public ESPNow::IESPNowTransportObserver {
private:
    Observable::ObserverHandlePtr _observerHandle;
    bool _initialized = false;

    ESPNowTransportEventBridge() = default;

public:
    ESPNowTransportEventBridge(const ESPNowTransportEventBridge&) = delete;
    ESPNowTransportEventBridge& operator=(const ESPNowTransportEventBridge&) = delete;

    /// <summary>Returns the process-wide ESP-NOW transport Event bridge.</summary>
    static ESPNowTransportEventBridge& GetInstance() {
        static ESPNowTransportEventBridge instance;
        return instance;
    }

    /// <summary>Registers the bridge with an ESP-NOW transport.</summary>
    /// <returns>True when observation is active.</returns>
    bool Initialize(
        ESPNow::ESPNowTransport& transport = ESPNow::ESPNowTransport::GetInstance()
    ) {
        if (_initialized) return true;
        _observerHandle = transport.RegisterObserver(this);
        _initialized = static_cast<bool>(_observerHandle);
        return _initialized;
    }

    /// <summary>Releases the transport observer registration.</summary>
    void Shutdown() {
        _observerHandle.reset();
        _initialized = false;
    }

    /// <summary>Reports whether the bridge is currently observing a transport.</summary>
    bool IsInitialized() const { return _initialized; }

    /// <inheritdoc/>
    void OnESPNowTransportInitialized() override {
        (new ESPNowTransportInitializedEvent())->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowTransportInitializationFailed() override {
        (new ESPNowTransportInitializationFailedEvent())->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowTransportShutdown() override {
        (new ESPNowTransportShutdownEvent())->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowPeerAdded(const ESPNow::MacAddress& address) override {
        (new ESPNowPeerAddedEvent(address))->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowPeerRemoved(const ESPNow::MacAddress& address) override {
        (new ESPNowPeerRemovedEvent(address))->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowSendAccepted(
        const ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength
    ) override {
        (new ESPNowSendAcceptedEvent(destination, protocol, payloadLength))->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowSendFailed(
        const ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength
    ) override {
        // Compatibility path for callers/implementations that only provide the
        // original observer callback.
        (new ESPNowSendFailedEvent(destination, protocol, payloadLength))->Queue();
    }

    /// <inheritdoc/>
    void OnESPNowSendFailedDetailed(
        const ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength,
        ESPNow::ESPNowSendFailure failure,
        int32_t nativeError
    ) override {
        (new ESPNowSendFailedEvent(
            destination,
            protocol,
            payloadLength,
            failure,
            nativeError
        ))->Queue();
    }
};

} // namespace ESPressio::Event
