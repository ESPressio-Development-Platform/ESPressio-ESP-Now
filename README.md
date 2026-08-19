# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio integrations for
ESP32-family devices.

## Latest Stable Version

**0.2.0**

## ESPressio Development Platform

ESPressio is a collection of discrete, composable component libraries
built around a common development ethos:

-   **Light-weight** --- minimise memory consumption and runtime
    overhead without sacrificing correctness.
-   **Ease of use** --- provide strongly typed, developer-friendly
    abstractions over lower-level facilities.
-   **Object-oriented** --- a type for everything, and everything in a
    type.
-   **SOLID** --- favour focused responsibilities, extensibility,
    substitutable abstractions, narrow interfaces, and dependency
    inversion wherever practical on embedded C++ platforms.

## License

Licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).

## ESPressio Library Dependencies

ESPressio is designed as a modular ecosystem of independently useful
libraries, with required dependencies kept explicit and optional
integrations introduced only when the corresponding functionality is
selected.

For a complete overview of required dependencies, opt-in dependencies,
and the overall hierarchy, see:

**[ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.md)**

-   **Solid relationships** represent required ESPressio dependencies.
-   **Dashed relationships** represent opt-in dependencies introduced
    only by the corresponding feature, integration, type, or header.

### Required ESPressio dependencies

-   **ESPressio Timing \>= 2.1.0**

### Optional: ESPressio Event

Event is required only when `ESPNowEventTransport` is selected.

## Architecture

ESPressio ESP-Now owns ESP-NOW-specific communication mechanisms, not
generic Event routing or clock discipline:

``` text
Timing
    -> clock synchronization semantics

Event
    -> Event Transport semantics

ESP-Now
    -> concrete ESP-NOW implementations
```

## System Clock synchronization

The library implements the ESP-NOW transport side of Timing's
distributed System Clock synchronization.

Timing remains responsible for synchronization calculations, System
Clock discipline, state, and Observer notifications.

The design supports two or more ESP32 devices.

## Optional Event Transport

`ESPNowEventTransport` implements Event's transport-neutral
`IEventTransport` abstraction so registered Serializable Events can
travel over ESP-NOW.

The dependency remains opt-in: clock-sync-only consumers do not acquire
Event/Serializable requirements.

## Broadcast / all-device Event transport

The library supports distributed Event patterns where Events are sent to
all participating devices rather than a single point-to-point peer.

Routing policy remains in:

``` cpp
Event::EventTransportManager
```

including inbound, outbound, bidirectional, and per-transport
registration.

## Design goals

-   Keep ESP-NOW concerns separate from generic policy.
-   Reuse Timing synchronization contracts.
-   Reuse Event Transport contracts.
-   Support multi-device operation.
-   Keep Event integration optional.
