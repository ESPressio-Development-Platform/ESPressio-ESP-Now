# ESPressio Dependency Chart — Current Cascade Generation

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

This document records the dependency generation validated by ESPressio ESP-Now 0.8.3. Arrows point from the consuming library to the library it consumes.

- **Required** — the dependency is part of the library's normal/core contract.
- **Opt-in** — the dependency is introduced only when the corresponding integration/header is selected.

## Cascade generation

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
```

## Required dependencies

```text
Timing 2.2.8
    -> Units >= 0.2.7 < 1.0.0
    -> Observable >= 3.0.2 < 4.0.0

ESP-Now 0.8.3
    -> Timing >= 2.2.8 < 3.0.0
    -> Observable >= 3.0.2 < 4.0.0
```

## ESP-Now opt-in integrations

```text
ESP-Now
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

Serial remains terminal/downstream. No upstream ESPressio library should depend on Serial.

The next cascade step after ESP-Now 0.8.3 is to update downstream consumers, including WiFi where applicable and finally Serial.
