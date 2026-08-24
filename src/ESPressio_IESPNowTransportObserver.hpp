#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_IObserver.hpp>

#include "ESPressio_ESPNowTypes.hpp"

namespace ESPressio::ESPNow {

class IESPNowTransportObserver :
    public virtual Observable::IObserver {
public:
    virtual ~IESPNowTransportObserver() = default;

    virtual void OnESPNowTransportInitialized() {}
    virtual void OnESPNowTransportInitializationFailed() {}
    virtual void OnESPNowTransportShutdown() {}

    virtual void OnESPNowPeerAdded(
        const MacAddress&
    ) {}

    virtual void OnESPNowPeerRemoved(
        const MacAddress&
    ) {}

    /*
     * Called only after the ESPressio wire header has been validated. This is
     * intentionally protocol-agnostic: discovery, clock, Event, Command,
     * Security and user traffic are all valid evidence that a peer is alive.
     */
    virtual void OnESPNowFrameReceived(
        const MacAddress&,
        uint8_t,
        std::size_t,
        uint64_t
    ) {}

    virtual void OnESPNowSendAccepted(
        const MacAddress&,
        uint8_t,
        std::size_t
    ) {}

    virtual void OnESPNowSendFailed(
        const MacAddress&,
        uint8_t,
        std::size_t
    ) {}

    /*
     * Detailed failure callback added by #40. The default implementation
     * delegates to the original callback, so existing observers remain source
     * compatible while newer observers can inspect the stable failure class
     * and native ESP-IDF esp_err_t value.
     */
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
