# ESPressio Dependency Chart — Current Released Generation

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

This document records the completed Serializable 0.11.3 cascade and the dependency generation historically validated by ESPressio ESP-Now 0.8.3. Operative dependency references for the release restructure now target repository `main` branches.

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
ESP-Now
    -> System main
    -> Task main
    -> Units main
    -> Timing main
    -> Observable main
    -> Threads main

ESP-Now optional integrations
    - - -> Event main
    - - -> Command main
    - - -> Security main
    - - -> WiFi main
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

The former Serializable 0.11.3 cascade remains recorded above as historical release context. Current dependency consumption for the restructuring phase is from repository `main`. No upstream ESPressio library depends on Serial. ESPressio Tree remains standalone.
