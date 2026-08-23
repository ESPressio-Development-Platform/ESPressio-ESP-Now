# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the ESPressio Development Platform.

## Current Version — 0.8.2

ESPressio ESP-Now **0.8.2** is a dependency-maintenance release aligning the hardened 0.8 generation with the corrected Serializable 0.11.2 cascade. The 8192-byte receive-task default and `GetReceiveTaskMinimumFreeStackBytes()` diagnostics introduced in 0.8.0 remain unchanged, as do ESP-NOW wire framing and protocol identifiers.

## Dependencies

Required:

```text
ESPressio Timing >= 2.2.7 < 3.0.0
ESPressio Observable >= 3.0.2 < 4.0.0
Arduino-ESP32
```

Optional integrations:

```text
ESPressio Event >= 6.0.2 < 7.0.0
ESPressio Command >= 1.0.2 < 2.0.0
ESPressio Security >= 0.4.1 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command and Security includes.

## Installation

```ini
lib_deps =
    espressio-development-platform/ESPressio-ESP-Now@^0.8.2
    espressio-development-platform/ESPressio-Timing@^2.2.7
    espressio-development-platform/ESPressio-Observable@^3.0.2
```

Add Event, Command and Security only when their adapters are selected.

## Core transport

```cpp
#include <ESPressio_ESPNow.hpp>

auto& transport = ESPressio::ESPNow::ESPNowTransport::GetInstance();
transport.Initialize();
```

Incoming Wi-Fi callback data is copied into a bounded FreeRTOS queue. Protocol validation and application handlers run on the ESPressio receive task. The default receive-task stack remains 8192 bytes and can be configured through `ESPNowTransportConfig::ReceiveTaskStackSize`.

```cpp
const uint32_t minimumFreeBytes = transport.GetReceiveTaskMinimumFreeStackBytes();
```

## Clock synchronization

`ESPNowClockSynchronizer` transports ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for sample validation, estimation and SystemClock discipline. ESP-Now 0.8.2 validates this surface against Timing 2.2.7.

## Event integration

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

Event integration remains opt-in and is validated against Event 6.0.2. ESP-Now continues to own its concrete Event transport and the Event representation of ESP-Now lifecycle observations, preserving the one-way dependency `ESP-Now -> Event`.

## Command integration

```cpp
#include <ESPressio_ESPNowCommandTransport.hpp>
```

Command integration remains opt-in and is validated against Command 1.0.2. ESP-Now Command protocol v1 remains wire-compatible: scalar `CommandValue` instances are normalized at the existing string-valued wire boundary and reconstructed as compatible invocation values on receive.

## Security integration

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
```

Security integration remains opt-in and is validated against Security 0.4.1. `ESPNowSecureTransport` owns ESP-NOW-specific framing/fragmentation while ESPressio Security owns authenticated encryption, sender/session identity, protocol binding and replay protection.

## Observable lifecycle

`IESPNowTransportObserver` remains the synchronous lifecycle surface for initialization, shutdown, peers, sends and validated inbound ESPressio frames. `ESPNowTransportEventBridge` optionally converts those observations into asynchronous Events.

## Corrected cascade generation

```text
Observable    3.0.2
Serializable  0.11.2
Units         0.2.6
Timing        2.2.7
Threads       3.1.6
Event         6.0.2
Command       1.0.2
Security      0.4.1
Persistence   0.3.1
Sockets       0.7.2
ESP-Now       0.8.2
```

## Compatibility

No public ESP-Now API or runtime behaviour changes are introduced in 0.8.2. Wire framing, protocol IDs, peer management, clock synchronization, Event Transport, Command protocol-v1, Security transport, peer-liveness semantics and receive-task execution semantics are unchanged.

See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md), [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), and [CHANGELOG.md](CHANGELOG.md) for further details.
