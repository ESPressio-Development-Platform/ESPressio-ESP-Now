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
};

} // namespace ESPressio::ESPNow
