#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

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

class ESPNowAsyncProtocolHandler {
public:
    using Handler = std::function<void(const ESPNowReceivedFrame&)>;

    struct Configuration {
        const char* Name = "espnowProtocol";
        uint32_t StackSize = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_STACK_SIZE;
        uint32_t Priority = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_PRIORITY;
        int32_t Core = -1;
        size_t QueueDepth = ESPRESSIO_ESPNOW_ASYNC_PROTOCOL_QUEUE_DEPTH;
        Task::TaskQueueOverflowPolicy OverflowPolicy =
            Task::TaskQueueOverflowPolicy::Reject;
    };

private:
    Configuration _configuration;
    std::unique_ptr<Task::TaskExecutor<ESPNowReceivedFrame>> _executor;
    std::atomic<uint64_t> _handoffRejected{0};
    bool _initialized = false;

public:
    ESPNowAsyncProtocolHandler() = default;

    ESPNowAsyncProtocolHandler(const ESPNowAsyncProtocolHandler&) = delete;
    ESPNowAsyncProtocolHandler& operator=(const ESPNowAsyncProtocolHandler&) = delete;

    bool Initialize(
        Handler handler,
        Configuration configuration = {}
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

        auto executor =
            std::make_unique<Task::TaskExecutor<ESPNowReceivedFrame>>(
                taskConfiguration
            );

        const auto initialized = executor->Initialize(
            [handler = std::move(handler)](const ESPNowReceivedFrame& frame) {
                handler(frame);
            }
        );

        if (initialized != Task::TaskExecutionStatus::Success) {
            return false;
        }

        if (executor->Start() != Task::TaskExecutionStatus::Success) {
            return false;
        }

        _executor = std::move(executor);
        _handoffRejected.store(0, std::memory_order_release);
        _initialized = true;
        return true;
    }

    void Shutdown() {
        _initialized = false;
        if (_executor) {
            _executor->Stop();
            _executor.reset();
        }
    }

    bool GetIsInitialized() const noexcept {
        return _initialized;
    }

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

    Task::TaskExecutionStatistics GetStatistics() const {
        return _executor
            ? _executor->GetStatistics()
            : Task::TaskExecutionStatistics{};
    }

    uint64_t GetRejectedHandoffCount() const noexcept {
        return _handoffRejected.load(std::memory_order_acquire);
    }
};

}
}
