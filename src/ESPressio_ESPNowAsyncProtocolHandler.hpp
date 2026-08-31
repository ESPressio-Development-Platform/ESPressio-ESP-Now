#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_Task.hpp>

#include "ESPressio_ESPNowTransport.hpp"

#ifndef ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_STACK_SIZE
    #define ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_STACK_SIZE 6144
#endif

#ifndef ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_QUEUE_DEPTH
    #define ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_QUEUE_DEPTH 8
#endif

#ifndef ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_PRIORITY
    #define ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_PRIORITY 2
#endif

namespace ESPressio {
namespace ESPNow {

/// <summary>
/// Moves received ESP-NOW frame leases from the transport worker to a dedicated task executor without copying payloads.
/// </summary>
/// <remarks>
/// Packet bytes remain stationary in ESPNowTransport's externally preferred receive pool. This handler retains only a
/// bounded pool of small move-only leases while TaskExecutor queues trivially-copyable lease pointers. Releasing an
/// executed, rejected, evicted or shutdown work item returns the underlying transport pool slot automatically.
/// </remarks>
class ESPNowAsyncProtocolHandler {
public:
    /// <summary>Callback invoked for each frame successfully handed to the protocol worker.</summary>
    using Handler = std::function<void(const ESPNowReceivedFrame&)>;

    /// <summary>Task and queue resources used by the asynchronous protocol worker.</summary>
    struct Configuration {
        const char* Name = "espnowProtocol";
        uint32_t StackSize = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_STACK_SIZE;
        uint32_t Priority = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_PRIORITY;
        int32_t Core = -1;
        size_t QueueDepth = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_QUEUE_DEPTH;
        Task::TaskQueueOverflowPolicy OverflowPolicy = Task::TaskQueueOverflowPolicy::Reject;
    };

private:
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;
    using WorkItem = ESPNowReceivedFrameLease*;
    using Executor = Task::TaskExecutor<WorkItem>;
    using ExecutorPtr = System::Memory::UniquePtr<Executor, ExternalPreferred>;
    using LeaseStorage = System::Memory::Vector<ESPNowReceivedFrameLease, ExternalPreferred>;
    using SlotStorage = System::Memory::Vector<uint8_t, ExternalPreferred>;

    Configuration _configuration{};
    ExecutorPtr _executor;
    LeaseStorage _leases;
    SlotStorage _slotInUse;
    Handler _handler;
    mutable System::Synchronization::Mutex _lifecycleMutex;
    mutable System::Synchronization::Mutex _poolMutex;
    std::atomic<uint64_t> _handoffRejected{0};
    std::atomic<bool> _initialized{false};

    WorkItem AcquireLease(ESPNowReceivedFrameLease&& lease) {
        std::lock_guard<System::Synchronization::Mutex> lock(_poolMutex);
        for (std::size_t index = 0; index < _leases.size(); ++index) {
            if (_slotInUse[index] != 0) continue;
            _slotInUse[index] = 1;
            _leases[index] = std::move(lease);
            return &_leases[index];
        }
        return nullptr;
    }

    void ReleaseLease(WorkItem lease) noexcept {
        if (lease == nullptr) return;
        std::lock_guard<System::Synchronization::Mutex> lock(_poolMutex);
        if (_leases.empty()) return;
        ESPNowReceivedFrameLease* const first = _leases.data();
        ESPNowReceivedFrameLease* const end = first + _leases.size();
        if (lease < first || lease >= end) return;
        const std::size_t index = static_cast<std::size_t>(lease - first);
        lease->Reset();
        _slotInUse[index] = 0;
    }

    void ProcessFrame(WorkItem lease) {
        if (lease == nullptr || !*lease) return;
        class LeaseRelease final {
        public:
            LeaseRelease(ESPNowAsyncProtocolHandler& owner, WorkItem item) noexcept
                : _owner(owner), _item(item) {}
            ~LeaseRelease() { _owner.ReleaseLease(_item); }
        private:
            ESPNowAsyncProtocolHandler& _owner;
            WorkItem _item;
        } release(*this, lease);

        if (_handler) _handler(lease->Frame());
    }

public:
    ESPNowAsyncProtocolHandler() = default;
    ~ESPNowAsyncProtocolHandler() { Shutdown(); }

    ESPNowAsyncProtocolHandler(const ESPNowAsyncProtocolHandler&) = delete;
    ESPNowAsyncProtocolHandler& operator=(const ESPNowAsyncProtocolHandler&) = delete;

    bool Initialize(Handler handler) {
        return Initialize(std::move(handler), Configuration{});
    }

    bool Initialize(Handler handler, Configuration configuration) {
        Shutdown();
        if (!handler || configuration.StackSize == 0 || configuration.QueueDepth == 0) return false;
        if (configuration.QueueDepth > std::numeric_limits<size_t>::max() - 2U) return false;

        _configuration = configuration;
        const size_t poolSize = configuration.QueueDepth + 2U;
        try {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _leases.clear();
            _leases.resize(poolSize);
            _slotInUse.assign(poolSize, 0);
            _handler = std::move(handler);
        } catch (...) {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _leases.clear();
            _slotInUse.clear();
            return false;
        }

        Task::TaskConfiguration taskConfiguration;
        taskConfiguration.Name = configuration.Name;
        taskConfiguration.StackSize = configuration.StackSize;
        taskConfiguration.Priority = configuration.Priority;
        taskConfiguration.Core = configuration.Core;
        taskConfiguration.QueueDepth = configuration.QueueDepth;
        taskConfiguration.OverflowPolicy = configuration.OverflowPolicy;
        taskConfiguration.MemoryPolicy = Task::TaskMemoryPolicy::PreferExternal;

        auto executor = System::Memory::MakeUnique<Executor, ExternalPreferred>(taskConfiguration);
        const auto initialized = executor->Initialize(
            [this](WorkItem const& lease) { ProcessFrame(lease); },
            [this](WorkItem const& discarded) { ReleaseLease(discarded); }
        );

        if (initialized != Task::TaskExecutionStatus::Success) {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _leases.clear();
            _slotInUse.clear();
            return false;
        }

        if (executor->Start() != Task::TaskExecutionStatus::Success) {
            executor->Stop();
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _leases.clear();
            _slotInUse.clear();
            return false;
        }

        {
            std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
            _executor = std::move(executor);
            _handoffRejected.store(0, std::memory_order_release);
            _initialized.store(true, std::memory_order_release);
        }
        return true;
    }

    void Shutdown() {
        ExecutorPtr executor;
        {
            std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
            _initialized.store(false, std::memory_order_release);
            executor = std::move(_executor);
        }
        if (executor) {
            executor->Stop();
            executor.reset();
        }

        std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
        _handler = {};
        for (auto& lease : _leases) lease.Reset();
        _leases.clear();
        _slotInUse.clear();
    }

    bool GetIsInitialized() const noexcept {
        return _initialized.load(std::memory_order_acquire);
    }

    /// <summary>Moves one received transport lease into the asynchronous worker handoff.</summary>
    /// <remarks>Failure leaves no payload copy behind; the incoming lease is released automatically on return.</remarks>
    bool Submit(ESPNowReceivedFrameLease&& lease) {
        if (!lease) return false;
        std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
        if (!_initialized.load(std::memory_order_acquire) || !_executor) return false;

        WorkItem item = AcquireLease(std::move(lease));
        if (item == nullptr) {
            _handoffRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto result = _executor->Submit(item);
        if (result != Task::TaskExecutionStatus::Success) {
            ReleaseLease(item);
            _handoffRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    Task::TaskExecutionStatistics GetStatistics() const {
        std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
        return _executor ? _executor->GetStatistics() : Task::TaskExecutionStatistics{};
    }

    uint64_t GetRejectedHandoffCount() const noexcept {
        return _handoffRejected.load(std::memory_order_acquire);
    }
};

}
}
