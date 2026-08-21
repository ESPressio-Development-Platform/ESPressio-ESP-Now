## 0.6.0 — 2026-08-21

### Added
- Moved the ESP-Now lifecycle Event types into ESPressio ESP-Now, preserving their existing public names.
- Moved `ESPNowTransportEventBridge` into ESPressio ESP-Now alongside `ESPNowEventTransport`.
- Added dependency-boundary CI ensuring the core ESP-Now umbrella remains free of optional Event, Command, and Security integrations.
- Added ESP32 integration compilation for the relocated Event types/bridge against the Event 6.0.0 candidate.

### Changed
- Raised the optional Event integration baseline to ESPressio Event `>=6.0.0 <7.0.0`.
- Raised the coordinated optional Command baseline to `>=0.4.0 <1.0.0` and Security baseline to `>=0.3.0 <1.0.0`.
- Updated package and compile-time metadata for ESP-Now 0.6.0.
- Updated README and textual/graphical dependency documentation for the corrected one-way Event dependency architecture.

### Architecture
- Removed the previous reciprocal optional Event/ESP-Now relationship. Event 6.0.0 no longer consumes ESP-Now; all ESP-Now-specific Event integration is owned downstream by ESP-Now.
- Core ESP-Now continues to require only Timing and Observable. Event, Command, and Security remain opt-in.

### Compatibility
- Existing ESP-Now-specific Event header and class names are preserved, but their owning package changes from ESPressio Event to ESPressio ESP-Now.
- Core transport, clock synchronization, peer-liveness, wire framing, protocol IDs, Command transport, and Security transport semantics are unchanged.
