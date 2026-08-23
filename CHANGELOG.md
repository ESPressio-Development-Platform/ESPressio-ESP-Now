# Changelog

## 0.8.2 — 2026-08-23

### Changed
- Raised required Timing to `>=2.2.7 <3.0.0` while preserving Observable `>=3.0.2 <4.0.0`.
- Raised optional Event to `>=6.0.2 <7.0.0`, Command to `>=1.0.2 <2.0.0`, and Security to `>=0.4.1 <1.0.0`.
- Updated Event integration validation to Serializable 0.11.2, Units 0.2.6, Timing 2.2.7, Threads 3.1.6 and Event 6.0.2.
- Updated Command/Security validation to Command 1.0.2 and Security 0.4.1.
- Updated package metadata and README for ESP-Now 0.8.2.

### Architecture
- Timing and Observable remain the only required ESPressio dependencies.
- Event, Command and Security remain opt-in and outside the normal `ESPressio_ESPNow.hpp` umbrella.
- ESP-Now continues to own its concrete Event transport and ESP-Now lifecycle Event bridge.

### Compatibility
- No public ESP-Now API or runtime behaviour changes.
- ESP-NOW wire framing, protocol IDs, clock synchronization, peer-liveness, Command protocol-v1, Event Transport, Security transport and receive-task semantics remain unchanged.

### Tracking
- Closes #33.

## 0.8.1 — 2026-08-22

### Changed
- Published the post-migration ESPressio ESP-Now package generation from `ESPressio-Development-Platform`.
- Raised required Timing to `>=2.2.5 <3.0.0` and Observable to `>=3.0.2 <4.0.0`.
- Raised optional Event to `>=6.0.1 <7.0.0`, Command to `>=1.0.1 <2.0.0`, and Security to `>=0.3.1 <1.0.0`.
- Updated package metadata, integration documentation, CI validation, and dependency documentation.

### Compatibility
- No ESP-Now public API, wire framing, protocol identifiers, or runtime behaviour changes are introduced by this repository-relocation patch release.

## 0.8.0 — 2026-08-22

### Added
- Added `ESPNowTransport::GetReceiveTaskMinimumFreeStackBytes()` so applications and hardware stress tests can inspect the minimum unused ESP-NOW receive-task stack observed since initialization.
- The diagnostic returns `0` when the receive task is unavailable, including before initialization and after shutdown.
- Added host regression coverage for the safer receive-task stack default.

### Changed
- Increased the default `ESPNowTransportConfig::ReceiveTaskStackSize` from 4096 to 8192 bytes after EventConsole-Lab hardware stress testing reproduced a FreeRTOS stack-canary failure while processing repeated Command traffic alongside Timing synchronization, Observable notifications, Security/Event integrations, and diagnostics.
- Preserved the existing configurable stack-size field so applications with unusually deep protocol handlers can still select a larger or smaller allocation explicitly.
- Corrected compile-time ESP-Now version macros to 0.8.0 and updated PlatformIO/Arduino package metadata, README, and textual/graphical dependency charts.

### Compatibility
- This is an interface-extending minor release: existing 0.7.x callers remain source-compatible and the new stack diagnostic is additive.
- ESP-NOW wire framing, frame version, protocol identifiers, peer management, clock synchronization, Event Transport, Command protocol-v1, Security transport, and peer-liveness semantics are unchanged.
- Registered protocol handlers continue to execute synchronously on the ESP-NOW receive task; 0.8.0 hardens the default stack allocation and makes remaining stack headroom observable without changing execution semantics.

### Tracking
- Implements #27.

## 0.7.0 — 2026-08-22

### Changed
- Updated the optional ESPressio Command integration baseline to Command >= 1.0.0 < 2.0.0.
- Adapted ESP-Now Command protocol v1 to Command 1.0.0 `CommandValue` positional and named containers.
- Native scalar `CommandValue` instances are normalized through `ToString()` at the established protocol-v1 wire boundary and reconstructed as string-backed values on decode, preserving compatibility with existing protocol-v1 peers.
- Null `CommandValue` is rejected because the existing protocol-v1 representation has no null value.
- Added host regression coverage for typed integer, boolean and floating-point invocations, protocol normalization, and null rejection.
- Updated ESP32 integration validation to released ESPressio Command 1.0.0.
- Updated package/component metadata, README, Command integration guide, CI and textual/graphical dependency charts for ESP-Now 0.7.0.

### Compatibility
- ESP-Now Command wire protocol version remains 1; no peer wire-format migration is required.
- Core ESP-NOW transport, peer/liveness behavior, clock synchronization, Event Transport and Security transport semantics are unchanged.
- Event, Command and Security remain opt-in integrations outside the normal `ESPressio_ESPNow.hpp` umbrella.

### Tracking
- Implements #22.
- Cascades ESPressio Command 1.0.0.

## 0.6.0 — 2026-08-21

### Added
- Moved the ESP-Now lifecycle Event family into ESPressio ESP-Now, making the domain library the owner of its concrete Event representations.
- Moved `ESPNowTransportEventBridge` into ESPressio ESP-Now alongside `ESPNowEventTransport`.
- Preserved the existing ESP-Now-specific public header and class names because they remain semantically unambiguous after relocation.

### Changed
- Updated the optional Event integration baseline to ESPressio Event >= 6.0.0 < 7.0.0.
- Updated the optional Command integration baseline to ESPressio Command >= 0.4.0 < 1.0.0.
- Updated the optional Security integration baseline to ESPressio Security >= 0.3.0 < 1.0.0.
- Removed the former reciprocal Event -> ESP-Now dependency direction: ESP-Now now owns all ESP-Now-specific Event integration while Event remains transport-neutral.
- Updated package/component metadata, README, CI and textual/graphical dependency documentation for 0.6.0.
- Retained required Timing >= 2.2.4 < 3.0.0 and Observable >= 3.0.1 < 4.0.0 baselines.

### Compatibility
- Core ESP-NOW transport, clock synchronization, wire framing, protocol identifiers and peer-liveness behavior remain unchanged from 0.5.3.
- Existing `ESPressio_ESPNowEvents.hpp` and `ESPressio_ESPNowTransportEventBridge.hpp` names are preserved, but applications must obtain them from ESPressio ESP-Now 0.6.0 rather than ESPressio Event.
- Event, Command and Security remain opt-in integrations and are not introduced by the core `ESPressio_ESPNow.hpp` umbrella.

## 0.5.3 — 2026-08-21

### Added
- Added `ESPNowPeerLivenessTracker`, a bounded reusable peer-liveness tracker with distinct `Alive`, `Suspect`, and `Expired` states.
- Added `IESPNowTransportObserver::OnESPNowFrameReceived`, emitted after ESPressio wire-header and payload-length validation for every protocol, so discovery, clock, Event, Command, Security, and user traffic can all act as peer-liveness evidence.
- Added host regression coverage for transient discovery loss, traffic-based liveness refresh, genuine hard expiry, removal, and rediscovery.

### Fixed
- Fixed the liveness model exposed by EventConsole-Lab hardware testing where several missed discovery broadcasts could cause a healthy peer and Event destination to be removed even while useful addressed traffic was still flowing.
- Separated short-term suspicion from hard expiry so consumers no longer need to equate a brief discovery gap with peer disappearance.

### Changed
- Bumped package and compile-time version metadata to 0.5.3.
- Updated README and textual/graphical dependency documentation for 0.5.3.
- Updated the validated optional Event integration baseline to Event `>=5.8.3 <6.0.0`, propagating the Event 5.8.3 per-Event lock-resource fix without making Event a mandatory dependency.
- Required Timing remains `>=2.2.4 <3.0.0`; required Observable remains `>=3.0.1 <4.0.0`.

### Compatibility
- Existing ESP-NOW transport, clock synchronization, Event Transport, Command Transport, and Security APIs remain source-compatible.
- The new receive observer callback has a default no-op implementation, so existing `IESPNowTransportObserver` implementations do not need to change.
- Wire framing and protocol IDs are unchanged.

## 0.5.2 — 2026-08-21

### Changed
- Raised the required ESPressio Timing baseline from 2.2.3 to 2.2.4, carrying the Units 0.2.3 / Serializable 0.10.2 dependency refresh downstream.
- Preserved the required ESPressio Observable baseline at `>=3.0.1 <4.0.0`.
- Preserved Command >= 0.3.0 < 1.0.0 and Security >= 0.2.0 < 1.0.0 as opt-in integration baselines.
- Kept Event as an opt-in compatible 5.x integration rather than introducing a hard Event 5.8.2 dependency.
- Updated package, compile-time version metadata, README and dependency documentation to 0.5.2.
- Preserved the documented dependency-cycle resolution direction: ESP-Now-specific Event bridge code belongs downstream with ESP-Now's Event integration.

### Compatibility
- ESP-NOW transport, clock synchronization, Event Transport, Command Transport, Security integration, and Observable APIs remain source-compatible with 0.5.1.
- Event remains opt-in; no new mandatory Event dependency is introduced.

## 0.5.1 — 2026-08-20

### Changed
- Raised the required ESPressio Timing baseline from 2.2.2 to 2.2.3, carrying the Units 0.2.2 / Serializable 0.10.1 dependency refresh downstream.
- Preserved the required ESPressio Observable baseline at `>=3.0.1 <4.0.0`.
- Preserved Command >= 0.3.0 < 1.0.0 and Security >= 0.2.0 < 1.0.0 as opt-in integration baselines.
- Updated package and compile-time version metadata to 0.5.1.
- Documented the existing reciprocal optional Event/ESP-Now integration as an architectural dependency-cycle exception pending downstream relocation of the ESP-Now-specific Observer-to-Event bridge.

### Compatibility
- ESP-NOW transport, clock synchronization, Event Transport, Command Transport, Security integration, and Observable APIs remain source-compatible with 0.5.0.
- Event remains opt-in; no new mandatory Event dependency is introduced.

## 0.5.0 — 2026-08-20

### Added
- Added `IESPNowTransportObserver` and observable transport lifecycle notifications.
- Added notifications for initialization success/failure, shutdown, peer addition/removal, and send acceptance/failure.
- Added ESPressio Observable >= 3.0.1 < 4.0.0 as a core dependency.
- Added optional ESPressio Event bridge support through ESPressio Event 5.8.0.

### Changed
- Updated the validated optional ESPressio Command baseline to Command >= 0.3.0 < 1.0.0.
- Updated the validated optional ESPressio Security baseline to Security >= 0.2.0 < 1.0.0.
- Bumped package and compile-time version metadata to 0.5.0.
- Event, Command and Security integrations remain opt-in.

## 0.4.0 — 2026-08-20

### Added
- Added opt-in ESPressio Security integration targeting Security >= 0.1.0 < 1.0.0.
- Added `ESPNowProtocol::SecureTransport` (protocol ID 4).
- Added `ESPNowSecurityProtocol` for bounded fragmentation and reassembly of transport-security envelopes across ESP-NOW frames.
- Added `ESPNowSecureTransport`, which applies ESPressio Security before ESP-NOW transmission and authenticates/decrypts before application delivery.
- Added security failure observation without exposing key material.
- Added support for Security sender IDs, authenticated session epochs, sequence/replay protection, key rotation, and runtime-selectable AEAD algorithms.
- Added secure ESP-NOW example and host coverage for fragmentation, out-of-order reassembly, malformed fragments, and protocol allocation.

### Changed
- Bumped package and compile-time version metadata to 0.4.0.
- Security remains optional; `ESPressio_ESPNow.hpp` does not include the Security integration header.
- Plain ESP-NOW, Event Transport, Command Transport, and clock synchronization APIs remain source-compatible.
- Security envelope fragmentation allows protected payloads to span multiple ESP-NOW v1-compatible frames.

### Compatibility
- Existing 0.3.x applications continue to operate unchanged when Security is not selected.
- ESPressio Security is an optional downstream dependency and does not become mandatory for core ESP-NOW use.

## 0.3.0 — 2026-08-20

### Added
- Added opt-in ESPressio Command integration targeting Command >= 0.2.0 < 1.0.0.
- Added `ESPNowProtocol::CommandTransport`.
- Added `ESPNowCommandProtocol`, a compact versioned binary request/result protocol with no Serializable or ArduinoJson dependency.
- Added `ESPNowCommandEndpoint`, a host-testable Command endpoint implementing asynchronous invocation, correlated results, bounded fragmentation/reassembly, per-peer state isolation, timeouts, policy hooks, result observation, and duplicate-request suppression/result replay.
- Added `ESPNowCommandTransport`, a thin adapter over the existing shared `ESPNowTransport` protocol-handler/send infrastructure.
- Added bounded outstanding-request, reassembly and duplicate-result caches.
- Added configurable request, reassembly and duplicate-cache timeouts.
- Added peer/request metadata for remote Command authorization, auditing and diagnostics.
- Added comprehensive host tests and permanent GitHub Actions validation against released ESPressio Command 0.2.0.
- Added a two-peer Command example and dedicated Command integration documentation.

### Changed
- Bumped package and compile-time version metadata to 0.3.0.
- Updated README/dependency documentation for optional Command transport.
- Preserved existing ESP-NOW transport, clock synchronization and Event Transport APIs.
- Kept ESPressio Command optional; the normal `ESPressio_ESPNow.hpp` umbrella does not include Command integration headers.

### Compatibility
- Existing 0.2.x transport, synchronization and Event Transport source usage remains supported.
- Command integration is opt-in and adds no mandatory Serializable dependency.

## 0.2.3 — 2026-08-20

### Changed
- Raised the required ESPressio Timing baseline from 2.2.1 to 2.2.2 so ESP-Now consumes the refreshed Timing dependency chain.
- Updated the validated optional ESPressio Event Transport baseline to Event 5.7.1 within the 5.x line.
- Updated package metadata for ESP-Now 0.2.3.
- No ESP-NOW transport, synchronization, fragmentation, or Event Transport runtime semantics changed.

## 0.2.2 — 2026-08-19

### Changed
- Updated active ESPressio dependency baselines to the latest released versions available on 2026-08-19.
- Bounded dependency compatibility to the current major version so future breaking major releases are not selected automatically.
- Updated optional ESPressio Event integrations to require Event 5.6.2 or newer within the 5.x line.
- Updated the required ESPressio Timing baseline to 2.2.1 within the 2.x line.
- Corrected compile-time patch-version macros to match the package version.

All notable changes to this project are documented in this file.

The structure follows the principles of Keep a Changelog and Semantic Versioning.

## [0.2.1] - 2026-08-19

### Changed
- Updated the optional ESPressio Event Transport integration baseline from ESPressio Event 5.4.0 to 5.5.0.
- Updated Event Transport compatibility documentation and compile-time dependency guidance for Event 5.5.0.
- Bumped ESPressio ESP-Now package/version metadata to 0.2.1.

### Compatibility
- No ESP-NOW transport interface or runtime behaviour changes are introduced by this patch release.
- Existing ESP-NOW System Clock synchronization remains unchanged.
- Event Transport remains opt-in; applications using only ESP-NOW/Timing functionality do not acquire ESPressio Event or Serializable dependencies.

## [0.2.0] - 2026-08-19

### Added
- Added `ESPNowEventTransport`, the first concrete transport for ESPressio Event 5.4 distributed Serializable Events.
- Added `ESPNowProtocol::EventTransport`.
- Added bidirectional Serializable Event transport over ESP-NOW.
- Added multiple Event destination peers.
- Added Event packet fragmentation and reassembly.
- Added bounded reassembly memory usage and incomplete-reassembly timeout.
- Added Event Transport packet-size protection.
- Added Event 5.4 per-transport routing support.
- Added point-to-point and all-device/broadcast Event Transport examples.

### Changed
- Kept ESPressio Event and Serializable dependencies optional for applications using only ESP-NOW clock synchronization.
- Preserved receive-callback isolation and existing Timing/System Clock synchronization.

## [0.1.0] - 2026-08-18

### Added
- Initial ESPressio ESP-Now release.
- Added reusable ESP-NOW transport infrastructure.
- Added peer management with `ESPNowPeerConfig` and `MacAddress`.
- Added versioned ESPressio ESP-NOW wire framing.
- Reserved the initial protocol identifier for System Clock synchronization.
- Added transport implementation for ESPressio Timing System Clock synchronization across two or more ESP32 devices.
- Added example projects demonstrating multi-device clock synchronization.
