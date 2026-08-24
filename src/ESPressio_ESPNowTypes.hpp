#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <freertos/FreeRTOS.h>

#include <ESPressio_ClockSynchronization.hpp>

namespace ESPressio {
namespace ESPNow {

static constexpr std::size_t MacAddressLength = 6;
static constexpr std::size_t MaximumFrameSize = 250;
static constexpr uint32_t FrameMagic = 0x4553504EU; // "ESPN"
static constexpr uint8_t FrameVersion = 1;

enum class ESPNowProtocol : uint8_t {
    ClockSynchronization = 1,
    EventTransport = 2,
    CommandTransport = 3,
    SecureTransport = 4,
    UserBase = 64
};

enum class ESPNowClockSynchronizationMode : uint8_t {
    Disabled = 0,
    Client = 1,
    Reference = 2,
    ClientAndReference = 3
};

enum class ESPNowWiFiInterface : uint8_t {
    Auto = 0,
    Station = 1,
    AccessPoint = 2
};

enum class ESPNowSendFailure : uint8_t {
    None = 0,
    NotInitialized,
    InvalidArgument,
    NoMemory,
    PeerNotFound,
    InterfaceMismatch,
    ChannelMismatch,
    RadioUnavailable,
    Internal,
    Unknown
};

struct ESPNowSendResult {
    bool Success = false;
    ESPNowSendFailure Failure = ESPNowSendFailure::Unknown;
    int32_t NativeError = 0;
    explicit operator bool() const noexcept { return Success; }
};

struct MacAddress {
    uint8_t Bytes[MacAddressLength] = {0,0,0,0,0,0};
    MacAddress() = default;
    explicit MacAddress(const uint8_t* address) {
        if (address != nullptr) std::memcpy(Bytes, address, MacAddressLength);
    }
    bool operator==(const MacAddress& other) const {
        return std::memcmp(Bytes, other.Bytes, MacAddressLength) == 0;
    }
    bool operator!=(const MacAddress& other) const { return !(*this == other); }
    bool IsZero() const {
        static const uint8_t zero[MacAddressLength] = {0,0,0,0,0,0};
        return std::memcmp(Bytes, zero, MacAddressLength) == 0;
    }
};

// Logical binding between ESP-NOW and the shared WiFi radio. WiFi remains the
// authority for native radio mode/channel; ESP-NOW consumes this snapshot to
// rebind Auto peers and to expose explicit temporary-unavailability windows
// such as WiFi scans and disruptive mode transitions.
struct ESPNowRadioBinding {
    ESPNowWiFiInterface PreferredInterface = ESPNowWiFiInterface::Auto;
    uint8_t Channel = 0;
    bool Available = true;
};

struct ESPNowTransportConfig {
    bool InitializeWiFi = true;
    uint8_t Channel = 0;

    // Preserved 0.8.x names: these now configure the ESPressio PrecisionThread
    // worker that owns receive processing and protocol maintenance.
    uint32_t ReceiveTaskStackSize = 8192;
    UBaseType_t ReceiveTaskPriority = 2;
    BaseType_t ReceiveTaskCore = tskNO_AFFINITY;
    std::size_t ReceiveQueueLength = 12;

    // Minimum start-to-start interval for ESP-NOW worker iterations. Incoming
    // frames remain queued until the next permitted iteration; they do not
    // bypass this rate limit.
    uint32_t WorkerIterationIntervalMilliseconds = 5;
};

struct ESPNowPeerConfig {
    MacAddress Address;
    uint8_t Channel = 0;
    ESPNowWiFiInterface Interface = ESPNowWiFiInterface::Auto;
    bool Encrypt = false;
    uint8_t LocalMasterKey[16] = {0};
};

struct ESPNowReceivedFrame {
    MacAddress Source;
    uint64_t ReceiveMonotonicNanoseconds = 0;
    uint8_t Protocol = 0;
    uint16_t PayloadLength = 0;
    uint8_t Payload[MaximumFrameSize] = {0};
};

struct ESPNowClockSynchronizationConfig {
    ESPNowClockSynchronizationMode Mode = ESPNowClockSynchronizationMode::Disabled;
    MacAddress ReferencePeer;
    uint32_t SynchronizationIntervalMilliseconds = 1000;
    Timing::ClockSynchronizationAdjustmentMode AdjustmentMode =
        Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
};

} // namespace ESPNow
} // namespace ESPressio
