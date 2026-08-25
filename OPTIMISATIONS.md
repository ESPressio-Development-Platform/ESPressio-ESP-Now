# ESPressio ESP-Now Optimisation Log

This file records the current resource-optimisation round chronologically. Version numbers are intentionally unchanged during this round.

Rollback anchor: `rollback/optimisations-pre-20260825` -> `bcfa442ff31f8394563e6e9570e27ab5d75e8732`.

## 2026-08-25 — Existing coexistence work enters optimisation round (#39 / #42 / #43)

### Context
Hardware validation of WiFi + ESP-NOW + Event on an M5StickC Plus2 exposed both radio-lifecycle defects and severe internal-RAM pressure. The radio lifecycle work remains on `bugfix/39-wifi-coexistence` so the current working branch is retained rather than forked.

### Changes already present after the rollback anchor
- #42: failed clock-synchronization sends now respect the configured synchronization interval instead of retrying every worker iteration.
- #43: default receive worker stack reduced from 8192 to 4096 bytes for initial constrained-device validation.
- #43: default receive queue reduced from 12 to 6 frames.
- Existing stack high-water telemetry remains available and both resource values remain explicitly configurable.

### Dependency pin
`library.json` resolves ESPressio Threads directly from `optimisation/69-resource-footprint` so ESP-NOW integration testing exercises the active Threads implementation.

### Safety / rollback
The ESP-Now version remains 0.8.3. Resource reductions remain configurable; the rollback branch above identifies the pre-optimisation working head. Coexistence, peer binding and retry behavior continue to be tracked by their existing issues.

## 2026-08-25 — Restore ESP-NOW receive-worker stack safety margin (#43)

### Hardware evidence
Concurrent encrypted Command/Event traffic on both M5StickC Plus2 and original M5StickC hardware disproved the 4096-byte receive-worker stack as a safe default. The Lab's FreeRTOS high-water instrumentation measured only ~80-132 bytes remaining on StickA and ~124 bytes on StickB before stack-canary failures. ELF/MAP analysis tied both crashes to the ESP-NOW TransportWorker task.

### Change
- Increase the default `ESPNowTransportConfig::ReceiveTaskStackSize` from 4096 to 6144 bytes.
- Retain `ReceiveQueueLength = 6`; no evidence implicated the queue reduction.
- Retain configurability so applications can explicitly request a different stack after workload-specific measurement.
- Update core-type regression coverage to assert the corrected default and continued override support.

### Rationale
6144 bytes recovers 2048 bytes of safety margin relative to the failing 4096-byte configuration while preserving 2048 bytes of the original 8192-byte stack optimisation. This is a measured compromise rather than a speculative reduction.

### Commits
- `08f5a40` — `fix(#43): restore ESP-NOW worker stack safety margin`
- `4ca8095` — `test(#43): cover corrected ESP-NOW worker stack default`

## 2026-08-25 — Boot-order / peer-lifecycle validation continues (#44)

### Context
Forced AP/channel-1 Lab validation materially improved StickB-to-StickA Command ingress, but Command responses and some Events remained unreliable. Concurrent logs also suggest that the device already initialized when its peer joins tends to establish healthier application transport state than the device discovering an already-present peer during its own startup.

### Validation strategy
Do not redefine the public meaning of `ESPNowPeerConfig::Channel == 0` yet. Keep #44 focused on proving the lifecycle hypothesis with explicit AP/channel-1 peer materialization and expanded Lab diagnostics that capture discovery trigger, native peer interface/channel, radio binding, EventTransport destination state, role, and startup-ready state.

A generalized Auto/channel-0 transport change remains deferred until hardware tests isolate whether the remaining asymmetry is native peer programming, role election, or application peer-registration ordering.
