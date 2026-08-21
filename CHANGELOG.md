# Changelog

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

The structure follows the principles of [Keep a
Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic
Versioning](https://semver.org/).

> **Historical note:** This changelog was reconstructed retrospectively
> from published GitHub Releases, tags, release notes, repository
> history, and the documented public API. Where an historical release
> had little or no release-note detail, the entry is intentionally terse
> rather than inferring unsupported intent.

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

-   Added `ESPNowEventTransport`, the first concrete transport for
    ESPressio Event 5.4 distributed Serializable Events.
-   Added `ESPNowProtocol::EventTransport`.
-   Added bidirectional Serializable Event transport over ESP-NOW.
-   Added multiple Event destination peers.
-   Added Event packet fragmentation and reassembly.
-   Added bounded reassembly memory usage and incomplete-reassembly
    timeout.
-   Added Event Transport packet-size protection.
-   Added Event 5.4 per-transport routing support.
-   Added point-to-point and all-device/broadcast Event Transport
    examples.

### Changed

-   Kept ESPressio Event and Serializable dependencies optional for
    applications using only ESP-NOW clock synchronization.
-   Preserved receive-callback isolation and existing Timing/System
    Clock synchronization.

## [0.1.0] - 2026-08-18

### Added

-   Initial ESPressio ESP-Now release.
-   Added reusable ESP-NOW transport infrastructure.
-   Added peer management with `ESPNowPeerConfig` and `MacAddress`.
-   Added versioned ESPressio ESP-NOW wire framing.
-   Reserved the initial protocol identifier for System Clock
    synchronization.
-   Added transport implementation for ESPressio Timing System Clock
    synchronization across two or more ESP32 devices.
-   Added example projects demonstrating multi-device clock
    synchronization.
