# ESPressio ESP-Now Optimisation Log

This file records the current resource-optimisation/coexistence round chronologically. Version numbers are intentionally unchanged during this round.

Rollback anchor: `rollback/optimisations-pre-20260825` -> `bcfa442ff31f8394563e6e9570e27ab5d75e8732`.

## 2026-08-25 — Existing coexistence work enters optimisation round (#39 / #42 / #43)

Hardware validation of WiFi + ESP-NOW + Event on constrained classic ESP32 hardware exposed both radio-lifecycle defects and severe internal-RAM pressure. Work remains on `bugfix/39-wifi-coexistence` rather than forking the active coexistence branch.

- #42: failed clock-synchronization sends respect the configured synchronization interval instead of retrying every worker iteration.
- #43: receive queue reduced from 12 to 6 frames.
- The initial receive-worker stack reduction from 8192 to 4096 bytes was retained only long enough for hardware measurement.
- `library.json` resolves ESPressio Threads directly from `optimisation/69-resource-footprint` during this round.

## 2026-08-25 — Restore ESP-NOW receive-worker stack safety margin (#43)

Concurrent encrypted Command/Event traffic disproved 4096 bytes as a safe worker stack. Hardware high-water telemetry measured only ~80-132 bytes remaining on StickA and ~124 bytes on StickB immediately before stack-canary failures; ELF/MAP analysis tied both failures to the ESP-NOW TransportWorker.

Changes:
- default `ReceiveTaskStackSize`: 4096 -> 6144 bytes;
- retain `ReceiveQueueLength = 6` because no evidence implicated the queue reduction;
- retain explicit configurability and high-water telemetry.

Commits:
- `08f5a40` — `fix(#43): restore ESP-NOW worker stack safety margin`
- `4ca8095` — `test(#43): cover corrected ESP-NOW worker stack default`

## 2026-08-25 — Boot-order / peer-lifecycle validation (#44)

Forced AP/channel-1 Lab testing materially improved StickB-to-StickA Command ingress but did not restore complete reliability. Logs also suggested initialization-order/peer-lifecycle asymmetry. The Lab therefore added first-discovery diagnostics covering application-ready state, role, logical binding, native peer interface/channel and EventTransport destination state.

The public meaning of `ESPNowPeerConfig::Channel == 0` remains “follow current radio”; it was deliberately not redefined as channel 1.

## 2026-08-25 — Reduce peak live stack and transient heap (#45)

The 6144-byte stack is a safety baseline, not a substitute for reducing peak live memory. Source audit identified avoidable overlapping temporaries in synchronous receive/decrypt/Command/reply paths.

Implemented:
- one-fragment-at-a-time secure encoding instead of materializing all encrypted fragments;
- narrower secure receive scopes so decoded fragment/encrypted-envelope storage is destroyed before application callbacks;
- explicit reassembly storage release at lifecycle boundaries while retaining normal steady-state capacity reuse;
- streaming Command fragmentation;
- move decoded `CommandInvocation` into invocation context instead of copying;
- move encoded responses directly into duplicate-response cache while preserving cache-before-send semantics.

Regression coverage verifies byte-for-byte secure fragment equivalence and existing multi-fragment Command behaviors.

Commits:
- `5222671` — `perf(#45): stream secure fragment encoding`
- `bf20f54` — `perf(#45): shorten secure transport temporary lifetimes`
- `ae6d8d7` — `perf(#45): support streaming command fragments`
- `978e42f` — `perf(#45): reduce command transport transient copies`
- `3c2bfec` — `fix(#45): include algorithm for streamed secure fragments`
- `8dfb06a` — `test(#45): cover streamed secure fragment encoding`

Deferred higher-risk work remains: stack-resident receive-frame duplication, the stack-resident native TX frame, maintenance-handler snapshot storage, and moving application execution to another task.

## 2026-08-25 — Transactional WiFi radio-transition ownership (#44 / WiFi #22)

Hardware evidence showed that explicit AP mode alone did not reproduce the highly reliable ESP-NOW-only control. AP→Client produced `ESP_ERR_ESPNOW_IF`, while Client→AP could crash inside ESP-IDF SoftAP startup with ESP-NOW still attached.

`ESPNowWiFiCoordinator` now treats WiFi pre/post radio notifications as a transaction boundary:
- mark ESP-NOW unavailable before disruptive radio changes;
- detach native `esp_now` before WiFi changes AP/STA/APSTA/interface state;
- preserve logical ESPressio worker/protocol/peer/security state;
- force native ESP-NOW reinitialization and managed-peer reconciliation after the resulting WiFi state is stable;
- avoid intermediate reconciliation while transition-suspended.

Commit:
- `513a03b` — `fix(#44): suspend native ESP-NOW during WiFi radio transitions`

## 2026-08-25 — Runtime ESP-NOW diagnostic/control Commands (#46)

### Context
Coexistence debugging requires changing operating conditions repeatedly on physical devices. Rebuilding/reflashing for every channel/interface experiment is slow and makes comparisons harder to control.

### Changes
- Added optional `ESPressio_ESPNowCommandHandler.hpp`; it remains outside the normal umbrella so ESPressio Command does not become a mandatory core dependency.
- Added `espnow status` with logical binding, native WiFi mode/channel, worker interval/high-water data and last native send result.
- Added `espnow config` exposing initialization settings while explicitly labelling worker stack, queue, priority/core, `InitializeWiFi` and worker interval as initialization-only.
- Added `espnow channel <1-14>`. The handler delegates the physical channel change to an application-supplied radio-authority callback, reads back the actual channel, then reconciles ESP-NOW peers to it. This prevents a misleading peer-only channel mutation.
- Added `espnow binding status`, `binding interface <auto|sta|ap>`, `binding channel <0-14>` and `binding available <bool>` for controlled logical-binding experiments.
- Added `espnow reconcile` to force native ESP-NOW reinitialization and reconstruction of managed peers from logical state.
- Channel changes intentionally do not force a second teardown after the radio authority returns; WiFi-owned applications may already have completed a transactional suspend/rebuild through the coordinator.
- Added `docs/RUNTIME_COMMANDS.md` with ownership rules and usage guidance.

### Safety / rollback
The command handler never directly assumes WiFi ownership. ESPressio WiFi applications must route physical channel changes through `WiFiManager`; standalone applications may deliberately use `esp_wifi_set_channel`. Initialization-only task resources are read-only rather than falsely exposed as live mutable settings. No version numbers changed and the existing rollback branch remains valid.

### Commits
- `760f47c` — `feat(#46): add runtime ESP-NOW command handler`
- `ad15d29` — `fix(#46): keep channel changes within radio authority lifecycle`
- `e56fb13` — `docs(#46): document ESP-NOW runtime command surface`

## 2026-08-25 — Re-right-size worker after measured #45 improvement (#43 / #45)

### Hardware evidence
The first 4096-byte worker configuration was unsafe before #45: encrypted bidirectional traffic drove minimum-free stack to roughly 80-124 bytes and caused stack-canary failures. After #45 shortened transient lifetimes and streamed fragment work, the current full Lab build has again been operating with a 4096-byte worker and reports approximately 1920 bytes minimum-free on StickA and 2540 bytes on StickB in `espnow status` during coexistence testing. Periodic high-water telemetry in the same run remained around 2000 bytes on StickA before the later WiFi failure.

The subsequent StickA crashes were resolved with the exact ELF/MAP to `ieee80211_hostap_attach` / `wifi_softap_start`; one explicitly failed `esp_timer_create()` with `ESP_ERR_NO_MEM`. They were therefore native WiFi internal-heap failures, not ESP-NOW worker stack exhaustion.

### Change
`ESPNowTransportConfig::ReceiveTaskStackSize` returns from 6144 to 4096 bytes, retaining the 6-frame receive queue and high-water telemetry.

### Safety rationale
This is not a repeat of the earlier speculative 8192 -> 4096 cut. It follows the #45 peak-live-memory changes and uses measured post-change hardware headroom of roughly 1.9-2.5 KB on the 4096-byte reservation. The value remains explicitly configurable for heavier applications.

### Expected internal-RAM recovery
Approximately 2048 bytes of permanent FreeRTOS worker-stack reservation per transport instance relative to the temporary 6144-byte safety baseline.

Commit: `e6cd7cd835244be2d3fe1c6051f1c3bec0ceec4f` — `perf(#43,#45): right-size worker stack after measured peak reduction`.
