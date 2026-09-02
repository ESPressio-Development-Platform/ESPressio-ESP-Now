# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the ESPressio Development Platform.

## Current Version — 0.8.3

ESPressio ESP-Now **0.8.3** is the current release generation aligned with Serializable 0.11.3. The current development work corrects the existing release in place under issues #39/#40 while preserving the 0.8.3 version number and existing ESP-NOW wire framing.

## Dependencies

Required:

```text
ESPressio Radio main
ESPressio Timing main
ESPressio Observable main
ESPressio Threads main
Arduino-ESP32
```

Optional integrations:

```text
ESPressio WiFi main (shared-radio coordination)
ESPressio Event main
ESPressio Command main
ESPressio Security main
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of WiFi, Event, Command and Security includes. The WiFi coordinator is explicitly opt-in through `ESPressio_ESPNowWiFiCoordinator.hpp`.

## Installation

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-ESP-Now.git#optimisation/54-move-owned-radio-packets
    https://github.com/ESPressio-Development-Platform/ESPressio-Radio.git#main
    https://github.com/ESPressio-Development-Platform/ESPressio-Timing.git#main
    https://github.com/ESPressio-Development-Platform/ESPressio-Observable.git#main
    https://github.com/ESPressio-Development-Platform/ESPressio-Threads.git#main
```

Add WiFi, Event, Command and Security from their `main` branches only when their integrations are selected.

## Core transport and worker ownership

```cpp
#include <ESPressio_ESPNow.hpp>

auto& transport = ESPressio::ESPNow::ESPNowTransport::GetInstance();
transport.Initialize();
```

ESP-Now owns a rate-limited ESPressio `PrecisionThread` worker. The ESP-IDF receive callback performs bounded frame copying into a FreeRTOS queue; protocol validation, protocol handlers and registered maintenance work execute on the worker. This removes the former split where receive/reassembly state could be mutated on the ESP-NOW receive task while timeout maintenance was invoked independently from Arduino `loop()`.

The Threads infrastructure must therefore be initialized before `ESPNowTransport::Initialize()`. In normal ESPressio applications this is performed by `ThreadManager` alongside the other managed Threads.

The existing 0.8.x configuration names remain source-compatible and configure the worker:

```cpp
ESPressio::ESPNow::ESPNowTransportConfig config;
config.ReceiveTaskStackSize = 8192;
config.ReceiveTaskPriority = 2;
config.ReceiveTaskCore = tskNO_AFFINITY;
config.ReceiveQueueLength = 12;
config.WorkerIterationIntervalMilliseconds = 5;
```

`WorkerIterationIntervalMilliseconds` is the minimum worker iteration interval. Incoming frames remain queued until the next permitted iteration and do not bypass the rate limit. The compatibility diagnostic `GetReceiveTaskMinimumFreeStackBytes()` reports the minimum observed free stack for this worker.

Command transport timeout/reassembly maintenance is automatically registered with the ESP-NOW worker. Applications no longer need to call `ESPNowCommandTransport::Update()` from `loop()`; the method remains available as a source-compatible manual hook.

`ESPNowTransport::Send()` remains the compatibility boolean API. `SendDetailed()` returns an `ESPNowSendResult` containing a stable ESPressio failure class plus the native ESP-IDF `esp_err_t` value, and `GetLastSendResult()` exposes the most recent result for diagnostics.

## WiFi and ESP-NOW coexistence

ESP-NOW and conventional WiFi share one physical ESP32 2.4 GHz radio. They therefore cannot be managed as independent radio subsystems. When ESPressio WiFi is present, **WiFi is the authority for radio mode and channel and ESP-Now follows that state**.

Known coexistence rules:

- An ESP32 cannot maintain one simultaneous WiFi channel for infrastructure/AP traffic and a different simultaneous ESP-NOW channel.
- When STA associates with an infrastructure access point, that network determines the effective radio channel.
- `ESPNowTransportConfig::Channel = 0` and `ESPNowPeerConfig::Channel = 0` mean “follow the current WiFi radio channel” and are recommended when WiFi owns radio configuration.
- In AP+STA mode, a connected infrastructure STA normally has channel priority; a fallback SoftAP shares the same physical channel.
- Active WiFi scans hop the shared radio through channels. ESP-NOW is intentionally treated as temporarily unavailable during this window when the coordinator is used.
- AP/STA/APSTA transitions can invalidate native peer interface state even though the ESP-NOW logical topology has not changed.

### Optional direct WiFi coordination

Applications using ESPressio WiFi should opt into the low-level coordinator:

```cpp
#include <ESPressio_ESPNow.hpp>
#include <ESPressio_ESPNowWiFiCoordinator.hpp>
#include <ESPressio_WiFi.hpp>

ESPressio::ESPNow::ESPNowTransport& transport =
    ESPressio::ESPNow::ESPNowTransport::GetInstance();

ESPressio::ESPNow::ESPNowWiFiCoordinator coordinator(
    transport,
    wifiManager
);

void setup() {
    // Configure/start WiFi and initialize Threads first.

    ESPressio::ESPNow::ESPNowTransportConfig config;
    config.InitializeWiFi = false; // WiFi owns the radio.
    config.Channel = 0;            // Follow WiFi's channel.
    transport.Initialize(config);

    coordinator.Initialize();
}
```

The coordinator registers directly with WiFi's dedicated `IWiFiRadioObserver` infrastructure API; it does not route radio coordination through application Events. WiFi publishes authoritative native radio snapshots containing mode, active interfaces, STA connection state, scan state, current channel and AP/STA MAC addresses.

The coordinator responds synchronously to that lifecycle:

```text
WiFi transition begins
    -> ESP-NOW marks radio temporarily unavailable

WiFi changes AP/STA/APSTA/channel state

WiFi transition completes
    -> ESP-NOW resolves the current Auto interface
    -> managed peers are reconciled against the native ESP-NOW table
    -> transmission resumes

WiFi scan begins
    -> ESP-NOW transmission is suspended

WiFi scan completes
    -> current channel/interface are reconciled
    -> transmission resumes
```

Normal transitions use the least-disruptive peer reconciliation first. A complete native `esp_now_deinit()` / `esp_now_init()` rebuild is retained as an escalation path only when the driver rejects a lightweight rebind. Logical peer configuration, protocol handlers, maintenance handlers and the ESPressio worker remain owned by the transport across such a rebuild.

### Dynamic `Auto` interface handling

`ESPNowPeerConfig::Interface` defaults to `ESPNowWiFiInterface::Auto`. `Auto` is now a **lifetime policy**, not a one-time choice made when the peer is first added.

Resolution follows this model:

```text
AP-only                  -> AccessPoint
STA-only                 -> Station
AP+STA, STA connected    -> Station
AP+STA, STA disconnected -> AccessPoint when AP is active
```

When the WiFi coordinator is attached, its authoritative radio binding takes precedence over fallback inference. Validated unicast receive metadata can still provide an interface hint for an Auto peer. Explicit `Station` and `AccessPoint` peers never silently migrate.

The transport keeps a bounded ESPressio logical peer registry separate from the native ESP-NOW peer table. `ReconcileManagedPeers()` can therefore reprogram native peers after WiFi changes without requiring applications to reconstruct their logical topology.

### Radio availability and send diagnostics

During an explicit WiFi radio transition or scan, `SendDetailed()` can report:

```text
RadioUnavailable
```

rather than entering the ESP-IDF driver during a known disruptive window.

Other detailed send failures include:

```text
NotInitialized
InvalidArgument
NoMemory
PeerNotFound
InterfaceMismatch
ChannelMismatch
RadioUnavailable
Internal
Unknown
```

The native ESP-IDF error value is preserved as well. Existing `OnESPNowSendFailed(...)` observers remain compatible; detailed observers can override `OnESPNowSendFailedDetailed(...)`.

### Local endpoint MAC

STA and AP interfaces have different MAC addresses. `ESPNowTransport::GetLocalEndpointAddress()` reports the MAC of the currently resolved ESP-NOW endpoint so discovery layers do not need to assume that the STA MAC is always the transmitting identity.

Applications that need a stable logical node identity across AP/STA endpoint migration should keep that stable identity separate from the current transport endpoint. The transport deliberately exposes endpoint state rather than conflating those concepts.

## Clock synchronization

`ESPNowClockSynchronizer` transports ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for sample validation, estimation and SystemClock discipline.

## Event integration

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

Event integration remains opt-in and is validated against Event `main`.

## Command integration

```cpp
#include <ESPressio_ESPNowCommandTransport.hpp>
```

Command integration remains opt-in and is validated against Command `main`. Command protocol v1 remains wire-compatible. Receive processing and periodic endpoint maintenance share the ESP-NOW worker execution context; application-thread `Invoke()` calls cross a narrow synchronization boundary into that state safely.

## Security integration

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
```

Security integration remains opt-in and is validated against Security `main`.

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

The version remains **0.8.3** intentionally. Issues #39/#40 correct the existing development release in place rather than starting another downstream version cascade. Existing wire framing, frame version, protocol IDs, clock synchronization payloads, Command protocol-v1 and Security framing are unchanged.

Because this 0.8.3 generation is not externally consumed, these corrections intentionally override normal versioning expectations while the architecture is stabilized.

See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md), [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), and [CHANGELOG.md](CHANGELOG.md) for further details.
