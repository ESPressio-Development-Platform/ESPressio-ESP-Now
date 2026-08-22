# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

## Current Version — 0.7.0

ESPressio ESP-Now **0.7.0** adds compatibility with ESPressio Command 1.0.0's typed `CommandInvocation` values while preserving the existing ESP-Now Command protocol-v1 wire representation. ESP-Now continues to own its concrete Event Transport, ESP-Now lifecycle Event types, and `ESPNowTransportEventBridge`; ESPressio Event 6.0.0 remains responsible only for the generic Event mechanism and does not depend back on ESP-Now.

The public Event integration header and class names remain:

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
    ESPressio Command >= 1.0.0 < 2.0.0

Optional Secure Transport
    ESPressio Security >= 0.3.0 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command, and Security includes. Applications acquire those dependencies only by selecting their corresponding integration headers.

## Command 1.x compatibility

Command 1.0.0 allows structured invocations to retain native scalar `CommandValue` types. The existing ESP-Now Command protocol remains **version 1** and intentionally preserves its historical string-valued wire representation:

```text
CommandValue -> ToString() -> ESP-Now Command protocol-v1 string
protocol-v1 string -> string-backed CommandValue
```

This means typed invocations such as integer and boolean values can be submitted through the ESP-Now Command API without breaking existing protocol-v1 peers. The receiving Command Registry remains responsible for typed parameter conversion and validation. Native scalar type identity is not carried across protocol v1, and null values are rejected because protocol v1 has no null representation.

## Event integration ownership

The dependency direction remains:

```text
Event 6.0.0
    ^
    |
    | optional
    |
ESP-Now 0.7.0
    +-- ESPNowEventTransport
    +-- ESPNow lifecycle Event types
    +-- ESPNowTransportEventBridge
```

`ESPNowTransportEventBridge` observes `ESPNowTransport` lifecycle notifications and publishes their Event representations. Because those concepts belong to ESP-Now, the bridge and Event types are owned by ESP-Now.

## Peer-liveness reliability

The peer-liveness model introduced in 0.5.3 remains unchanged. `ESPNowPeerLivenessTracker` distinguishes `Alive`, `Suspect`, and `Expired` peers, and any validated ESPressio ESP-NOW frame can refresh peer liveness. This prevents transient discovery-packet loss from tearing down a healthy Event destination.

## Compatibility

0.7.0 does not change ESP-NOW radio framing, Event transport semantics, clock synchronization, peer-liveness behavior, Security transport semantics, or the ESP-Now Command protocol-v1 wire layout. The Command integration now targets Command 1.x and adapts its typed structured value model at the existing protocol boundary.

## ESPressio Development Platform

ESPressio libraries are discrete, composable components built around a common design ethos:

- **Light-weight** — minimise memory and operational overhead.
- **Ease of Use** — strongly typed abstractions over low-level facilities.
- **Object-Oriented** — a type for everything and everything in a type.
- **SOLID** — keep responsibilities and dependencies explicit.

ESPressio ESP-Now owns ESP-NOW-specific peer/radio/framing concerns and the optional integrations representing those concerns through other ESPressio mechanisms.

## Final coordinated generation

```text
Observable    3.0.1
Serializable  0.10.2
Units         0.2.3
Timing        2.2.4
Threads       3.1.4
Command       1.0.0
Security      0.3.0
Event         6.0.0
Sockets       0.7.0
ESP-Now       0.7.0
Serial        0.7.0
```

## Dependency documentation

See **[ESPressio Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.md)** for the complete 0.7.0 dependency direction and coordinated release generation.

## License

Licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).
