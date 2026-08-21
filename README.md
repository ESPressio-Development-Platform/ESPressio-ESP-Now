# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

## Current Version — 0.6.0

ESPressio ESP-Now **0.6.0** corrects the ownership of ESP-Now-specific Event integration. ESP-Now now owns its concrete Event Transport, ESP-Now lifecycle Event types, and `ESPNowTransportEventBridge`; ESPressio Event 6.0.0 remains responsible only for the generic Event mechanism and no longer depends back on ESP-Now.

The public integration header and class names are preserved because they already describe ESP-Now concepts unambiguously:

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

### Dependency model

```text
Required
    ESPressio Timing >= 2.2.4 < 3.0.0
    ESPressio Observable >= 3.0.1 < 4.0.0

Optional Event integration
    ESPressio Event >= 6.0.0 < 7.0.0

Optional Command integration
    ESPressio Command >= 0.4.0 < 1.0.0

Optional Secure Transport
    ESPressio Security >= 0.3.0 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command, and Security includes. Applications acquire those dependencies only by selecting their corresponding integration headers.

## Event integration ownership

The correct dependency direction is now:

```text
Event 6.0.0
    ^
    |
    | optional
    |
ESP-Now 0.6.0
    +-- ESPNowEventTransport
    +-- ESPNow lifecycle Event types
    +-- ESPNowTransportEventBridge
```

This removes the previous optional reciprocal dependency where Event hosted `ESPNowTransportEventBridge` while ESP-Now already consumed Event for `ESPNowEventTransport`.

`ESPNowTransportEventBridge` observes `ESPNowTransport` lifecycle notifications and publishes their Event representations. Because those concepts belong to ESP-Now, the bridge and Event types are owned by ESP-Now.

## Peer-liveness reliability

The peer-liveness model introduced in 0.5.3 remains unchanged. `ESPNowPeerLivenessTracker` distinguishes `Alive`, `Suspect`, and `Expired` peers, and any validated ESPressio ESP-NOW frame can refresh peer liveness. This prevents transient discovery-packet loss from tearing down a healthy Event destination.

## Compatibility

0.6.0 preserves the existing ESP-Now-specific Event header and class names, but their owning package changes from ESPressio Event to ESPressio ESP-Now. Applications using these headers must therefore consume ESP-Now 0.6.0 together with Event 6.0.0.

Core ESP-NOW transport, clock synchronization, wire framing, protocol IDs, peer-liveness APIs, Command transport, and Security transport semantics are otherwise unchanged.

## ESPressio Development Platform

ESPressio libraries are discrete, composable components built around a common design ethos:

- **Light-weight** — minimise memory and operational overhead.
- **Ease of Use** — strongly typed abstractions over low-level facilities.
- **Object-Oriented** — a type for everything and everything in a type.
- **SOLID** — keep responsibilities and dependencies explicit.

ESPressio ESP-Now owns ESP-NOW-specific peer/radio/framing concerns and the optional integrations representing those concerns through other ESPressio mechanisms.

## Dependency documentation

See **[ESPressio Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.md)** for the complete 0.6.0 dependency direction and final coordinated release generation.

## License

Licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).
