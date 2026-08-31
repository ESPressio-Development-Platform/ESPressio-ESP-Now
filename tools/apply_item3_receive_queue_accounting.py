from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
transport_path = ROOT / "src/ESPressio_ESPNowTransport.hpp"
workflow_path = ROOT / ".github/workflows/wifi-coexistence-tests.yml"

transport = transport_path.read_text(encoding="utf-8")

old = """    std::atomic<ESPNowSendFailure> _lastSendFailure{ESPNowSendFailure::None};\n    std::atomic<int32_t> _lastSendNativeError{0};\n"""
new = """    std::atomic<ESPNowSendFailure> _lastSendFailure{ESPNowSendFailure::None};\n    std::atomic<int32_t> _lastSendNativeError{0};\n    std::atomic<uint64_t> _receiveQueueRejectedCount{0};\n"""
if new not in transport:
    if old not in transport:
        raise SystemExit("transport counter insertion guard failed")
    transport = transport.replace(old, new, 1)

old = """        frame.HasLocalInterface = ResolveLocalInterface(destination, frame.LocalInterface);\n        (void)self->_receiveQueue->Send(&frame, 0);\n"""
new = """        frame.HasLocalInterface = ResolveLocalInterface(destination, frame.LocalInterface);\n        if (!static_cast<bool>(self->_receiveQueue->Send(&frame, 0))) {\n            self->_receiveQueueRejectedCount.fetch_add(1, std::memory_order_relaxed);\n        }\n"""
if new not in transport:
    if old not in transport:
        raise SystemExit("receive callback send guard failed")
    transport = transport.replace(old, new, 1)

old = """        _lastSendFailure.store(ESPNowSendFailure::None);\n        _lastSendNativeError.store(0);\n"""
new = """        _lastSendFailure.store(ESPNowSendFailure::None);\n        _lastSendNativeError.store(0);\n        _receiveQueueRejectedCount.store(0, std::memory_order_relaxed);\n"""
if new not in transport:
    if old not in transport:
        raise SystemExit("receive rejection reset guard failed")
    transport = transport.replace(old, new, 1)

old = """    bool GetIsInitialized() const { return _initialized.load(std::memory_order_acquire); }\n    uint32_t GetReceiveTaskMinimumFreeStackBytes() const { return _worker ? _worker->MinimumFreeStackBytes() : 0; }\n    uint32_t GetWorkerIterationIntervalMilliseconds() const noexcept { return _config.WorkerIterationIntervalMilliseconds; }\n"""
new = """    bool GetIsInitialized() const { return _initialized.load(std::memory_order_acquire); }\n    uint32_t GetReceiveTaskMinimumFreeStackBytes() const { return _worker ? _worker->MinimumFreeStackBytes() : 0; }\n    uint32_t GetWorkerIterationIntervalMilliseconds() const noexcept { return _config.WorkerIterationIntervalMilliseconds; }\n\n    /// <summary>Returns the number of valid native ESP-NOW frames dropped because the bounded receive queue could not accept them.</summary>\n    /// <remarks>The native receive callback remains nonblocking; this counter is incremented atomically without logging, locking, allocation, or observer dispatch.</remarks>\n    uint64_t GetReceiveQueueRejectedCount() const noexcept {\n        return _receiveQueueRejectedCount.load(std::memory_order_relaxed);\n    }\n"""
if new not in transport:
    if old not in transport:
        raise SystemExit("receive rejection getter guard failed")
    transport = transport.replace(old, new, 1)

transport_path.write_text(transport, encoding="utf-8")

workflow = workflow_path.read_text(encoding="utf-8")
old = """              (void)transport.GetLastSendResult();\n"""
new = """              (void)transport.GetLastSendResult();\n              (void)transport.GetReceiveQueueRejectedCount();\n"""
if new not in workflow:
    if old not in workflow:
        raise SystemExit("compile-surface getter guard failed")
    workflow = workflow.replace(old, new, 1)

old = """          grep -q 'PrecisionThread' src/ESPressio_ESPNowTransport.hpp\n          if grep -q 'xTaskCreatePinnedToCore' src/ESPressio_ESPNowTransport.hpp; then\n"""
new = """          grep -q 'PrecisionThread' src/ESPressio_ESPNowTransport.hpp\n          grep -q 'GetReceiveQueueRejectedCount' src/ESPressio_ESPNowTransport.hpp\n          grep -q '_receiveQueueRejectedCount.fetch_add(1, std::memory_order_relaxed)' src/ESPressio_ESPNowTransport.hpp\n          if grep -q '(void)self->_receiveQueue->Send(&frame, 0)' src/ESPressio_ESPNowTransport.hpp; then\n            echo 'Native ESP-NOW receive callback must account for rejected bounded-queue submissions.' >&2\n            exit 1\n          fi\n          if grep -q 'xTaskCreatePinnedToCore' src/ESPressio_ESPNowTransport.hpp; then\n"""
if new not in workflow:
    if old not in workflow:
        raise SystemExit("source guard insertion failed")
    workflow = workflow.replace(old, new, 1)

workflow_path.write_text(workflow, encoding="utf-8")
