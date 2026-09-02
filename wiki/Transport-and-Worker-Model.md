# Transport and Worker Model

ESP-NOW receive processing is deliberately split between the native ESP-IDF callback and the managed ESPressio worker.

## Native callback responsibility

The native receive callback should do only bounded, transport-safe work: validate the minimum framing required to copy the inbound frame and enqueue that bounded work for later processing.

It must not perform general protocol handling, reassembly maintenance, Command invocation or other unbounded application work directly in the receive callback context.

## Managed worker

The ESPressio worker performs:

- ESPressio frame validation;
- protocol dispatch;
- reassembly/timeout maintenance;
- registered maintenance handlers;
- Command transport maintenance where selected.

This keeps protocol state mutations in one managed execution context instead of splitting them between the native receive task and Arduino `loop()`.

## Rate limiting

`WorkerIterationIntervalMilliseconds` is the minimum iteration interval. Incoming frames remain queued until the next permitted iteration.

This makes the worker's CPU demand explicit and prevents receive traffic from bypassing the selected execution budget.

## Queue capacity

The receive queue is bounded. Size it for expected burst traffic and worker cadence; queue exhaustion is a resource condition to observe and test, not a reason to introduce unbounded buffering.

## Lifecycle ownership

ESP-Now uses ESPressio Threads for its long-lived worker. Do not create a second native receive worker or direct FreeRTOS task ownership inside new protocol integrations when the transport worker already supplies the required serialized execution context.