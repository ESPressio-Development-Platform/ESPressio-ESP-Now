#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_Event.hpp>
#include <ESPressio_ESPNowTypes.hpp>

namespace ESPressio::Event {

/// <summary>Signals successful ESP-NOW transport initialization.</summary>
class ESPNowTransportInitializedEvent final :
    public TypedEvent<ESPNowTransportInitializedEvent> {};

/// <summary>Signals that ESP-NOW transport initialization failed.</summary>
class ESPNowTransportInitializationFailedEvent final :
    public TypedEvent<ESPNowTransportInitializationFailedEvent> {};

/// <summary>Signals that the ESP-NOW transport has shut down.</summary>
class ESPNowTransportShutdownEvent final :
    public TypedEvent<ESPNowTransportShutdownEvent> {};

/// <summary>Signals that an ESP-NOW peer was added to the transport.</summary>
class ESPNowPeerAddedEvent final :
    public TypedEvent<ESPNowPeerAddedEvent> {
public:
    /// <summary>MAC address of the peer that was added.</summary>
    const ESPNow::MacAddress Address;
    explicit ESPNowPeerAddedEvent(const ESPNow::MacAddress& address) : Address(address) {}
};

/// <summary>Signals that an ESP-NOW peer was removed from the transport.</summary>
class ESPNowPeerRemovedEvent final :
    public TypedEvent<ESPNowPeerRemovedEvent> {
public:
    /// <summary>MAC address of the peer that was removed.</summary>
    const ESPNow::MacAddress Address;
    explicit ESPNowPeerRemovedEvent(const ESPNow::MacAddress& address) : Address(address) {}
};

/// <summary>Signals that an outbound ESP-NOW payload was accepted for transmission.</summary>
class ESPNowSendAcceptedEvent final :
    public TypedEvent<ESPNowSendAcceptedEvent> {
public:
    /// <summary>Destination MAC address.</summary>
    const ESPNow::MacAddress Destination;
    /// <summary>ESPressio protocol identifier carried by the frame.</summary>
    const uint8_t Protocol;
    /// <summary>Payload size in bytes.</summary>
    const std::size_t PayloadLength;
    ESPNowSendAcceptedEvent(
        const ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength
    ) : Destination(destination), Protocol(protocol), PayloadLength(payloadLength) {}
};

/// <summary>Signals that an outbound ESP-NOW payload could not be accepted or transmitted.</summary>
class ESPNowSendFailedEvent final :
    public TypedEvent<ESPNowSendFailedEvent> {
public:
    /// <summary>Destination MAC address.</summary>
    const ESPNow::MacAddress Destination;
    /// <summary>ESPressio protocol identifier carried by the frame.</summary>
    const uint8_t Protocol;
    /// <summary>Payload size in bytes.</summary>
    const std::size_t PayloadLength;
    /// <summary>Library-level failure classification.</summary>
    const ESPNow::ESPNowSendFailure Failure;
    /// <summary>Underlying platform/native error value when available.</summary>
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
