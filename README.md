# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the ESPressio Development Platform.

## Current Version — 0.8.3

ESPressio ESP-Now **0.8.3** is the current release generation aligned with Serializable 0.11.3. The release is being corrected in place under issue #40 to harden coexistence with normal ESP32 WiFi operation and eliminate cross-task protocol-state races while preserving the 0.8.3 version number and existing ESP-NOW wire format.

## Dependencies

Required:

```text
ESPressio Timing >= 2.2.8 < 3.0.0
ESPressio Observable >= 3.0.2 < 4.0.0
ESPressio Threads >= 3.1.7 < 4.0.0
Arduino-ESP32
```

Optional integrations:

```text
ESPressio Event >= 6.0.3 < 7.0.0
ESPressio Command >= 1.0.3 < 2.0.0
ESPressio Security >= 0.4.2 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command and Security includes.

## Installation

```ini
lib_deps =
    espressio-development-platform/ESPressio-ESP-Now@^0.8.3
    espressio-development-platform/ESPressio-Timing@^2.2.8
    espressio-development-platform/ESPressio-Observable@^3.0.2
    espressio-development-platform/ESPressio-Threads@^3.1.7
```

Add Event, Command and Security only when their adapters are selected.

## Core transport and worker ownership

```cpp
#include <ESPressio_ESPNow.hpp>

auto& transport = ESPressio::ESPNow::ESPNowTransport::GetInstance();
transport.Initialize();
```

ESP-Now now owns a rate-limited ESPressio `PrecisionThread` worker. The ESP-IDF receive callback does only bounded frame copying into a FreeRTOS queue; protocol validation, protocol handlers and registered maintenance work are executed by the worker. This removes the former split where receive/reassembly state could be mutated on the ESP-NOW receive task while timeout maintenance was invoked independently from Arduino `loop()`.

The Threads infrastructure must therefore be initialized before `ESPNowTransport::Initialize()`. In normal ESPressio applications this is performed by `ThreadManager` alongside the other managed Threads.

The existing 0.8.x configuration names remain source-compatible and now configure the worker:

```cpp
ESPressio::ESPNow::ESPNowTransportConfig config;
config.ReceiveTaskStackSize = 8192;
config.ReceiveTaskPriority = 2;
config.ReceiveTaskCore = tskNO_AFFINITY;
config.ReceiveQueueLength = 12;
config.WorkerIterationIntervalMilliseconds = 5;
```

`WorkerIterationIntervalMilliseconds` is the minimum worker iteration interval. Incoming frames remain queued until the next permitted iteration and do not bypass the rate limit. The compatibility diagnostic `GetReceiveTaskMinimumFreeStackBytes()` now reports the minimum observed free stack for this worker.

Command transport timeout/reassembly maintenance is automatically registered with the ESP-NOW worker. Applications no longer need to call `ESPNowCommandTransport::Update()` from `loop()`; the method remains available as a source-compatible manual hook.

`ESPNowTransport::Send()` remains the compatibility boolean API. `SendDetailed()` returns an `ESPNowSendResult` containing a stable ESPressio failure class plus the native ESP-IDF `esp_err_t` value, and `GetLastSendResult()` exposes the most recent result for diagnostics.

## Wi-Fi and ESP-NOW coexistence

ESP-NOW and conventional WiFi are permitted to operate at the same time on ESP32 devices. They are not independent radios: both facilities use the same 2.4 GHz WiFi radio and therefore share its current channel and interface state.

Known coexistence cases:

- An ESP32 cannot maintain one simultaneous WiFi channel for infrastructure/AP traffic and a different simultaneous ESP-NOW channel. There is one WiFi radio/channel at any instant.
- When STA is associated with an infrastructure access point, that network determines the effective radio channel. ESP-NOW peers must communicate on that effective channel.
- `ESPNowTransportConfig::Channel = 0` and `ESPNowPeerConfig::Channel = 0` mean “follow the current WiFi channel” and are recommended when WiFi owns radio configuration.
- In AP+STA mode, the STA association has channel priority and the local SoftAP follows that channel.
- Active WiFi scans hop the shared radio through channels and can temporarily interrupt ESP-NOW traffic. Transient send failures or increased latency during scans are expected.
- WiFi association/reassociation, AP start/stop and channel transitions can create short ESP-NOW disruption windows.
- ESP-NOW peers are bound to a local interface (`WIFI_IF_STA` or `WIFI_IF_AP`). A wrong interface can produce one-way communication or `ESP_ERR_ESPNOW_IF` failures.

### Automatic interface handling

`ESPNowPeerConfig::Interface` defaults to `ESPNowWiFiInterface::Auto`. On ESP-IDF v5 and later, ESPressio records the local interface addressed by validated unicast ESP-NOW frames. If discovery initially added a peer using only broadcast information, the peer registration is corrected automatically when later unicast traffic reveals that the peer is addressing the other local interface.

If no learned interface exists, `Auto` selects AP in AP-only mode and STA in STA/AP+STA modes. Applications with a fixed topology can explicitly select `Station` or `AccessPoint`.

This automation addresses interface selection; it cannot create a second physical channel.

### Send diagnostics

A detailed send failure is classified as one of:

```text
NotInitialized
InvalidArgument
NoMemory
PeerNotFound
InterfaceMismatch
ChannelMismatch
Internal
Unknown
```

The native ESP-IDF error value is preserved as well. Existing `OnESPNowSendFailed(...)` observers continue to work; new observers can override `OnESPNowSendFailedDetailed(...)`. The ESP-Now Event bridge carries the same failure class and native code in `ESPNowSendFailedEvent`.

## Clock synchronization

`ESPNowClockSynchronizer` transports ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for sample validation, estimation and SystemClock discipline.

## Event integration

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

Event integration remains opt-in and is validated against Event 6.0.3.

## Command integration

```cpp
#include <ESPressio_ESPNowCommandTransport.hpp>
```

Command integration remains opt-in and is validated against Command 1.0.3. Command protocol v1 remains wire-compatible. Receive processing and periodic endpoint maintenance now share the ESP-NOW worker execution context; application-thread `Invoke()` calls cross a narrow synchronization boundary into that state safely.

## Security integration

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
```

Security integration remains opt-in and is validated against Security 0.4.2.

## Observable lifecycle

`IESPNowTransportObserver` is the synchronous lifecycle surface for initialization, shutdown, peers, sends and validated inbound ESPressio frames. The detailed send-failure callback added by #40 is additive and defaults to the original failure callback so existing observers remain compatible.

## Serializable 0.11.3 cascade generation

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

## Compatibility and release mutation

The version remains **0.8.3** intentionally. Issue #40 corrects the existing release in place rather than starting another downstream version cascade. The change adds Threads as a required dependency and changes the internal execution model from a raw receive task plus application-driven maintenance to a managed `PrecisionThread` worker. Existing wire framing, frame version, protocol IDs, clock synchronization payloads, Command protocol-v1 and Security framing are unchanged.

Because the existing 0.8.3 release is not externally consumed, this stability correction intentionally overrides normal versioning expectations and invalidates the previous 0.8.3 implementation when the release is mutated onto the corrected commit.

See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md), [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), and [CHANGELOG.md](CHANGELOG.md) for further details.
