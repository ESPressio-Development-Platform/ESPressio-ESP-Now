#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include <ESPressio_ClockSynchronization.hpp>

namespace ESPressio {
namespace ESPNow {

/// <summary>Number of bytes in an IEEE 802.11 MAC address.</summary>
static constexpr std::size_t MacAddressLength = 6;
/// <summary>Maximum ESP-NOW frame size supported by the transport.</summary>
static constexpr std::size_t MaximumFrameSize = 250;
/// <summary>Magic value identifying ESPressio ESP-NOW frames.</summary>
static constexpr uint32_t FrameMagic = 0x4553504EU; // "ESPN"
/// <summary>Current ESPressio ESP-NOW frame format version.</summary>
static constexpr uint8_t FrameVersion = 1;

/// <summary>Built-in protocol identifiers multiplexed over the ESP-NOW transport.</summary>
enum class ESPNowProtocol : uint8_t {
    ClockSynchronization = 1,
    EventTransport = 2,
    CommandTransport = 3,
    SecureTransport = 4,
    StateTransport = 5,
    UserBase = 64
};

/// <summary>Selects whether this node participates in clock synchronization as a client, reference, both, or neither.</summary>
enum class ESPNowClockSynchronizationMode : uint8_t {
    Disabled = 0,
    Client = 1,
    Reference = 2,
    ClientAndReference = 3
};

/// <summary>Selects the Wi-Fi interface used to carry ESP-NOW traffic.</summary>
enum class ESPNowWiFiInterface : uint8_t {
    /// <summary>Resolve the active compatible interface automatically.</summary>
    Auto = 0,
    /// <summary>Use the station interface.</summary>
    Station = 1,
    /// <summary>Use the access-point interface.</summary>
    AccessPoint = 2
};

/// <summary>Library-level classification for an ESP-NOW send failure.</summary>
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

/// <summary>Result of submitting an ESP-NOW frame for transmission.</summary>
struct ESPNowSendResult {
    /// <summary>Whether the send request was accepted successfully.</summary>
    bool Success = false;
    /// <summary>Library-level failure classification when unsuccessful.</summary>
    ESPNowSendFailure Failure = ESPNowSendFailure::Unknown;
    /// <summary>Underlying platform/native error value when available.</summary>
    int32_t NativeError = 0;
    explicit operator bool() const noexcept { return Success; }
};

/// <summary>Value type representing a six-byte ESP-NOW peer MAC address.</summary>
struct MacAddress {
    uint8_t Bytes[MacAddressLength] = {0,0,0,0,0,0};
    MacAddress() = default;
    /// <summary>Copies six address bytes from the supplied pointer when non-null.</summary>
    explicit MacAddress(const uint8_t* address) {
        if (address != nullptr) std::memcpy(Bytes, address, MacAddressLength);
    }
    bool operator==(const MacAddress& other) const {
        return std::memcmp(Bytes, other.Bytes, MacAddressLength) == 0;
    }
    bool operator!=(const MacAddress& other) const { return !(*this == other); }
    /// <summary>Indicates whether every address byte is zero.</summary>
    bool IsZero() const {
        static const uint8_t zero[MacAddressLength] = {0,0,0,0,0,0};
        return std::memcmp(Bytes, zero, MacAddressLength) == 0;
    }
};

/// <summary>Describes the currently available Wi-Fi radio binding for ESP-NOW operation.</summary>
struct ESPNowRadioBinding {
    /// <summary>Preferred or resolved Wi-Fi interface.</summary>
    ESPNowWiFiInterface PreferredInterface = ESPNowWiFiInterface::Auto;
    /// <summary>Required Wi-Fi channel, or zero when not constrained.</summary>
    uint8_t Channel = 0;
    /// <summary>Whether the radio is currently available to ESP-NOW.</summary>
    bool Available = true;
};

/// <summary>Configures ESP-NOW transport initialization, receive task resources, and worker cadence.</summary>
struct ESPNowTransportConfig {
    /// <summary>Whether the transport may initialize Wi-Fi when required.</summary>
    bool InitializeWiFi = true;
    /// <summary>Requested ESP-NOW channel, or zero for the active/default channel.</summary>
    uint8_t Channel = 0;
    /// <summary>Receive task stack size.</summary>
    uint32_t ReceiveTaskStackSize = 4096;
    /// <summary>Receive task priority.</summary>
    uint32_t ReceiveTaskPriority = 2;
    /// <summary>Receive task processor core, or -1 for no explicit affinity.</summary>
    int32_t ReceiveTaskCore = -1;
    /// <summary>Maximum queued receive records and owned receive-pool slots.</summary>
    /// <remarks>The current bounded ownership pool supports up to 64 simultaneously owned receive slots.</remarks>
    std::size_t ReceiveQueueLength = 6;
    /// <summary>Worker iteration interval in milliseconds.</summary>
    uint32_t WorkerIterationIntervalMilliseconds = 5;
};

/// <summary>Configuration used when registering an ESP-NOW peer.</summary>
struct ESPNowPeerConfig {
    /// <summary>Peer MAC address.</summary>
    MacAddress Address;
    /// <summary>Peer channel, or zero to follow the current channel.</summary>
    uint8_t Channel = 0;
    /// <summary>Wi-Fi interface through which the peer should be reached.</summary>
    ESPNowWiFiInterface Interface = ESPNowWiFiInterface::Auto;
    /// <summary>Whether ESP-NOW peer encryption is enabled.</summary>
    bool Encrypt = false;
    /// <summary>Local master key bytes used when encrypted peer operation is configured.</summary>
    uint8_t LocalMasterKey[16] = {0};
};

/// <summary>Received ESP-NOW frame together with its source and local monotonic receive timestamp.</summary>
/// <remarks>Frame storage is owned by ESPNowTransport's bounded receive pool while a lease is outstanding.</remarks>
struct ESPNowReceivedFrame {
    MacAddress Source;
    uint64_t ReceiveMonotonicNanoseconds = 0;
    uint8_t Protocol = 0;
    uint16_t PayloadLength = 0;
    uint8_t Payload[MaximumFrameSize] = {0};
};

/// <summary>
/// Move-only ownership lease for one ESP-NOW frame held in ESPNowTransport's bounded external-memory receive pool.
/// </summary>
/// <remarks>
/// Moving this type transfers ownership without copying payload bytes. Destroying or resetting the final lease returns
/// its pool slot to ESPNowTransport. A synchronous consumer may use the implicit const-frame view without retaining the
/// lease; an asynchronous consumer such as ESPNowRadio moves the lease into its own bounded handoff queue.
/// </remarks>
class ESPNowReceivedFrameLease final {
public:
    using ReleaseFunction = void (*)(void*, uint16_t) noexcept;

    ESPNowReceivedFrameLease() noexcept = default;

    ESPNowReceivedFrameLease(
        ESPNowReceivedFrame* frame,
        void* owner,
        uint16_t slot,
        ReleaseFunction release
    ) noexcept : _frame(frame), _owner(owner), _slot(slot), _release(release) {}

    ~ESPNowReceivedFrameLease() { Reset(); }

    ESPNowReceivedFrameLease(const ESPNowReceivedFrameLease&) = delete;
    ESPNowReceivedFrameLease& operator=(const ESPNowReceivedFrameLease&) = delete;

    ESPNowReceivedFrameLease(ESPNowReceivedFrameLease&& other) noexcept {
        MoveFrom(other);
    }

    ESPNowReceivedFrameLease& operator=(ESPNowReceivedFrameLease&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(other);
        }
        return *this;
    }

    explicit operator bool() const noexcept { return _frame != nullptr; }

    ESPNowReceivedFrame& Frame() noexcept { return *_frame; }
    const ESPNowReceivedFrame& Frame() const noexcept { return *_frame; }

    ESPNowReceivedFrame* operator->() noexcept { return _frame; }
    const ESPNowReceivedFrame* operator->() const noexcept { return _frame; }

    ESPNowReceivedFrame& operator*() noexcept { return *_frame; }
    const ESPNowReceivedFrame& operator*() const noexcept { return *_frame; }

    /// <summary>Allows existing synchronous frame consumers to borrow the leased frame without copying it.</summary>
    operator const ESPNowReceivedFrame&() const noexcept { return Frame(); }

    void Reset() noexcept {
        if (_frame != nullptr && _release != nullptr) {
            _release(_owner, _slot);
        }
        _frame = nullptr;
        _owner = nullptr;
        _slot = 0;
        _release = nullptr;
    }

private:
    void MoveFrom(ESPNowReceivedFrameLease& other) noexcept {
        _frame = other._frame;
        _owner = other._owner;
        _slot = other._slot;
        _release = other._release;
        other._frame = nullptr;
        other._owner = nullptr;
        other._slot = 0;
        other._release = nullptr;
    }

    ESPNowReceivedFrame* _frame = nullptr;
    void* _owner = nullptr;
    uint16_t _slot = 0;
    ReleaseFunction _release = nullptr;
};

/// <summary>Configures ESP-NOW-based clock synchronization behavior.</summary>
struct ESPNowClockSynchronizationConfig {
    /// <summary>Synchronization role performed by this node.</summary>
    ESPNowClockSynchronizationMode Mode = ESPNowClockSynchronizationMode::Disabled;
    /// <summary>Reference peer used while operating as a synchronization client.</summary>
    MacAddress ReferencePeer;
    /// <summary>Interval between synchronization exchanges in milliseconds.</summary>
    uint32_t SynchronizationIntervalMilliseconds = 1000;
    /// <summary>Timing adjustment policy applied to accepted synchronization samples.</summary>
    Timing::ClockSynchronizationAdjustmentMode AdjustmentMode =
        Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
};

} // namespace ESPNow
} // namespace ESPressio
