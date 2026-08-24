# ESPressio Dependency Chart — Current Released Generation

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

This document records the completed Serializable 0.11.3 cascade and the dependency generation validated by ESPressio ESP-Now 0.8.3.

## Released generation

```text
Observable    3.0.2
Serializable  0.11.3
Units         0.2.7
Timing        2.2.8
Threads       3.1.7
Event         6.0.3
Command       1.0.3
Security      0.4.2
Persistence   0.3.2
Sockets       0.7.3
ESP-Now       0.8.3
WiFi          0.2.0
Serial        0.8.1
```

## ESP-Now dependency position

```text
ESP-Now 0.8.3
    -> Timing >= 2.2.8 < 3.0.0
    -> Observable >= 3.0.2 < 4.0.0

ESP-Now optional integrations
    - - -> Event >= 6.0.3 < 7.0.0
    - - -> Command >= 1.0.3 < 2.0.0
    - - -> Security >= 0.4.2 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains integration-neutral. ESP-Now owns its concrete Event transport and lifecycle Event bridge, preserving one-way dependency direction.

## Dependency-direction invariants

```text
ESP-Now - - -> Event
ESP-Now - - -> Command
ESP-Now - - -> Security

Event    -> ESP-Now  NONE
Command  -> ESP-Now  NONE
Security -> ESP-Now  NONE
```

The Serializable 0.11.3 cascade is complete through WiFi 0.2.0 and terminal Serial 0.8.1. No upstream ESPressio library depends on Serial. ESPressio Tree remains standalone.
