# Changelog

All notable changes to this project are documented in this file.

The structure follows the principles of [Keep a
Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic
Versioning](https://semver.org/).

> **Historical note:** This changelog was reconstructed retrospectively
> from published GitHub Releases, tags, release notes, repository
> history, and the documented public API. Where an historical release
> had little or no release-note detail, the entry is intentionally terse
> rather than inferring unsupported intent.

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
