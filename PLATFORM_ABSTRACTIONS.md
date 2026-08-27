# Platform Abstractions Audit Trail

This file records ESP-NOW changes made during the platform-abstraction tranche tracked by issue #52.

## 2026-08-27

### Scope boundary
- ESP-NOW itself remains an ESP32-specific transport in this tranche.
- Native `esp_now_*`, ESP-NOW peer configuration and ESP32 radio-interface selection remain local to this repository.
- No generic ESPressio-Radio abstraction is introduced or anticipated by these changes.

### Shared-runtime findings
`ESPNowTransport` still contains three lower-level concerns that now have shared ESPressio abstractions:
- monotonic timestamp acquisition via direct `esp_timer_get_time()`;
- the bounded receive callback queue via native FreeRTOS queue APIs;
- worker stack high-water telemetry via direct FreeRTOS task APIs.

These are queued for migration to ESPressio-System/Threads without changing ESP-NOW transport behaviour.

### WiFi coexistence
- The existing `ESPNowWiFiCoordinator` already consumes ESPressio-WiFi radio-state and observer concepts for coordinated WiFi/ESP-NOW ownership.
- Direct native radio interrogation used for standalone ESP-NOW fallback remains acceptable where no WiFi manager is present; it must not duplicate higher-level WiFi lifecycle policy.

### Dependency alignment
- Updated the Timing working dependency from `main` to `feature/29-platform-clock-abstractions` so this branch consumes the current abstraction tranche.

## Boundary rule

Shared execution, queues, clocking and telemetry should consume ESPressio abstractions. Intrinsically ESP-NOW-specific operations remain implemented here.
