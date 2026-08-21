# ESPressio ESP-Now Dependency Boundaries

ESPressio ESP-Now 0.6.0 owns ESP-Now-specific integrations while keeping its core dependency set limited to Timing and Observable.

Optional Event, Command, and Security integrations must not be included by the core `ESPressio_ESPNow.hpp` umbrella. Their concrete headers may depend on those libraries only when explicitly selected by an application.

For Event integration, ESP-Now is the downstream owner of `ESPNowEventTransport`, the ESP-Now lifecycle Event family, and `ESPNowTransportEventBridge`. ESPressio Event must not depend back on ESP-Now merely to represent ESP-Now lifecycle concepts.

The intended direction is therefore one-way:

```text
ESP-Now - - -> Event
Event       -> ESP-Now  NONE
```

This boundary is validated by CI for the 0.6.0 release line.
