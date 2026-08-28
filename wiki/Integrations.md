# Event, Command and Security Integrations

ESP-Now keeps higher-level ESPressio integrations optional.

## Event transport

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
```

The Event adapter implements Event's transport-neutral contract using ESP-NOW framing/routing. Event type identity and Serializable Event semantics remain owned by ESPressio Event.

## Command transport

```cpp
#include <ESPressio_ESPNowCommandTransport.hpp>
```

Command protocol handling and maintenance execute through the ESP-NOW worker so receive processing and timeout/reassembly state remain serialized in one managed context.

Application-thread invocation crosses only the synchronization boundary required to interact with that worker-owned state.

## Security transport

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
```

Security integration applies the Security library's protection/authentication contracts to ESP-NOW traffic. ESP-Now does not become the owner of cipher/key policy.

## Dependency discipline

The normal `ESPressio_ESPNow.hpp` umbrella should remain free of Event, Command and Security includes. Projects pay for these dependencies only when they select the corresponding integration header.

## Design rule

Each integration adapts an existing authoritative domain contract to ESP-NOW. Do not duplicate Event routing, Command execution or Security policy inside ESP-Now merely because ESP-NOW is the selected transport.