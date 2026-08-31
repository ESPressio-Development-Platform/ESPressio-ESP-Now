#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include <ESPressio_ClockSynchronization.hpp>

namespace ESPressio {
namespace ESPNow {

static constexpr std::size_t MacAddressLength = 6;
static constexpr std::size_t MaximumFrameSize = 250;
static constexpr uint32_t FrameMagic = 0x4553504EU;
static constexpr uint8_t FrameVersion = 1;

enum class ESPNowProtocol : uint8_t {
    ClockSynchronization = 1,
    EventTransport = 2,
    CommandTransport = 3,
    SecureTransport = 4,
    StateTransport = 5,
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

struct ESPNowRadioBinding {
    ESPNowWiFiInterface PreferredInterface = ESPNowWiFiInterface::Auto;
    uint8_t Channel = 0;
    bool Available = true;
};

struct ESPNowTransportConfig {
    bool InitializeWiFi = true;
    uint8_t Channel = 0;
    uint32_t ReceiveTaskStackSize = 4096;
    uint32_t ReceiveTaskPriority = 2;
    int32_t ReceiveTaskCore = -1;
    /// <summary>Maximum queued receive records and owned receive-pool slots.</summary>
    /// <remarks>The current bounded ownership pool supports up to 64 simultaneously owned receive slots.</remarks>
    std::size_t ReceiveQueueLength = 6;
    uint32_t WorkerIterationIntervalMilliseconds = 5;
};

struct ESPNowPeerConfig {
    MacAddress Address;
    uint8_t Channel = 0;
    ESPNowWiFiInterface Interface = ESPNowWiFiInterface::Auto;
    bool Encrypt = false;
    uint8_t LocalMasterKey[16] = {0};
};

/// <summary>Received ESP-NOW frame stored in an ESPNowTransport-owned bounded receive-pool slot.</summary>
struct ESPNowReceivedFrame {
    MacAddress Source;
    uint64_t ReceiveMonotonicNanoseconds = 0;
    uint8_t Protocol = 0;
    uint16_t PayloadLength = 0;
    uint8_t Payload[MaximumFrameSize] = {0};
};

/// <summary>Move-only ownership lease for one ESP-NOW frame in the transport's external-memory receive pool.</summary>
/// <remarks>
/// Ownership transfer is explicit. Borrowing the frame requires calling Frame(); there is deliberately no implicit
/// conversion to the pre-redesign receive-frame API. Destroying/resetting the final lease returns its pool slot.
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

    ESPNowReceivedFrameLease(ESPNowReceivedFrameLease&& other) noexcept { MoveFrom(other); }

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

    void Reset() noexcept {
        if (_frame != nullptr && _release != nullptr) _release(_owner, _slot);
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

struct ESPNowClockSynchronizationConfig {
    ESPNowClockSynchronizationMode Mode = ESPNowClockSynchronizationMode::Disabled;
    MacAddress ReferencePeer;
    uint32_t SynchronizationIntervalMilliseconds = 1000;
    Timing::ClockSynchronizationAdjustmentMode AdjustmentMode =
        Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
};

} // namespace ESPNow
} // namespace ESPressio
