#pragma once

/*
 * Optional ESPressio Event integration.
 *
 * This header is intentionally NOT included by ESPressio_ESPNow.hpp so that
 * ordinary ESP-NOW / Timing users do not acquire an ESPressio Event or
 * ESPressio Serializable dependency.
 */

#if !__has_include(<ESPressio_EventTransport.hpp>)
#error "ESPressio ESP-NOW Event Transport requires ESPressio Event in the consuming project."
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <utility>

#include <ESPressio_EventTransport.hpp>
#include <ESPressio_Memory.hpp>
#include <ESPressio_Task.hpp>

#include "ESPressio_ESPNowAsyncProtocolHandler.hpp"
#include "ESPressio_ESPNowTransport.hpp"
#include "ESPressio_ESPNowTypes.hpp"

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS
    #define ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS 16
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES
    #define ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES 8
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE
    #define ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE 16384
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS
    #define ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS 5000
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_OUTBOUND_STACK_SIZE
    #define ESPRESSIO_ESPNOW_EVENT_OUTBOUND_STACK_SIZE 4096
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_OUTBOUND_QUEUE_DEPTH
    #define ESPRESSIO_ESPNOW_EVENT_OUTBOUND_QUEUE_DEPTH 8
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_OUTBOUND_PRIORITY
    #define ESPRESSIO_ESPNOW_EVENT_OUTBOUND_PRIORITY 2
#endif

namespace ESPressio::ESPNow {

/// <summary>Implements the ESPressio Event owned-byte transport contract over fragmented ESP-NOW protocol frames.</summary>
/// <remarks>
/// Inbound radio callbacks are handed to a bounded asynchronous worker before Event delivery. Outbound Event packets are
/// accepted into an ExternalPreferred pool and only a pointer is queued to a dedicated TaskExecutor. Fragmentation and
/// physical ESP-NOW submission therefore execute on the transport task rather than the EventTransportManager stack.
/// </remarks>
class ESPNowEventTransport final : public Event::IEventTransport {
private:
#pragma pack(push, 1)
    struct FragmentHeader {
        static constexpr uint32_t MagicValue = 0x45564E57u; // EVNW
        static constexpr uint8_t VersionValue = 1;

        uint32_t Magic = MagicValue;
        uint8_t Version = VersionValue;
        uint8_t Reserved = 0;
        uint16_t FragmentIndex = 0;
        uint16_t FragmentCount = 0;
        uint16_t FragmentLength = 0;
        uint32_t TotalLength = 0;
        uint64_t MessageID = 0;
    };
#pragma pack(pop)

    static constexpr std::size_t ESPNowWireHeaderSize = 8;
    static constexpr std::size_t MaximumProtocolPayload = MaximumFrameSize - ESPNowWireHeaderSize;
    static constexpr std::size_t MaximumFragmentPayload = MaximumProtocolPayload - sizeof(FragmentHeader);
    static constexpr std::size_t MaximumFragments =
        (ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE + MaximumFragmentPayload - 1) /
        MaximumFragmentPayload;
    static constexpr std::size_t ReceiptBitmapBytes = (MaximumFragments + 7u) / 8u;
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

    template<typename T>
    using ExternalVector = System::Memory::Vector<T, ExternalPreferred>;
    using ByteBuffer = Event::EventTransportBuffer;

    static_assert(MaximumFragmentPayload > 0, "ESP-NOW Event fragment header exceeds ESP-NOW protocol payload.");
    static_assert(MaximumFragments <= UINT16_MAX, "Configured ESP-NOW Event packet size exceeds wire fragment capacity.");

    struct Reassembly {
        MacAddress Source;
        uint64_t MessageID = 0;
        uint32_t TotalLength = 0;
        uint16_t FragmentCount = 0;
        uint16_t ReceivedCount = 0;
        uint64_t LastReceiveMonotonicNanoseconds = 0;
        ByteBuffer Data;
        std::array<uint8_t, ReceiptBitmapBytes> ReceivedBits{};
    };

public:
    /// <summary>Task and queue resources used for outbound Event fragmentation and physical send.</summary>
    struct OutboundConfiguration {
        const char* Name = "espnowEventTx";
        uint32_t StackSize = ESPRESSIO_ESPNOW_EVENT_OUTBOUND_STACK_SIZE;
        uint32_t Priority = ESPRESSIO_ESPNOW_EVENT_OUTBOUND_PRIORITY;
        int32_t Core = -1;
        std::size_t QueueDepth = ESPRESSIO_ESPNOW_EVENT_OUTBOUND_QUEUE_DEPTH;
        Task::TaskQueueOverflowPolicy OverflowPolicy = Task::TaskQueueOverflowPolicy::Reject;
    };

private:
    struct OutboundWork {
        Event::EventTransportPacket Packet;
        std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS> Destinations{};
        std::size_t DestinationCount = 0;
    };

    using ReassemblyStorage = ExternalVector<Reassembly>;
    using OutboundStorage = ExternalVector<OutboundWork>;
    using OutboundSlotStorage = ExternalVector<uint8_t>;
    using OutboundWorkItem = OutboundWork*;
    using OutboundExecutor = Task::TaskExecutor<OutboundWorkItem>;
    using OutboundExecutorPtr = System::Memory::UniquePtr<OutboundExecutor, ExternalPreferred>;

    ESPNowTransport* _transport = nullptr;
    Event::IEventTransportReceiver* _receiver = nullptr;
    std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS> _destinations{};
    std::size_t _destinationCount = 0;
    ReassemblyStorage _reassemblies;
    ESPNowAsyncProtocolHandler _asyncHandler;
    OutboundExecutorPtr _outboundExecutor;
    OutboundStorage _outboundWorks;
    OutboundSlotStorage _outboundSlots;
    mutable System::Synchronization::Mutex _outboundLifecycleMutex;
    mutable System::Synchronization::Mutex _mutex;
    std::atomic<uint64_t> _outboundRejected{0};
    std::atomic<bool> _initialized{false};

    static bool IsFragmentReceived(const Reassembly& state, std::size_t index) noexcept {
        const std::size_t byteIndex = index >> 3u;
        const uint8_t mask = static_cast<uint8_t>(1u << (index & 7u));
        return byteIndex < state.ReceivedBits.size() && (state.ReceivedBits[byteIndex] & mask) != 0u;
    }

    static void MarkFragmentReceived(Reassembly& state, std::size_t index) noexcept {
        const std::size_t byteIndex = index >> 3u;
        const uint8_t mask = static_cast<uint8_t>(1u << (index & 7u));
        if (byteIndex < state.ReceivedBits.size()) {
            state.ReceivedBits[byteIndex] = static_cast<uint8_t>(state.ReceivedBits[byteIndex] | mask);
        }
    }

    OutboundWorkItem AcquireOutboundWork(
        Event::EventTransportPacket packet,
        const std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS>& destinations,
        std::size_t destinationCount
    ) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for (std::size_t index = 0; index < _outboundWorks.size(); ++index) {
            if (_outboundSlots[index] != 0) continue;
            _outboundSlots[index] = 1;
            auto& work = _outboundWorks[index];
            work.Packet = std::move(packet);
            work.DestinationCount = destinationCount;
            for (std::size_t destination = 0; destination < destinationCount; ++destination) {
                work.Destinations[destination] = destinations[destination];
            }
            return &work;
        }
        return nullptr;
    }

    void ReleaseOutboundWork(OutboundWorkItem work) noexcept {
        if (work == nullptr) return;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if (_outboundWorks.empty()) return;
        OutboundWork* first = _outboundWorks.data();
        OutboundWork* end = first + _outboundWorks.size();
        if (work < first || work >= end) return;
        const std::size_t index = static_cast<std::size_t>(work - first);
        work->Packet = {};
        work->DestinationCount = 0;
        _outboundSlots[index] = 0;
    }

    bool SendToDestination(
        ESPNowTransport& transport,
        const MacAddress& destination,
        const Event::EventTransportPacket& packet
    ) {
        if (!packet || packet.Size() > ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE) return false;

        const std::size_t fragmentCountSize =
            (packet.Size() + MaximumFragmentPayload - 1) / MaximumFragmentPayload;
        if (fragmentCountSize == 0 || fragmentCountSize > UINT16_MAX || packet.Size() > UINT32_MAX) {
            return false;
        }

        const uint16_t fragmentCount = static_cast<uint16_t>(fragmentCountSize);
        std::array<uint8_t, MaximumProtocolPayload> payload{};
        for (uint16_t index = 0; index < fragmentCount; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * MaximumFragmentPayload;
            const std::size_t fragmentLength = std::min(packet.Size() - offset, MaximumFragmentPayload);

            FragmentHeader header;
            header.FragmentIndex = index;
            header.FragmentCount = fragmentCount;
            header.FragmentLength = static_cast<uint16_t>(fragmentLength);
            header.TotalLength = static_cast<uint32_t>(packet.Size());
            header.MessageID = packet.MessageID();

            std::memcpy(payload.data(), &header, sizeof(header));
            std::memcpy(payload.data() + sizeof(header), packet.Data() + offset, fragmentLength);

            if (!transport.Send(
                    destination,
                    static_cast<uint8_t>(ESPNowProtocol::EventTransport),
                    payload.data(),
                    sizeof(header) + fragmentLength)) {
                return false;
            }
        }
        return true;
    }

    void ProcessOutbound(OutboundWorkItem work) {
        if (work == nullptr) return;
        class Release final {
        public:
            Release(ESPNowEventTransport& owner, OutboundWorkItem item) noexcept
                : _owner(owner), _item(item) {}
            ~Release() { _owner.ReleaseOutboundWork(_item); }
        private:
            ESPNowEventTransport& _owner;
            OutboundWorkItem _item;
        } release(*this, work);

        ESPNowTransport* transport = nullptr;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            transport = _transport;
        }
        if (transport == nullptr) return;

        for (std::size_t index = 0; index < work->DestinationCount; ++index) {
            (void)SendToDestination(*transport, work->Destinations[index], work->Packet);
        }
    }

    void HandleFrame(const ESPNowReceivedFrame& frame) {
        if (frame.PayloadLength < sizeof(FragmentHeader)) return;

        FragmentHeader header;
        std::memcpy(&header, frame.Payload, sizeof(header));
        if (
            header.Magic != FragmentHeader::MagicValue ||
            header.Version != FragmentHeader::VersionValue ||
            header.FragmentCount == 0 ||
            header.FragmentCount > MaximumFragments ||
            header.FragmentIndex >= header.FragmentCount ||
            header.FragmentLength > MaximumFragmentPayload ||
            sizeof(FragmentHeader) + header.FragmentLength > frame.PayloadLength ||
            header.TotalLength == 0 ||
            header.TotalLength > ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE
        ) return;

        const std::size_t offset = static_cast<std::size_t>(header.FragmentIndex) * MaximumFragmentPayload;
        if (offset + header.FragmentLength > header.TotalLength) return;

        if (header.FragmentCount == 1) {
            if (header.FragmentIndex != 0 || header.FragmentLength != header.TotalLength) return;
            Event::IEventTransportReceiver* receiver = nullptr;
            {
                std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
                receiver = _receiver;
            }
            if (receiver == nullptr) return;

            ByteBuffer packetBytes;
            try { packetBytes.resize(header.FragmentLength); }
            catch (...) { return; }
            std::memcpy(
                packetBytes.data(),
                frame.Payload + sizeof(FragmentHeader),
                header.FragmentLength
            );
            receiver->ReceiveEventTransportPacket(
                this,
                Event::EventTransportPacket(std::move(packetBytes), header.MessageID)
            );
            return;
        }

        ByteBuffer completed;
        Event::IEventTransportReceiver* receiver = nullptr;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            const uint64_t timeoutNanoseconds =
                static_cast<uint64_t>(ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS) * 1000000ULL;

            _reassemblies.erase(
                std::remove_if(
                    _reassemblies.begin(),
                    _reassemblies.end(),
                    [&](const Reassembly& current) {
                        return frame.ReceiveMonotonicNanoseconds > current.LastReceiveMonotonicNanoseconds &&
                            frame.ReceiveMonotonicNanoseconds - current.LastReceiveMonotonicNanoseconds > timeoutNanoseconds;
                    }
                ),
                _reassemblies.end()
            );

            auto found = std::find_if(
                _reassemblies.begin(),
                _reassemblies.end(),
                [&](const Reassembly& current) {
                    return current.MessageID == header.MessageID && current.Source == frame.Source;
                }
            );

            if (found == _reassemblies.end()) {
                if (_reassemblies.size() >= ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES) {
                    const auto oldest = std::min_element(
                        _reassemblies.begin(),
                        _reassemblies.end(),
                        [](const Reassembly& left, const Reassembly& right) {
                            return left.LastReceiveMonotonicNanoseconds < right.LastReceiveMonotonicNanoseconds;
                        }
                    );
                    if (oldest != _reassemblies.end()) _reassemblies.erase(oldest);
                }

                Reassembly reassembly;
                reassembly.Source = frame.Source;
                reassembly.MessageID = header.MessageID;
                reassembly.TotalLength = header.TotalLength;
                reassembly.FragmentCount = header.FragmentCount;
                reassembly.LastReceiveMonotonicNanoseconds = frame.ReceiveMonotonicNanoseconds;
                try {
                    reassembly.Data.resize(header.TotalLength);
                    _reassemblies.push_back(std::move(reassembly));
                } catch (...) { return; }
                found = std::prev(_reassemblies.end());
            }

            if (found->TotalLength != header.TotalLength || found->FragmentCount != header.FragmentCount) {
                _reassemblies.erase(found);
                return;
            }

            found->LastReceiveMonotonicNanoseconds = frame.ReceiveMonotonicNanoseconds;
            if (!IsFragmentReceived(*found, header.FragmentIndex)) {
                std::memcpy(
                    found->Data.data() + offset,
                    frame.Payload + sizeof(FragmentHeader),
                    header.FragmentLength
                );
                MarkFragmentReceived(*found, header.FragmentIndex);
                ++found->ReceivedCount;
            }

            if (found->ReceivedCount == found->FragmentCount) {
                completed = std::move(found->Data);
                _reassemblies.erase(found);
                receiver = _receiver;
            }
        }

        if (receiver != nullptr && !completed.empty()) {
            receiver->ReceiveEventTransportPacket(
                this,
                Event::EventTransportPacket(std::move(completed), header.MessageID)
            );
        }
    }

public:
    ESPNowEventTransport() = default;
    explicit ESPNowEventTransport(ESPNowTransport& transport) : _transport(&transport) {}

    ESPNowEventTransport(const ESPNowEventTransport&) = delete;
    ESPNowEventTransport& operator=(const ESPNowEventTransport&) = delete;
    ~ESPNowEventTransport() override { Shutdown(); }

    bool Initialize(
        ESPNowTransport& transport = ESPNowTransport::GetInstance(),
        ESPNowAsyncProtocolHandler::Configuration asyncConfiguration = {},
        OutboundConfiguration outboundConfiguration = {}
    ) {
        if (_initialized.load(std::memory_order_acquire)) return true;
        if (!transport.GetIsInitialized() || outboundConfiguration.StackSize == 0 ||
            outboundConfiguration.QueueDepth == 0 ||
            outboundConfiguration.QueueDepth > std::numeric_limits<std::size_t>::max() - 2U) return false;

        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _transport = &transport;
        }
        asyncConfiguration.Name = "espnowEvent";

        try {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _reassemblies.reserve(ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES);
            const std::size_t outboundPoolSize = outboundConfiguration.QueueDepth + 2U;
            _outboundWorks.resize(outboundPoolSize);
            _outboundSlots.assign(outboundPoolSize, 0);
        } catch (...) {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _transport = nullptr;
            _outboundWorks.clear();
            _outboundSlots.clear();
            return false;
        }

        if (!_asyncHandler.Initialize(
                [this](const ESPNowReceivedFrame& frame) { HandleFrame(frame); },
                asyncConfiguration)) {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _transport = nullptr;
            _outboundWorks.clear();
            _outboundSlots.clear();
            return false;
        }

        Task::TaskConfiguration taskConfiguration;
        taskConfiguration.Name = outboundConfiguration.Name;
        taskConfiguration.StackSize = outboundConfiguration.StackSize;
        taskConfiguration.Priority = outboundConfiguration.Priority;
        taskConfiguration.Core = outboundConfiguration.Core;
        taskConfiguration.QueueDepth = outboundConfiguration.QueueDepth;
        taskConfiguration.OverflowPolicy = outboundConfiguration.OverflowPolicy;
        taskConfiguration.MemoryPolicy = Task::TaskMemoryPolicy::PreferExternal;

        auto executor = System::Memory::MakeUnique<OutboundExecutor, ExternalPreferred>(taskConfiguration);
        if (executor->Initialize(
                [this](OutboundWorkItem const& work) { ProcessOutbound(work); },
                [this](OutboundWorkItem const& work) { ReleaseOutboundWork(work); }
            ) != Task::TaskExecutionStatus::Success ||
            executor->Start() != Task::TaskExecutionStatus::Success) {
            executor->Stop();
            _asyncHandler.Shutdown();
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _transport = nullptr;
            _outboundWorks.clear();
            _outboundSlots.clear();
            return false;
        }

        {
            std::lock_guard<System::Synchronization::Mutex> lifecycle(_outboundLifecycleMutex);
            _outboundExecutor = std::move(executor);
        }

        if (!transport.RegisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::EventTransport),
                [this](const ESPNowReceivedFrame& frame) { (void)_asyncHandler.Submit(frame); })) {
            Shutdown();
            return false;
        }

        _outboundRejected.store(0, std::memory_order_release);
        _initialized.store(true, std::memory_order_release);
        return true;
    }

    void Shutdown() {
        _initialized.store(false, std::memory_order_release);

        ESPNowTransport* transport = nullptr;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            transport = _transport;
        }
        if (transport != nullptr) {
            transport->UnregisterProtocolHandler(static_cast<uint8_t>(ESPNowProtocol::EventTransport));
        }

        OutboundExecutorPtr outboundExecutor;
        {
            std::lock_guard<System::Synchronization::Mutex> lifecycle(_outboundLifecycleMutex);
            outboundExecutor = std::move(_outboundExecutor);
        }
        if (outboundExecutor) outboundExecutor->Stop();

        _asyncHandler.Shutdown();
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _receiver = nullptr;
            _destinationCount = 0;
            _reassemblies.clear();
            _outboundWorks.clear();
            _outboundSlots.clear();
            _transport = nullptr;
            for (auto& destination : _destinations) destination = MacAddress{};
        }
    }

    bool GetIsInitialized() const noexcept { return _initialized.load(std::memory_order_acquire); }

    Task::TaskExecutionStatistics GetAsyncHandlerStatistics() const {
        return _asyncHandler.GetStatistics();
    }

    uint64_t GetRejectedAsyncHandoffCount() const noexcept {
        return _asyncHandler.GetRejectedHandoffCount();
    }

    Task::TaskExecutionStatistics GetOutboundStatistics() const {
        std::lock_guard<System::Synchronization::Mutex> lifecycle(_outboundLifecycleMutex);
        return _outboundExecutor ? _outboundExecutor->GetStatistics() : Task::TaskExecutionStatistics{};
    }

    uint64_t GetRejectedOutboundHandoffCount() const noexcept {
        return _outboundRejected.load(std::memory_order_relaxed);
    }

    bool AddDestination(const MacAddress& destination) {
        if (destination.IsZero()) return false;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for (std::size_t index = 0; index < _destinationCount; ++index) {
            if (_destinations[index] == destination) return true;
        }
        if (_destinationCount >= _destinations.size()) return false;
        _destinations[_destinationCount++] = destination;
        return true;
    }

    bool RemoveDestination(const MacAddress& destination) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for (std::size_t index = 0; index < _destinationCount; ++index) {
            if (_destinations[index] != destination) continue;
            for (std::size_t move = index + 1; move < _destinationCount; ++move) {
                _destinations[move - 1] = _destinations[move];
            }
            --_destinationCount;
            _destinations[_destinationCount] = MacAddress{};
            return true;
        }
        return false;
    }

    void ClearDestinations() {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _destinationCount = 0;
        for (auto& destination : _destinations) destination = MacAddress{};
    }

    std::size_t GetDestinationCount() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return _destinationCount;
    }

    bool Send(Event::EventTransportPacket packet) override {
        if (!_initialized.load(std::memory_order_acquire) || !packet) return false;

        std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS> destinations{};
        std::size_t destinationCount = 0;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if (_transport == nullptr) return false;
            destinationCount = _destinationCount;
            for (std::size_t index = 0; index < destinationCount; ++index) {
                destinations[index] = _destinations[index];
            }
        }
        if (destinationCount == 0) return false;

        std::lock_guard<System::Synchronization::Mutex> lifecycle(_outboundLifecycleMutex);
        if (!_outboundExecutor) return false;
        OutboundWorkItem work = AcquireOutboundWork(
            std::move(packet),
            destinations,
            destinationCount
        );
        if (work == nullptr) {
            _outboundRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (_outboundExecutor->Submit(work) != Task::TaskExecutionStatus::Success) {
            ReleaseOutboundWork(work);
            _outboundRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    void SetReceiver(Event::IEventTransportReceiver* receiver) override {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _receiver = receiver;
    }
};

} // namespace ESPressio::ESPNow