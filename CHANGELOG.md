# Changelog

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

## \[0.2.0\] - 2026-08-19

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

## \[0.1.0\] - 2026-08-18

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
