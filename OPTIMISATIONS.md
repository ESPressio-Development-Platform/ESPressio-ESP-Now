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
The first 4096-byte worker configuration was unsafe before #45: encrypted bidirectional traffic drove minimum-free stack to roughly 80-124 bytes and caused stack-canary failures. After #45 shortened transient lifetimes and streamed fragment work, the current full Lab build has again been operating with a 4096-byte worker and reports approximately 1920 bytes minimum-free on StickA and 2540 bytes minimum-free on StickB in `espnow status` during coexistence testing. Periodic high-water telemetry in the same run remained around 2000 bytes on StickA before the later WiFi failure.

The subsequent StickA crashes were resolved with the exact ELF/MAP to `ieee80211_hostap_attach` / `wifi_softap_start`; one explicitly failed `esp_timer_create()` with `ESP_ERR_NO_MEM`. They were therefore native WiFi internal-heap failures, not ESP-NOW worker stack exhaustion.

### Change
`ESPNowTransportConfig::ReceiveTaskStackSize` returns from 6144 to 4096 bytes, retaining the 6-frame receive queue and high-water telemetry.

### Safety rationale
This is not a repeat of the earlier speculative 8192 -> 4096 cut. It follows the #45 peak-live-memory changes and uses measured post-change hardware headroom of roughly 1.9-2.5 KB on the 4096-byte reservation. The value remains explicitly configurable for heavier applications.

### Expected internal-RAM recovery
Approximately 2048 bytes of permanent FreeRTOS worker-stack reservation per transport instance relative to the temporary 6144-byte safety baseline.

Commit: `e6cd7cd835244be2d3fe1c6051f1c3bec0ceec4f` — `perf(#43,#45): right-size worker stack after measured peak reduction`.

## 2026-08-25 — Asynchronous application handoff (#47)

### Hardware evidence
Encrypted Command/Event hardware traces showed that the transport worker could still overflow even after #45 because validated inbound frames synchronously nested application work on top of the ESP-NOW transport call stack. A secure Command could remain inside receive/reassembly/decrypt while Command execution, response construction, secure transmit, observer notification and Event/Timing work accumulated beneath it.

### Architecture
- Added `ESPNowAsyncProtocolHandler`, backed by a bounded persistent ESPressio `TaskExecutor`, as the explicit application-dispatch boundary.
- ESP-NOW protocol handlers now copy/submit validated application frames to the bounded executor and return to `TransportWorker` immediately.
- Event packet reassembly and `IEventTransportReceiver` delivery run on the Event protocol executor rather than on `TransportWorker`.
- Command protocol reassembly runs on the Command protocol executor; decoded inbound Commands are converted to transport-neutral `CommandRequestEnvelope` values and queued through ESPressio Event.
- Command execution occurs later on `CommandEventExecutor`; completion is correlated by request ID and returned through a lifetime-safe registered ESP-NOW `ICommandResponseRoute`.
- Duplicate in-flight Command requests preserve request identity and are not enqueued twice; completed duplicates are served from the bounded duplicate-result cache.
- Command endpoint access remains synchronized for application-originated `Invoke()` and compatibility `Update()` calls.
- The application executor exposes execution statistics and rejected-handoff counters for hardware validation.
- `TransportWorker` remains at 4096 bytes for the first post-handoff hardware run; its high-water telemetry will determine whether a later reduction is safe.

### Regression / CI
- Added host regression coverage proving that inbound Command receive/reassembly does not execute the application Command synchronously.
- Covered in-flight duplicate suppression, bounded inbound saturation, asynchronous completion correlation, cached duplicate completion and deterministic rejected-handoff errors.
- Host and ESP32 integration CI now resolve the coordinated Task, Command, Event, Threads and WiFi working branches and compile the new executor/response-route surface.
- No version number changes during this development round.

### Commits
- `1d64f4d` — `feat(#47): hand off ESP-NOW Event processing to bounded TaskExecutor`
- `af6c4ea` — `docs(#47): correct transport-worker stack rationale for async handoff`
- `8bf27d3` / `6ce28e1` — working-branch dependency wiring for Task/Event/Command
- `21f4c97` — `test(#47): prove inbound Command handoff is asynchronous and bounded`
- `23e94b7` — `test(#47): add asynchronous Command handoff regression target`
- `d3a97cf` — `test(#47): validate async handoff against coordinated working branches`

## 2026-08-27 — System-backed ESP-NOW bookkeeping and reconciliation (#49)

Phase 8 of the coordinated memory-policy programme externalises ESPressio-owned bookkeeping while deliberately leaving native WiFi/ESP-NOW and RTOS callback infrastructure internal.

### Changes
- protocol-handler, maintenance-handler, peer-interface-hint and managed-peer backing storage now uses ESPressio-System `ExternalPreferred` memory;
- registered protocol and maintenance callbacks are held in externally allocated stable shared objects, so dispatch snapshots only a `shared_ptr` instead of copying the stored `std::function` on every cycle;
- managed-peer reconciliation scratch moved off the transport worker stack into external-preferred storage;
- the State adapter's peer, pending-State-retry and pending-subscription tables moved to external-preferred storage;
- State adapter peer iteration scratch moved off the worker/caller stack;
- the upstream State subscriber registry now provides externally backed subscriber bookkeeping as part of State #10;
- `library.json` resolves ESPressio-System from `feature/1-system-memory-policy` during coordinated validation.

### Deliberately unchanged
The FreeRTOS receive queue, callback frame copied through that queue, native ESP-NOW/WiFi driver allocations and the immediate native TX frame remain internal because they participate directly in driver/callback/RTOS execution.

### Commits
- `3bcd5fa` — State-adapter bookkeeping migration
- `dffb694` — transport registry/callback/reconciliation migration
- `99fdde9` — working-branch System dependency metadata

## 2026-08-27 — Phase 11 Command endpoint ownership audit (#50)

The suite-wide ownership pass classified each Command endpoint copy by lifetime rather than mechanically applying `std::move`.

### Removed copies
- outbound invocation encoding now accepts `const CommandInvocation&` directly, eliminating the temporary `Request` wrapper's deep invocation copy;
- response encoding accepts `const CommandResult&` directly, eliminating the temporary response-result copy;
- `CompleteInbound()` moves the invocation context from the inbound record immediately before that record is erased;
- encoded responses move into the duplicate-result cache, and transmission uses the newly cached payload, eliminating the second full encoded-response copy while preserving cache-before-send behavior.

### Deliberately retained
The async inbound handoff retains one context copy: the authoritative request record must exist in `_inbound` before invoking a potentially re-entrant handler, while that handler receives the stable local context for the duration of the call. Moving either object would change that lifetime guarantee.

### Commits
- `91cacfb` — `perf(#50): encode Command payloads without wrapper copies`
- `7f59111` — `perf(#50): move retired Command endpoint payload ownership`

## 2026-08-27 — AP station topology reconciliation (#44)

### Hardware evidence
A paired long-running Lab soak isolated a persistent one-way ESP-NOW failure after a phone associated with StickA's access point. StickA continued receiving StickB State and inbound Commands, and continued executing those Commands successfully, while StickA's outbound native sends began failing across State, Command, Timing and eventually discovery traffic. StickB continued transmitting successfully for a substantial period and eventually expired StickA because no valid traffic returned. Disconnecting the phone did not recover StickA's outbound path.

### Root integration gap
The existing coordinator already treats actual WiFi radio transitions transactionally, but AP station association/disassociation does not change `WiFiRadioState` mode/channel facts and therefore does not produce that transition boundary. ESPressio-WiFi exposes AP station topology separately through `IWiFiObserver`.

### Change
- `ESPNowWiFiCoordinator` now observes both `IWiFiRadioObserver` and `IWiFiObserver`.
- `OnAccessPointStationConnected` and `OnAccessPointStationDisconnected` force the existing controlled native ESP-NOW reinitialization path and replay all retained managed peers against the current authoritative WiFi radio state.
- Logical peers, encryption keys, protocol handlers, worker state and higher-level transport state are preserved.
- No send-retry shim, peer-table duplicate or protocol-specific recovery path was added.
- The ESP32 coexistence compile guards both observer relationships and explicitly compiles both topology callbacks.

### Commits
- `e1c3801` — `fix(espnow): reconcile native peers on AP station topology changes (#44)`
- `b95bc07` — `test(espnow): guard AP station topology reconciliation (#44)`
- `fe0436e` — `docs(espnow): preserve abstraction audit history for #44`

### Validation status
Issue #44 remains open pending hardware validation of sustained bidirectional traffic before association, during AP-client association, after disassociation, and through an extended soak. No version or release numbering changed.
