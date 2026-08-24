# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the ESPressio Development Platform.

## Current Version — 0.8.3

ESPressio ESP-Now **0.8.3** is the current release generation aligned with Serializable 0.11.3. The release has been corrected in place under issue #40 to harden coexistence with normal ESP32 WiFi operation while preserving the 0.8.3 version number and existing ESP-NOW wire format.

## Dependencies

Required:

```text
ESPressio Timing >= 2.2.8 < 3.0.0
ESPressio Observable >= 3.0.2 < 4.0.0
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

`ESPNowTransport::Send()` remains the compatibility boolean API. `SendDetailed()` returns an `ESPNowSendResult` containing a stable ESPressio failure class plus the native ESP-IDF `esp_err_t` value, and `GetLastSendResult()` exposes the most recent result for diagnostics.

## Wi-Fi and ESP-NOW coexistence

ESP-NOW and conventional WiFi are permitted to operate at the same time on ESP32 devices. They are not independent radios, however: both facilities use the same 2.4 GHz WiFi radio and therefore share its current channel and interface state.

The important operating rules are:

- An ESP32 cannot maintain one simultaneous WiFi channel for infrastructure/AP traffic and a different simultaneous ESP-NOW channel. There is one WiFi radio/channel at any instant.
- When the station is associated with an infrastructure access point, that infrastructure network determines the effective radio channel. ESP-NOW peers must communicate on that same effective channel.
- `ESPNowTransportConfig::Channel = 0` and `ESPNowPeerConfig::Channel = 0` mean "use/follow the current WiFi channel" and are the recommended settings when WiFi owns radio configuration.
- In AP+STA mode, the station connection has channel priority. The local SoftAP follows the station's effective channel once associated.
- Active WiFi scans temporarily hop the shared radio through other channels. ESP-NOW send/receive opportunities on the home channel can therefore be interrupted during a scan. Applications should expect transient ESP-NOW send failures or increased latency while scans are running and should retry appropriately.
- WiFi association/reassociation, AP start/stop, and channel transitions can create short ESP-NOW disruption windows while the radio changes state.
- ESP-NOW peers are associated with a local WiFi interface (`WIFI_IF_STA` or `WIFI_IF_AP`). Hard-coding the wrong interface can produce one-way communication or `ESP_ERR_ESPNOW_IF` send failures.

### Automatic interface handling

`ESPNowPeerConfig::Interface` defaults to `ESPNowWiFiInterface::Auto`. On ESP-IDF v5 and later, ESPressio records the local interface addressed by validated unicast ESP-NOW frames. If discovery initially added a peer using only broadcast information, the peer registration is corrected automatically when later unicast traffic reveals that the peer is addressing the other local interface.

If no learned interface exists, `Auto` selects AP in AP-only mode and STA in STA/AP+STA modes. Applications with a fixed topology can explicitly select `Station` or `AccessPoint`.

This automation addresses interface selection; it cannot create a second physical channel. Channel coexistence remains governed by the single ESP32 WiFi radio.

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

The native ESP-IDF error value is preserved as well. Existing `OnESPNowSendFailed(...)` observers continue to work; new observers can override `OnESPNowSendFailedDetailed(...)` for the richer result. The ESP-Now Event bridge carries the same failure class and native code in `ESPNowSendFailedEvent`.

## Clock synchronization

`ESPNowClockSynchronizer` transports ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for sample validation, estimation and SystemClock discipline. ESP-Now 0.8.3 validates this surface against Timing 2.2.8.

## Event integration

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

Event integration remains opt-in and is validated against Event 6.0.3. ESP-Now continues to own its concrete Event transport and the Event representation of ESP-Now lifecycle observations, preserving the one-way dependency `ESP-Now -> Event`.

## Command integration

```cpp
#include <ESPressio_ESPNowCommandTransport.hpp>
```

Command integration remains opt-in and is validated against Command 1.0.3. ESP-Now Command protocol v1 remains wire-compatible: scalar `CommandValue` instances are normalized at the existing string-valued wire boundary and reconstructed as compatible invocation values on receive.

## Security integration

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
```

Security integration remains opt-in and is validated against Security 0.4.2. `ESPNowSecureTransport` owns ESP-NOW-specific framing/fragmentation while ESPressio Security owns authenticated encryption, sender/session identity, protocol binding and replay protection.

## Observable lifecycle

`IESPNowTransportObserver` is the synchronous lifecycle surface for initialization, shutdown, peers, sends and validated inbound ESPressio frames. The detailed send-failure callback added by #40 is additive and defaults to the original failure callback so existing observers remain compatible. `ESPNowTransportEventBridge` optionally converts those observations into asynchronous Events.

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

## Compatibility

The version remains **0.8.3** intentionally. Issue #40 corrects the existing release in place rather than starting another downstream version cascade. Existing `Send()` callers and observers implementing the original callback remain source-compatible. `ESPNowPeerConfig` gains an additive interface-selection field whose default is automatic. ESP-NOW wire framing, frame version, protocol IDs, clock synchronization payloads, Command protocol-v1 and Security framing are unchanged.

See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md), [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), and [CHANGELOG.md](CHANGELOG.md) for further details.
