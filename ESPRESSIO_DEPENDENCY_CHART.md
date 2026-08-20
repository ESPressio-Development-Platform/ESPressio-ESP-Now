# ESPressio Dependency Chart — ESP-Now 0.5.1

ESPressio ESP-Now keeps its core dependencies small and exposes higher-level
integrations only when explicitly selected.

```text
ESPressio ESP-Now 0.5.1
|
+-- required --> ESPressio Timing >= 2.2.3 < 3.0.0
|
+-- required --> ESPressio Observable >= 3.0.1 < 4.0.0
|
+-- optional --> ESPressio Event >= 5.8.0 < 6.0.0
|                  +-- ESPNowEventTransport
|
+-- optional --> ESPressio Command >= 0.3.0 < 1.0.0
|                  +-- ESPNowCommandTransport
|
+-- optional --> ESPressio Security >= 0.2.0 < 1.0.0
                   +-- ESPNowSecureTransport
```

The Event baseline intentionally remains a compatible 5.x range here rather
than creating a hard ESP-Now 0.5.1 -> Event 5.8.1 release dependency. Event is
optional and, importantly, Event 5.8 currently also contains an ESP-Now-specific
Observer bridge. Strengthening both edges would make the reciprocal dependency
more difficult to remove.

## Transitive Timing chain

```text
ESP-Now 0.5.1
    -> Timing 2.2.3
        -> Units 0.2.2
            - - -> Serializable >= 0.10.1 < 1.0.0
                   only for Serializable Unit representations
        -> Observable 3.0.1
```

ESP-Now does not acquire a direct Units or Serializable dependency through this
chain.

## Security placement

```text
Event / Command / Clock Sync / application protocol
                         |
                         v
                 ESPressio Security
                         |
                         v
                ESPNowSecureTransport
                         |
                         v
                   ESPNowTransport
                         |
                         v
                       ESP-NOW
```

Security remains below application protocol semantics and above the concrete
ESP-NOW radio transport. Event, Command, and Timing therefore do not gain direct
Security dependencies merely because their payloads may be protected.

## Optional dependency rule

The normal:

```cpp
#include <ESPressio_ESPNow.hpp>
```

does not include headers that introduce Event, Command, or Security dependencies.
Applications explicitly select the relevant integration headers.

## Circular-dependency audit

The current ecosystem contains one reciprocal optional relationship:

```text
ESP-Now - - -> Event
    ESPNowEventTransport

Event - - -> ESP-Now
    ESPNowTransportEventBridge
```

This is not the desired long-term dependency direction. The preferred hierarchy
is:

```text
Event
  ^
  |
  | optional downstream integration
  |
ESP-Now
  +-- ESPNowEventTransport
  +-- ESPNowTransportEventBridge   (preferred future location)
```

`ESPNowTransportEventBridge` is transport-specific integration and should move
downstream into ESP-Now's Event integration, or into a dedicated integration
package, so Event remains transport-neutral and no Event -> ESP-Now edge exists.

No new reciprocal dependency should be introduced before that relocation.

## PlatformIO

Core:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.1
    https://github.com/Flowduino/ESPressio-Timing@^2.2.3
    https://github.com/Flowduino/ESPressio-Observable@^3.0.1
```

Security integration additionally requires:

```ini
    https://github.com/Flowduino/ESPressio-Security@^0.2.0
```

Command/Event dependencies are added only when their adapters are compiled.
