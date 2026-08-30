#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
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
/// Moves received ESP-NOW protocol frames from the transport callback path to a dedicated task executor.
/// </summary>
/// <remarks>
/// The supplied handler executes asynchronously using the configured task and queue resources. Full frames are retained in a bounded externally preferred pool while the executor queue carries only frame pointers, avoiding repeated whole-frame queue copies and a full-frame worker-stack local. The pool includes capacity for all queued frames, one frame currently executing, and one incoming frame so <c>DropOldest</c> can evict an accepted item without losing the incoming handoff. Frames that cannot be handed off are counted as rejected handoffs. The underlying task stack remains on the platform-safe execution path. Executor publication and teardown are serialized so receive callbacks cannot dereference an executor after shutdown has taken ownership of it for destruction.
/// </remarks>
class ESPNowAsyncProtocolHandler {
public:
    /// <summary>Callback invoked for each frame successfully handed to the protocol worker.</summary>
    using Handler = std::function<void(const ESPNowReceivedFrame&)>;

    /// <summary>Task and queue resources used by the asynchronous protocol worker.</summary>
    struct Configuration {
        /// <summary>Name assigned to the worker task.</summary>
        const char* Name = "espnowProtocol";
        /// <summary>Worker stack size in bytes.</summary>
        uint32_t StackSize = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_STACK_SIZE;
        /// <summary>Worker scheduling priority.</summary>
        uint32_t Priority = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_PRIORITY;
        /// <summary>Processor core affinity, or -1 for platform-selected affinity.</summary>
        int32_t Core = -1;
        /// <summary>Maximum number of frames that may await asynchronous processing.</summary>
        size_t QueueDepth = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_QUEUE_DEPTH;
        /// <summary>Policy applied when the worker queue is full.</summary>
        Task::TaskQueueOverflowPolicy OverflowPolicy =
            Task::TaskQueueOverflowPolicy::Reject;
    };

private:
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using WorkItem = ESPNowReceivedFrame*;
    using Executor = Task::TaskExecutor<WorkItem>;
    using ExecutorPtr = System::Memory::UniquePtr<Executor, ExternalPreferred>;
    using FrameStorage = System::Memory::Vector<ESPNowReceivedFrame, ExternalPreferred>;
    using SlotStorage = System::Memory::Vector<uint8_t, ExternalPreferred>;

    Configuration _configuration;
    ExecutorPtr _executor;
    FrameStorage _frames;
    SlotStorage _slotInUse;
    Handler _handler;
    mutable System::Synchronization::Mutex _lifecycleMutex;
    mutable System::Synchronization::Mutex _poolMutex;
    std::atomic<uint64_t> _handoffRejected{0};
    std::atomic<bool> _initialized{false};

    WorkItem AcquireFrame(const ESPNowReceivedFrame& frame) {
        std::lock_guard<System::Synchronization::Mutex> lock(_poolMutex);
        for (std::size_t index = 0; index < _frames.size(); ++index) {
            if (_slotInUse[index] != 0) continue;
            _slotInUse[index] = 1;
            _frames[index] = frame;
            return &_frames[index];
        }
        return nullptr;
    }

    void ReleaseFrame(WorkItem frame) noexcept {
        if (frame == nullptr) return;
        std::lock_guard<System::Synchronization::Mutex> lock(_poolMutex);
        if (_frames.empty()) return;
        ESPNowReceivedFrame* const first = _frames.data();
        ESPNowReceivedFrame* const end = first + _frames.size();
        if (frame < first || frame >= end) return;
        _slotInUse[static_cast<std::size_t>(frame - first)] = 0;
    }

    void ProcessFrame(WorkItem frame) {
        if (frame == nullptr) return;
        class FrameRelease final {
        public:
            FrameRelease(ESPNowAsyncProtocolHandler& owner, WorkItem item) noexcept
                : _owner(owner), _item(item) {}
            ~FrameRelease() { _owner.ReleaseFrame(_item); }
        private:
            ESPNowAsyncProtocolHandler& _owner;
            WorkItem _item;
        } release(*this, frame);

        if (_handler) _handler(*frame);
    }

public:
    ESPNowAsyncProtocolHandler() = default;

    ESPNowAsyncProtocolHandler(const ESPNowAsyncProtocolHandler&) = delete;
    ESPNowAsyncProtocolHandler& operator=(const ESPNowAsyncProtocolHandler&) = delete;

    /// <summary>Initializes and starts the asynchronous handler using default task configuration.</summary>
    /// <param name="handler">Callback that will process received frames on the worker task.</param>
    /// <returns>True when the worker was initialized and started successfully.</returns>
    bool Initialize(Handler handler) {
        return Initialize(
            std::move(handler),
            Configuration{}
        );
    }

    /// <summary>Initializes and starts the asynchronous handler with explicit worker configuration.</summary>
    /// <param name="handler">Callback that will process received frames on the worker task.</param>
    /// <param name="configuration">Task and queue resources for the worker.</param>
    /// <returns>True when the configuration is valid and the worker starts successfully.</returns>
    /// <remarks>The bounded frame pool is materialized once in externally preferred storage. The TaskExecutor then queues pointer-sized work items rather than copying complete frames through FreeRTOS queue storage, and its discarded-item callback returns <c>DropOldest</c> evictions to the pool.</remarks>
    bool Initialize(
        Handler handler,
        Configuration configuration
    ) {
        Shutdown();
        if (!handler || configuration.StackSize == 0 || configuration.QueueDepth == 0) {
            return false;
        }
        if (configuration.QueueDepth > std::numeric_limits<size_t>::max() - 2U) {
            return false;
        }

        _configuration = configuration;
        const size_t poolSize = configuration.QueueDepth + 2U;
        try {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _frames.resize(poolSize);
            _slotInUse.assign(poolSize, 0);
            _handler = std::move(handler);
        } catch (...) {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _frames.clear();
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

        auto executor = System::Memory::MakeUnique<Executor, ExternalPreferred>(
            taskConfiguration
        );

        const auto initialized = executor->Initialize(
            [this](WorkItem const& frame) { ProcessFrame(frame); },
            [this](WorkItem const& discarded) { ReleaseFrame(discarded); }
        );

        if (initialized != Task::TaskExecutionStatus::Success) {
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _frames.clear();
            _slotInUse.clear();
            return false;
        }

        if (executor->Start() != Task::TaskExecutionStatus::Success) {
            executor->Stop();
            std::lock_guard<System::Synchronization::Mutex> poolLock(_poolMutex);
            _handler = {};
            _frames.clear();
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

    /// <summary>Stops the worker and releases its executor and externally preferred frame-pool resources.</summary>
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
        _frames.clear();
        _slotInUse.clear();
    }

    /// <summary>Reports whether the asynchronous worker is currently initialized.</summary>
    bool GetIsInitialized() const noexcept {
        return _initialized.load(std::memory_order_acquire);
    }

    /// <summary>Queues a received frame for asynchronous protocol processing.</summary>
    /// <param name="frame">Frame to hand off to the worker.</param>
    /// <returns>True when a pool slot and worker-queue entry were both acquired.</returns>
    bool Submit(const ESPNowReceivedFrame& frame) {
        std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
        if (!_initialized.load(std::memory_order_acquire) || !_executor) {
            return false;
        }

        WorkItem item = AcquireFrame(frame);
        if (item == nullptr) {
            _handoffRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto result = _executor->Submit(item);
        if (result != Task::TaskExecutionStatus::Success) {
            ReleaseFrame(item);
            _handoffRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    /// <summary>Returns task-executor queue and execution statistics for the protocol worker.</summary>
    Task::TaskExecutionStatistics GetStatistics() const {
        std::lock_guard<System::Synchronization::Mutex> lifecycle(_lifecycleMutex);
        return _executor
            ? _executor->GetStatistics()
            : Task::TaskExecutionStatistics{};
    }

    /// <summary>Returns the number of frames rejected while handing work to the asynchronous executor.</summary>
    uint64_t GetRejectedHandoffCount() const noexcept {
        return _handoffRejected.load(std::memory_order_acquire);
    }
};

}
}
