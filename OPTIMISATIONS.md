# ESPressio ESP-Now Optimisation Log

This file records the current resource-optimisation round chronologically. Version numbers are intentionally unchanged during this round.

Rollback anchor: `rollback/optimisations-pre-20260825` -> `bcfa442ff31f8394563e6e9570e27ab5d75e8732`.

## 2026-08-25 — Existing coexistence work enters optimisation round (#39 / #42 / #43)

### Context
Hardware validation of WiFi + ESP-NOW + Event on an M5StickC Plus2 exposed both radio-lifecycle defects and severe internal-RAM pressure. The radio lifecycle work remains on `bugfix/39-wifi-coexistence` so the current working branch is retained rather than forked.

### Changes already present after the rollback anchor
- #42: failed clock-synchronization sends now respect the configured synchronization interval instead of retrying every worker iteration.
- #43: default receive worker stack reduced from 8192 to 4096 bytes.
- #43: default receive queue reduced from 12 to 6 frames.
- Existing stack high-water telemetry remains available and both resource values remain explicitly configurable.

### Dependency pin
`library.json` now resolves ESPressio Threads directly from `optimisation/69-resource-footprint` so ESP-NOW integration testing exercises the active Threads implementation.

### Safety / rollback
The ESP-Now version remains 0.8.3. Resource reductions remain configurable; the rollback branch above identifies the pre-optimisation working head. Coexistence, peer binding and retry behavior continue to be tracked by their existing issues.
