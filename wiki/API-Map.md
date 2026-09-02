# API Map

## Core transport

- `ESPNowTransport` — singleton ESP-NOW transport/lifecycle owner.
- `ESPNowTransportConfig` — worker, queue, WiFi/channel and transport configuration.
- `ESPNowSendResult` — stable send result classification plus native diagnostic value.
- `IESPNowTransportObserver` — synchronous transport lifecycle/diagnostics observer.

## Peers and radio

- `ESPNowPeerConfig` — logical peer configuration.
- `ESPNowWiFiInterface` — Auto/Station/AccessPoint interface policy.
- logical managed peer registry and native reconciliation.
- `ESPNowWiFiCoordinator` — optional direct coordination with ESPressio WiFi radio lifecycle.

## Distributed integrations

- `ESPNowClockSynchronizer` — Timing clock-synchronization transport.
- Event transport adapter.
- Command transport adapter.
- Security transport adapter.
- State transport adapter.

## Dependency direction

Timing, Observable and Threads provide core upstream facilities. WiFi, Event, Command, Security and State integrations are selected explicitly rather than pulled into the normal umbrella automatically.

## Platform boundary

ESP-Now owns ESP-NOW-specific transport semantics, but native driver/resource constraints remain ESP-IDF realities. Shared-radio authority belongs to ESPressio WiFi whenever WiFi is present.