# Memory and Performance

ESP-NOW operates in one of the most memory-sensitive parts of an ESP32 application: native WiFi/ESP-NOW facilities require internal-capable memory while ESPressio protocol state competes with the rest of the application for heap and worker stack.

## Bounded receive queue

The native callback copies inbound frames into a fixed-length queue. Choose `ReceiveQueueLength` for expected burst traffic and worker cadence; do not replace this with unbounded buffering.

## Worker stack

The worker stack must cover protocol validation, handlers, reassembly and selected integrations. Use the transport's minimum-free-stack diagnostics during representative hardware workloads before reducing stack reservations.

## Protocol state

Keep reassembly, peer registries and pending work bounded. Optional integrations should not introduce open-ended collections just because higher-level protocols may generate bursts.

## Internal-memory pressure

Native ESP-NOW/WiFi driver allocations have capability requirements and must remain where ESP-IDF expects them. ESPressio-owned variable-size data may use System memory policy where appropriate, but memory optimisation must never move driver-owned/ISR/DMA structures blindly to PSRAM.

## Shared-radio work

WiFi transitions and scans are explicit suspension windows rather than opportunities to accumulate unlimited outbound work. Callers should handle `RadioUnavailable` and retry according to their own protocol semantics.

## Profiling

On real target hardware observe:

- free/largest internal heap;
- external heap where present;
- worker stack high-water mark;
- receive queue peak/overflow behaviour;
- native send result failures, especially `NoMemory`;
- reassembly/pending protocol capacity;
- effects of simultaneous WiFi scans/connect/disconnect transitions.

Optimisations are valid only when peer reconciliation, transport lifecycle and native callback safety remain intact.