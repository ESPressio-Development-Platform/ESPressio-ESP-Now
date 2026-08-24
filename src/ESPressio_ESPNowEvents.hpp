#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_Event.hpp>
#include <ESPressio_ESPNowTypes.hpp>

namespace ESPressio::Event {

class ESPNowTransportInitializedEvent final : public Event<> {};
class ESPNowTransportInitializationFailedEvent final : public Event<> {};
class ESPNowTransportShutdownEvent final : public Event<> {};

class ESPNowPeerAddedEvent final : public Event<> {
public:
    const ESPNow::MacAddress Address;
    explicit ESPNowPeerAddedEvent(const ESPNow::MacAddress& address) : Address(address) {}
};

class ESPNowPeerRemovedEvent final : public Event<> {
public:
    const ESPNow::MacAddress Address;
    explicit ESPNowPeerRemovedEvent(const ESPNow::MacAddress& address) : Address(address) {}
};

class ESPNowSendAcceptedEvent final : public Event<> {
public:
    const ESPNow::MacAddress Destination;
    const uint8_t Protocol;
    const std::size_t PayloadLength;
    ESPNowSendAcceptedEvent(const ESPNow::MacAddress& destination, uint8_t protocol, std::size_t payloadLength)
        : Destination(destination), Protocol(protocol), PayloadLength(payloadLength) {}
};

class ESPNowSendFailedEvent final : public Event<> {
public:
    const ESPNow::MacAddress Destination;
    const uint8_t Protocol;
    const std::size_t PayloadLength;
    const ESPNow::ESPNowSendFailure Failure;
    const int32_t NativeError;

    ESPNowSendFailedEvent(
        const ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength,
        ESPNow::ESPNowSendFailure failure = ESPNow::ESPNowSendFailure::Unknown,
        int32_t nativeError = 0
    ) : Destination(destination),
        Protocol(protocol),
        PayloadLength(payloadLength),
        Failure(failure),
        NativeError(nativeError) {}
};

} // namespace ESPressio::Event
