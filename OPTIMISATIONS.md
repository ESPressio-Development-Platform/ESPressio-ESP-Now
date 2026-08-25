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

## 2026-08-25 — Reduce peak live stack and transient heap in synchronous worker paths (#45)

### Context
Increasing the worker stack to 6144 bytes restores immediate safety margin, but source inspection showed avoidable overlapping lifetime in the synchronous receive/decrypt/Command/reply path. The objective is to reduce the amount of memory simultaneously live inside a worker iteration rather than treating a larger task stack as the final solution.

### Changes
- Added `ESPNowSecurityProtocol::EncodeFragmentPayload()` so callers can encode one secure fragment directly from a payload span without first creating a heap-owned `Fragment::Data` copy.
- Reworked secure transmit to reuse one encoded fragment buffer instead of materializing a `vector<vector<uint8_t>>` containing the complete secured envelope a second time.
- Narrowed secure receive temporary scope so decoded fragment and encrypted-envelope storage are destroyed before application Command/Event callbacks execute.
- Added `ReassemblyState::ReleaseStorage()` for explicit lifecycle-boundary release of retained fragment vector capacity; normal `Reset()` intentionally retains bounded capacity to avoid steady-state allocator churn.
- Added streaming Command fragmentation through `GetFragmentCount()` + `BuildFragment()`; the Command endpoint now reuses one fragment buffer instead of constructing every protocol fragment before sending.
- Moved decoded `CommandInvocation` ownership into the invocation context instead of duplicating it before execution.
- Moved encoded response ownership directly into the duplicate-response cache and sends from that stable cache entry, preserving the existing cache-before-send duplicate semantics without an additional payload copy.
- Kept the 6144-byte worker stack unchanged during this tranche so subsequent hardware high-water measurements can quantify the actual peak reduction safely.

### Regression coverage
- Existing Command endpoint tests exercise multi-fragment request/response behavior through the new streaming `SendPayload()` path, including reverse-order reassembly, duplicate handling, timeout behavior, policy callbacks and long payloads.
- Security protocol tests compare the new one-fragment streaming encoder byte-for-byte against the retained materialized-fragment compatibility helper.
- Security tests verify explicit reassembly storage release.

### Deferred higher-risk work
The transport still has avoidable stack-resident frame duplication (`CallbackFrame` plus `ESPNowReceivedFrame`), a stack-resident 250-byte send frame, and a stack-resident maintenance-handler snapshot. These remain candidates for a subsequent #45 tranche, but the received-frame ownership model and callback lifetimes require a more careful change than the secure/Command lifetime reductions above. Deferring application execution onto another task also remains out of scope because it would change scheduling semantics.

### Commits
- `5222671` — `perf(#45): stream secure fragment encoding`
- `bf20f54` — `perf(#45): shorten secure transport temporary lifetimes`
- `ae6d8d7` — `perf(#45): support streaming command fragments`
- `978e42f` — `perf(#45): reduce command transport transient copies`
- `3c2bfec` — `fix(#45): include algorithm for streamed secure fragments`
- `8dfb06a` — `test(#45): cover streamed secure fragment encoding`

## 2026-08-25 — Transactional WiFi radio-transition ownership (#44 / WiFi #22)

### Hardware evidence
The ESP-NOW-only Lab control remains highly reliable when the radio is established once and then left stable. With ESPressio WiFi present, explicit AP mode still showed native ESP-NOW `NO_MEM` send rejection, AP→Client left native peers incompatible with the resulting STA interface (`ESP_ERR_ESPNOW_IF`), and Client→AP crashed inside ESP-IDF SoftAP startup while ESP-NOW remained attached to the WiFi driver.

### Change
- `ESPNowWiFiCoordinator` now treats WiFi's pre/post radio callbacks as a transaction boundary rather than merely an availability hint.
- On transition beginning, new ESP-NOW traffic is marked unavailable and the native ESP-NOW attachment is deinitialized before WiFi changes mode/interface state.
- Logical ESPressio state remains intact: worker, protocol handlers, receive queue configuration, managed-peer records and higher-level Command/Event/Security objects are not shut down.
- On transition completion, the coordinator forces native ESP-NOW reinitialization and managed-peer reconciliation against the resulting WiFi interface/channel before making the binding available again.
- Intermediate state-change/scan callbacks do not attempt reconciliation while the native attachment is transition-suspended.

### Rationale
The earlier coordinator left `esp_now` and its native peer table attached while `WiFi.mode()` could tear down or recreate AP/STA interface structures. The new boundary gives WiFi exclusive ownership of disruptive driver changes, then reconstructs ESP-NOW from logical state afterward. This directly targets stale-interface errors and the observed native Client→AP transition crash without introducing a full ESPressio transport shutdown/re-registration cycle.

### Safety / rollback
The repository rollback branch remains the pre-optimisation recovery point. The change is isolated to the optional WiFi coordinator path; standalone ESP-NOW behavior is unchanged. No version number changes are made.

### Commits
- `513a03b` — `fix(#44): suspend native ESP-NOW during WiFi radio transitions`
