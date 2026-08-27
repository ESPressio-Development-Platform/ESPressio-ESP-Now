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

### WiFi coexistence
- The existing `ESPNowWiFiCoordinator` already consumes ESPressio-WiFi radio-state and observer concepts for coordinated WiFi/ESP-NOW ownership.
- Direct native radio interrogation used for standalone ESP-NOW fallback remains acceptable where no WiFi manager is present; it must not duplicate higher-level WiFi lifecycle policy.

### Dependency alignment
- ESPressio-System is an explicit working dependency for shared runtime/platform capabilities.
- Timing remains aligned to `feature/29-platform-clock-abstractions` for the current abstraction tranche.

## Boundary rule

Shared execution, queues, clocking and telemetry consume ESPressio abstractions. Intrinsically ESP-NOW-specific operations remain implemented here.
