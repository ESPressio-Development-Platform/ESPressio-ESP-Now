# ESPressio Dependency Chart — ESP-Now 0.7.0

ESPressio ESP-Now keeps its core dependencies small and owns all ESP-Now-specific Event integration downstream of the generic Event mechanism.

```text
ESPressio ESP-Now 0.7.0
|
+-- required --> ESPressio Timing >= 2.2.4 < 3.0.0
|
+-- required --> ESPressio Observable >= 3.0.1 < 4.0.0
|
+-- optional --> ESPressio Event >= 6.0.0 < 7.0.0
|                  +-- ESPNowEventTransport
|                  +-- ESPNow Event types
|                  +-- ESPNowTransportEventBridge
|
+-- optional --> ESPressio Command >= 1.0.0 < 2.0.0
|                  +-- ESPNowCommandTransport
|
+-- optional --> ESPressio Security >= 0.3.0 < 1.0.0
                   +-- ESPNowSecureTransport
```

The normal `ESPressio_ESPNow.hpp` umbrella does not include Event, Command, or Security integrations. Those relationships are introduced only when their specific integration headers are selected.

Command 1.x structured values are normalized at the existing ESP-Now Command protocol-v1 boundary, preserving the v1 wire layout and existing peer interoperability.

## Corrected Event dependency direction

```text
Event 6.0.0
    ^
    |
    | optional
    |
ESP-Now 0.7.0
    +-- ESPNowEventTransport
    +-- ESPNow Event types
    +-- ESPNowTransportEventBridge
```

Event does not consume ESP-Now merely to represent ESP-Now lifecycle information as Events. ESP-Now owns those domain-specific Event types and the Observer-to-Event bridge, while Event remains responsible for the generic Event mechanism and transport abstraction.

## Transitive Timing chain

```text
ESP-Now 0.7.0
    -> Timing 2.2.4
        -> Units 0.2.3
            - - -> Serializable >= 0.10.2 < 1.0.0
        -> Observable 3.0.1
```

ESP-Now does not acquire a direct Units or Serializable dependency through this chain.

## Final coordinated ecosystem generation

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

## Dependency-direction invariant

Domain-specific Event integrations belong to the lowest-order consumer that can own them without introducing a reverse dependency. ESP-Now already optionally consumes Event for its concrete Event Transport, so ESP-Now is also the correct owner of ESP-Now-specific Event types and `ESPNowTransportEventBridge`.
