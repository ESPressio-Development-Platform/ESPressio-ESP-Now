# Platform Abstractions Audit Trail

This file records ESP-NOW changes made during the platform-abstraction tranche tracked by issue #52.

## 2026-08-27

### Scope boundary
- ESP-NOW itself remains an ESP32-specific transport in this tranche.
- Native `esp_now_*`, ESP-NOW peer configuration and ESP32 radio-interface selection remain local to this repository.
- No generic ESPressio-Radio abstraction is introduced or anticipated by these changes.

### Shared-runtime migration
`ESPNowTransport` previously contained three lower-level concerns that now have shared ESPressio abstractions:
- monotonic timestamp acquisition via direct `esp_timer_get_time()`;
- the bounded receive callback queue via native FreeRTOS queue APIs;
- worker stack high-water telemetry via direct FreeRTOS task APIs.

These now consume ESPressio-System directly:
- monotonic timestamps use the installed `System::Clock::Monotonic()` provider;
- the receive callback queue is created through `System::Queue` and consumed through `IMessageQueue`;
- stack high-water telemetry is read through the installed `System::Execution` provider.

No ESP-NOW transport or protocol semantics were generalized as part of this migration.

### Worker configuration
- Hardware Lab validation exposed that `ESPNowTransportConfig` still leaked FreeRTOS task types after the runtime migration.
- `ReceiveTaskPriority` now uses portable `uint32_t` and `ReceiveTaskCore` uses portable `int32_t`, with `-1` representing no specific processor affinity.
- The worker continues to delegate priority/core configuration through ESPressio-Threads; no FreeRTOS task type remains necessary in the public transport configuration.

### Hardware regression correction
- StickA and StickB both failed before native `esp_now_init()` became operational, regardless of whether ESPressio-WiFi/AP passwording was enabled.
- The visible ESP-IDF `esp now not init!` diagnostic was produced during failed-initialization cleanup and was not the original cause.
- The associated global Thread failures exposed a provider-installation ordering hazard in System synchronization. System now supplies deferred binary signals so globally constructed Threads can bind to the concrete synchronization provider when they initialize after application bootstrap.
- ESP-NOW issue #52 remains open until hardware validation confirms worker initialization, native ESP-NOW initialization, peer discovery and downstream Event/State transport operation.

### WiFi coexistence
- The existing `ESPNowWiFiCoordinator` already consumes ESPressio-WiFi radio-state and observer concepts for coordinated WiFi/ESP-NOW ownership.
- Direct native radio interrogation used for standalone ESP-NOW fallback remains acceptable where no WiFi manager is present; it must not duplicate higher-level WiFi lifecycle policy.

### Dependency alignment
- ESPressio-System is an explicit working dependency for shared runtime/platform capabilities.
- Timing remains aligned to `feature/29-platform-clock-abstractions` for the current abstraction tranche.

### AP station topology reconciliation (#44)
- A long-running two-device hardware test exposed a persistent one-way failure after a phone associated with StickA's access point.
- StickA continued receiving and executing StickB traffic, while StickA's outbound ESP-NOW sends failed across State, Command, Timing and discovery protocols; disconnecting the phone did not restore the transmit path.
- AP station association/disassociation is not represented as a WiFi radio mode/channel transition. It is exposed separately through `IWiFiObserver`, so the existing `IWiFiRadioObserver`-only coordinator never rebuilt native ESP-NOW state for this topology change.
- `ESPNowWiFiCoordinator` now observes both WiFi lifecycle surfaces.
- `OnAccessPointStationConnected` and `OnAccessPointStationDisconnected` re-read the authoritative radio state and force the existing native ESP-NOW reinitialization plus managed-peer replay path.
- Logical peers, encryption keys, protocol handlers, worker state and higher-level transport state remain preserved; no duplicate peer registry or send-retry shim was introduced.
- ESP32 coexistence CI now guards both observer interfaces and compiles both AP-station topology callbacks.
- #44 remains open pending hardware validation of association, sustained traffic, disassociation, and continued bidirectional recovery.
- No release/version numbers were changed.

## Boundary rule

Shared execution, queues, clocking and telemetry consume ESPressio abstractions. Intrinsically ESP-NOW-specific operations remain implemented here.
