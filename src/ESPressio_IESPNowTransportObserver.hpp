#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_IObserver.hpp>

#include "ESPressio_ESPNowTypes.hpp"

namespace ESPressio::ESPNow {

/// <summary>Observes lifecycle, peer, receive, and send activity from the ESP-NOW transport.</summary>
/// <remarks>All callbacks have default no-op implementations so consumers may override only the activity they need.</remarks>
class IESPNowTransportObserver :
    public virtual Observable::IObserver {
public:
    virtual ~IESPNowTransportObserver() = default;

    /// <summary>Called after the native ESP-NOW transport initializes successfully.</summary>
    virtual void OnESPNowTransportInitialized() {}
    /// <summary>Called when native ESP-NOW transport initialization fails.</summary>
    virtual void OnESPNowTransportInitializationFailed() {}
    /// <summary>Called after the ESP-NOW transport has shut down.</summary>
    virtual void OnESPNowTransportShutdown() {}

    /// <summary>Called when a managed ESP-NOW peer is added or updated successfully.</summary>
    virtual void OnESPNowPeerAdded(
        const MacAddress&
    ) {}

    /// <summary>Called when a managed ESP-NOW peer is removed.</summary>
    virtual void OnESPNowPeerRemoved(
        const MacAddress&
    ) {}

    /// <summary>Called after an inbound frame has passed ESPressio wire-header validation.</summary>
    /// <remarks>This callback is protocol-agnostic: discovery, clock, Event, Command, Security, State, and user traffic are all valid peer-liveness evidence.</remarks>
    virtual void OnESPNowFrameReceived(
        const MacAddress&,
        uint8_t,
        std::size_t,
        uint64_t
    ) {}

    /// <summary>Called when an outbound frame is accepted by the native ESP-NOW send API.</summary>
    virtual void OnESPNowSendAccepted(
        const MacAddress&,
        uint8_t,
        std::size_t
    ) {}

    /// <summary>Compatibility callback for an outbound send failure without detailed error classification.</summary>
    virtual void OnESPNowSendFailed(
        const MacAddress&,
        uint8_t,
        std::size_t
    ) {}

    /// <summary>Called when an outbound send fails, including stable failure classification and the native ESP-IDF error value.</summary>
    /// <remarks>The default implementation delegates to OnESPNowSendFailed so existing observers remain compatible.</remarks>
    virtual void OnESPNowSendFailedDetailed(
        const MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength,
        ESPNowSendFailure,
        int32_t
    ) {
        OnESPNowSendFailed(destination, protocol, payloadLength);
    }
};

} // namespace ESPressio::ESPNow
