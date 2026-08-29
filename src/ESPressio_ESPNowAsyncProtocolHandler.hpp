#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <ESPressio_Memory.hpp>
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
/// The supplied handler executes asynchronously using the configured task and queue resources. Frames that cannot be handed off are counted as rejected handoffs. Executor object storage and queue backing prefer external memory; the underlying task stack remains on the platform-safe execution path.
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
    using Executor = Task::TaskExecutor<ESPNowReceivedFrame>;
    using ExecutorPtr = System::Memory::UniquePtr<Executor, ExternalPreferred>;

    Configuration _configuration;
    ExecutorPtr _executor;
    std::atomic<uint64_t> _handoffRejected{0};
    bool _initialized = false;

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
    /// <remarks>The caller's handler is moved directly into the TaskExecutor. No intermediate capturing lambda or second type-erased callable is materialized.</remarks>
    bool Initialize(
        Handler handler,
        Configuration configuration
    ) {
        Shutdown();
        if (!handler || configuration.StackSize == 0 || configuration.QueueDepth == 0) {
            return false;
        }

        _configuration = configuration;

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

        const auto initialized = executor->Initialize(std::move(handler));

        if (initialized != Task::TaskExecutionStatus::Success) {
            return false;
        }

        if (executor->Start() != Task::TaskExecutionStatus::Success) {
            executor->Stop();
            return false;
        }

        _executor = std::move(executor);
        _handoffRejected.store(0, std::memory_order_release);
        _initialized = true;
        return true;
    }

    /// <summary>Stops the worker and releases its executor resources.</summary>
    void Shutdown() {
        _initialized = false;
        if (_executor) {
            _executor->Stop();
            _executor.reset();
        }
    }

    /// <summary>Reports whether the asynchronous worker is currently initialized.</summary>
    bool GetIsInitialized() const noexcept {
        return _initialized;
    }

    /// <summary>Queues a received frame for asynchronous protocol processing.</summary>
    /// <param name="frame">Frame to hand off to the worker.</param>
    /// <returns>True when the frame was accepted by the worker queue.</returns>
    bool Submit(const ESPNowReceivedFrame& frame) {
        if (!_initialized || !_executor) {
            return false;
        }

        const auto result = _executor->Submit(frame);
        if (result != Task::TaskExecutionStatus::Success) {
            _handoffRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    /// <summary>Returns task-executor queue and execution statistics for the protocol worker.</summary>
    Task::TaskExecutionStatistics GetStatistics() const {
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
