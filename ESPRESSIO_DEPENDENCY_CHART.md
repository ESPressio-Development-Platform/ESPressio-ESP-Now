# ESPressio Dependency Chart — ESP-Now 0.5.3

ESPressio ESP-Now keeps its core dependencies small and exposes higher-level
integrations only when explicitly selected.

```text
ESPressio ESP-Now 0.5.3
|
+-- required --> ESPressio Timing >= 2.2.4 < 3.0.0
|
+-- required --> ESPressio Observable >= 3.0.1 < 4.0.0
|
+-- optional --> ESPressio Event >= 5.8.3 < 6.0.0
|                  +-- ESPNowEventTransport
|
+-- optional --> ESPressio Command >= 0.3.0 < 1.0.0
|                  +-- ESPNowCommandTransport
|
+-- optional --> ESPressio Security >= 0.2.0 < 1.0.0
                   +-- ESPNowSecureTransport
```

Event remains an optional integration rather than a core ESP-Now dependency.
The validated Event baseline is now 5.8.3 so applications using Event Transport
also receive the allocation-free Event lifecycle synchronization fix.

## Transitive Timing chain

```text
ESP-Now 0.5.3
    -> Timing 2.2.4
        -> Units 0.2.3
            - - -> Serializable >= 0.10.2 < 1.0.0
                   only for Serializable Unit representations
        -> Observable 3.0.1
```

ESP-Now does not acquire a direct Units or Serializable dependency through this
chain.

## Peer-liveness reliability

ESP-Now 0.5.3 adds `ESPNowPeerLivenessTracker` and a validated-frame observer
notification. Discovery advertisements are therefore no longer required to be
the sole evidence that a known peer is alive: any valid ESPressio ESP-NOW frame
can refresh liveness. The tracker separates a short `Suspect` interval from
actual `Expired` state so transient broadcast loss does not require immediate
peer/destination teardown.

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

The current ecosystem contains two reciprocal optional relationships involving
Event integrations:

```text
ESP-Now - - -> Event
    ESPNowEventTransport

Event - - -> ESP-Now
    ESPNowTransportEventBridge
```

and, separately:

```text
Sockets - - -> Event
    socket Event transports

Event - - -> Sockets
    SocketWorkerEventBridge
    SocketSecuritySessionEventBridge
```

For ESP-Now, the preferred hierarchy is:

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
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.3
    https://github.com/Flowduino/ESPressio-Timing@^2.2.4
    https://github.com/Flowduino/ESPressio-Observable@^3.0.1
```

Event Transport additionally validates against:

```ini
    https://github.com/Flowduino/ESPressio-Event@^5.8.3
```

Security integration additionally requires:

```ini
    https://github.com/Flowduino/ESPressio-Security@^0.2.0
```

Command/Event dependencies are added only when their adapters are compiled.
