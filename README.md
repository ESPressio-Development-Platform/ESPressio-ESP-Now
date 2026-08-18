# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications and hosts ESP-NOW-specific integrations for other ESPressio libraries.

The first release provides distributed **ESPressio System Clock synchronization** using the transport-independent clock-discipline engine introduced by ESPressio Timing 2.1.

## Latest Stable Version

The latest Stable Version is [0.1.0](https://github.com/Flowduino/ESPressio-ESPNow/releases/tag/0.1.0).

## Compatibility

ESPressio ESP-Now `0.1.0` targets the **ESP32 family under Arduino-ESP32**.

The implementation uses the native Espressif ESP-NOW and Wi-Fi APIs together with FreeRTOS queues/tasks provided by Arduino-ESP32.

Supported devices therefore include ESP32-family targets on which the installed Arduino-ESP32 version provides ESP-NOW support.

All ESP-NOW peers participating in a synchronization topology must operate on a compatible Wi-Fi channel.

The initial ESPressio wire format deliberately remains within the classic ESP-NOW `250` byte payload limit for broad interoperability, even on newer devices supporting larger ESP-NOW v2 payloads.

## ESPressio Development Platform

ESPressio ESP-Now is designed as the transport-specific ESP-NOW layer of the broader ESPressio ecosystem.

The responsibility boundary is:

```text
ESPressio Timing
    |
    +-- System Clock
    +-- synchronization samples
    +-- offset estimation
    +-- round-trip-delay estimation
    +-- clock discipline
    +-- slew / drift compensation
           ^
           |
           | transport-independent API
           |
ESPressio ESP-Now
    |
    +-- Wi-Fi / ESP-NOW initialization
    +-- peers / MAC addresses
    +-- packet framing
    +-- packet receive queue
    +-- synchronization request/response transport
```

ESPressio Timing does not know about ESP-NOW.

ESPressio ESP-Now gathers timing measurements and submits them to Timing.

## Dependencies

Version `0.1.0` requires:

```text
ESPressio Timing >= 2.1.0
Arduino-ESP32
```

ESPressio Timing provides the transport-independent synchronization and System Clock APIs.

No ESPressio Serializable dependency is required by this release.

## Namespace

The public API is contained within:

```cpp
ESPressio::ESPNow
```

The principal types introduced in `0.1.0` are:

```cpp
ESPNowTransport
ESPNowTransportConfig
ESPNowPeerConfig
ESPNowReceivedFrame
ESPNowProtocol

MacAddress

ESPNowClockSynchronizer
ESPNowClockSynchronizationConfig
ESPNowClockSynchronizationMode
```

## Header Structure

The normal umbrella header is:

```cpp
#include <ESPressio_ESPNow.hpp>
```

Individual facilities can also be included directly:

```cpp
#include <ESPressio_ESPNowTypes.hpp>
#include <ESPressio_ESPNowTransport.hpp>
#include <ESPressio_ESPNowClockSynchronizer.hpp>
```

## ESP-NOW Transport

`ESPNowTransport` is the common ESP-NOW transport foundation.

It is implemented as one process-wide transport because Espressif's ESP-NOW receive callback registration is itself global to the ESP-NOW subsystem.

Access it through:

```cpp
auto& transport =
    ESPressio::ESPNow::
        ESPNowTransport::
            GetInstance();
```

Initialize it with:

```cpp
ESPNow::ESPNowTransportConfig config;

config.Channel = 0;

bool initialized =
    ESPNow::ESPNowTransport::
        GetInstance().
        Initialize(config);
```

When:

```cpp
config.InitializeWiFi = true;
```

the transport configures:

```cpp
WiFi.mode(WIFI_STA);
```

before initializing ESP-NOW.

A channel value of:

```cpp
0
```

keeps the currently selected Wi-Fi channel.

A non-zero channel explicitly selects that channel before ESP-NOW initialization.

### Receive Processing

ESP-NOW receive callbacks execute in the Wi-Fi task and must not perform lengthy application work.

ESPressio ESP-Now therefore captures only the information required by the received frame inside the callback:

```text
source MAC
raw receive timestamp
frame length
frame bytes
```

and posts the data into a fixed-size FreeRTOS queue without blocking.

A lower-priority ESPressio ESP-Now receive task subsequently performs:

```text
wire-format validation
protocol routing
clock synchronization processing
application protocol callbacks
```

This prevents clock-discipline calculations or application callbacks from running in the Wi-Fi task.

### Transport Configuration

The transport can be configured through:

```cpp
ESPNowTransportConfig
```

with:

```cpp
InitializeWiFi

Channel

ReceiveTaskStackSize
ReceiveTaskPriority
ReceiveTaskCore

ReceiveQueueLength
```

The defaults are appropriate for ordinary synchronization use.

## Peer Management

Peers are represented using:

```cpp
ESPNowPeerConfig
```

For example:

```cpp
static const uint8_t RemoteMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x01
};

ESPNow::ESPNowPeerConfig peer;

peer.Address =
    ESPNow::MacAddress(
        RemoteMac
    );

peer.Channel = 0;
peer.Encrypt = false;

ESPNow::ESPNowTransport::
    GetInstance().
    AddPeer(peer);
```

The transport avoids adding a peer that is already registered with ESP-NOW.

Peers can be removed using:

```cpp
transport.RemovePeer(
    address
);
```

## ESPressio ESP-NOW Wire Format

ESPressio packets use a small internal frame header containing:

```text
magic
wire version
protocol identifier
payload length
```

The initial wire version is:

```text
1
```

Versioning the common envelope gives future ESPressio ESP-NOW functionality a consistent transport foundation.

`0.1.0` reserves protocol identifier:

```text
1
```

for System Clock synchronization.

User/application protocols can be assigned from:

```cpp
ESPNowProtocol::UserBase
```

onward.

## Protocol Handlers

Additional ESP-NOW-specific ESPressio implementations can register handlers with:

```cpp
transport.RegisterProtocolHandler(
    protocol,
    callback
);
```

The callback receives:

```cpp
ESPNowReceivedFrame
```

including:

```text
Source
ReceiveMonotonicNanoseconds
Protocol
PayloadLength
Payload
```

Handlers execute in the ESPressio ESP-Now receive-processing task rather than in the Wi-Fi receive callback.

This API is intended to provide the basis for future ESP-NOW-specific functionality such as:

```text
Serializable object transport
Event transport
peer discovery
request/response
device presence
distributed control
```

without duplicating low-level ESP-NOW initialization and receive handling.

---

# System Clock Synchronization

`0.1.0` adds:

```cpp
ESPNowClockSynchronizer
```

which transports ESPressio Timing 2.1 synchronization exchanges over ESP-NOW.

The synchronizer does **not** implement clock-discipline mathematics itself.

It communicates through:

```cpp
Timing::
    IClockSynchronizationTarget<
        Timing::ClockTick
    >
```

and submits completed four-timestamp exchanges to ESPressio Timing.

By default the synchronization target is:

```cpp
Timing::SystemClock<>::
    GetInstance()
```

so the normal shared ESPressio System Clock is disciplined.

## Synchronization Roles

A synchronizer can operate as:

```cpp
ESPNowClockSynchronizationMode::
    Disabled
```

```cpp
ESPNowClockSynchronizationMode::
    Client
```

```cpp
ESPNowClockSynchronizationMode::
    Reference
```

or:

```cpp
ESPNowClockSynchronizationMode::
    ClientAndReference
```

### Reference

A reference device answers synchronization requests.

Multiple client ESP32 units can synchronize against one reference:

```text
                   Reference
                    ESP32 A
                  /    |    \
                 /     |     \
                v      v      v
             Client  Client  Client
             ESP32 B ESP32 C ESP32 D
```

The reference itself does not need an absolute wall-clock source.

If its System Clock starts from an arbitrary local epoch, all clients still become synchronized to that same epoch.

If absolute time is desired, the reference can first establish its System Clock from another source such as:

```text
NTP
RTC
GPS
host-provided time
```

and ESP-NOW clients will then synchronize to that timeline.

### Client

A client identifies one reference MAC address and periodically performs synchronization exchanges with that device.

```cpp
ESPNowClockSynchronizationConfig config;

config.Mode =
    ESPNowClockSynchronizationMode::
        Client;

config.ReferencePeer =
    MacAddress(
        referenceMac
    );

config.
    SynchronizationIntervalMilliseconds =
        1000;
```

The client application calls:

```cpp
clockSynchronizer.Update();
```

regularly.

`Update()` sends a new exchange when the configured interval has elapsed.

An exchange can also be requested immediately:

```cpp
clockSynchronizer.
    RequestSynchronization();
```

### ClientAndReference

Dual-role mode allows hierarchical synchronization.

For example:

```text
              Primary Reference
                   ESP32 A
                       |
                       v
                 ESP32 B
           Client + Reference
                 /       \
                v         v
             ESP32 C   ESP32 D
```

ESP32 B synchronizes upstream to A while serving as the reference for C and D.

This makes larger synchronization topologies possible without requiring every device to communicate directly with the primary reference.

A device should have one clear upstream reference. Two devices should not continuously discipline each other in a circular topology.

---

# Four-Timestamp Exchange

Clock synchronization uses the four-timestamp model expected by ESPressio Timing 2.1:

```text
Client                                     Reference

T1 request transmit
    |
    |------------- request ------------------->
    |
    |                                 T2 receive
    |                                 T3 transmit
    |
    <------------ response -------------------
    |
T4 response receive
```

The completed exchange produces:

```cpp
Timing::
    ClockSynchronizationSample<
        Timing::ClockTick
    >
```

containing:

```cpp
LocalRequestTransmitTime
RemoteRequestReceiveTime
RemoteResponseTransmitTime
LocalResponseReceiveTime
```

That sample is submitted to the Timing clock-discipline engine.

Timing then owns:

```text
clock-offset estimation
round-trip-delay estimation
sample filtering
sample rejection
phase slew
drift estimation
drift compensation
synchronization state
```

## Receive Timestamp Accuracy

ESP-NOW receive callbacks run asynchronously in the Wi-Fi task.

The transport records a raw monotonic timestamp immediately when the callback receives the frame.

The frame is then queued for later processing.

When the clock synchronization handler runs, it converts that earlier raw timestamp back into the current disciplined System Clock domain by measuring the elapsed monotonic time since packet receipt.

Conceptually:

```text
callback:
    rawReceive = monotonicNow()

worker later:
    systemNow = SystemClock synchronization timestamp
    rawNow    = monotonicNow()

    systemAtReceive =
        systemNow -
        (rawNow - rawReceive)
```

This avoids treating FreeRTOS queue/worker latency as network propagation delay.

## T1 and T3 Placement

The client captures `T1` immediately before sending its request.

The reference captures `T3` only after any required peer-registration work and immediately before sending its response.

This keeps local application processing time out of the timing exchange as much as practical without hardware-level ESP-NOW timestamping.

## Sequence Numbers

Every synchronization request has a sequence number.

The client accepts a response only when it matches the currently outstanding synchronization exchange.

Once a response has been consumed, duplicate or delayed responses carrying the same sequence are ignored.

---

# Clock Discipline

The correction strategy is selected using the Timing 2.1 policy:

```cpp
config.AdjustmentMode =
    Timing::
        ClockSynchronizationAdjustmentMode::
            SlewOnly;
```

Available modes are:

```text
SlewOnly
StepIfUnsynchronized
StepAlways
```

`SlewOnly` is the default and is recommended when deadline-driven components such as:

```cpp
PrecisionThread<>
PrecisionEventThread<>
```

are active.

The ESP-NOW library only selects and transports the synchronization exchange.

The actual clock correction is performed by ESPressio Timing.

---

# Synchronization Status

The current Timing synchronization status can be obtained through:

```cpp
auto status =
    clockSynchronizer.
        GetSynchronizationStatus();
```

Status includes values such as:

```text
State
LastMeasuredOffsetNanoseconds
FilteredOffsetNanoseconds
PendingPhaseCorrectionNanoseconds
AppliedCorrectionNanoseconds
LastRoundTripDelayNanoseconds
EstimatedDriftPpm
AcceptedSampleCount
RejectedSampleCount
```

This makes it possible to expose synchronization quality through application diagnostics or telemetry.

---

# Example: Reference

```cpp
#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>

using namespace ESPressio;

ESPNow::ESPNowClockSynchronizer
    synchronizer;

void setup() {
    Serial.begin(115200);

    ESPNow::ESPNowTransport::
        GetInstance().
        Initialize();

    Serial.println(
        WiFi.macAddress()
    );

    ESPNow::
        ESPNowClockSynchronizationConfig
            config;

    config.Mode =
        ESPNow::
            ESPNowClockSynchronizationMode::
                Reference;

    synchronizer.Initialize(
        config
    );
}

void loop() {
    delay(1000);
}
```

The reference does not need to call:

```cpp
Update()
```

because incoming synchronization requests are handled by the transport receive task.

---

# Example: Client

```cpp
#include <ESPressio_ESPNow.hpp>

using namespace ESPressio;

static const uint8_t ReferenceMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x01
};

ESPNow::ESPNowClockSynchronizer
    synchronizer;

void setup() {
    ESPNow::ESPNowTransport::
        GetInstance().
        Initialize();

    ESPNow::
        ESPNowClockSynchronizationConfig
            config;

    config.Mode =
        ESPNow::
            ESPNowClockSynchronizationMode::
                Client;

    config.ReferencePeer =
        ESPNow::MacAddress(
            ReferenceMac
        );

    synchronizer.Initialize(
        config
    );

    synchronizer.
        RequestSynchronization();
}

void loop() {
    synchronizer.Update();

    delay(10);
}
```

---

# Included Examples

Version `0.1.0` includes:

```text
examples/
├── ClockReference/
│   └── ClockReference.ino
│
├── ClockClient/
│   └── ClockClient.ino
│
└── BidirectionalClockSync/
    └── BidirectionalClockSync.ino
```

`BidirectionalClockSync` demonstrates the hierarchical `ClientAndReference` role rather than circular two-device synchronization.

---

# PlatformIO

Add:

```ini
lib_deps =
    flowduino/ESPressio-ESPNow@^0.1.0
```

ESPressio Timing `>=2.1.0` is declared as a dependency.

A typical ESP32 environment might be:

```ini
[env:esp32]
platform = espressif32
framework = arduino
board = esp32dev

lib_deps =
    flowduino/ESPressio-ESPNow@^0.1.0
```

When multiple ESP32s are not associated with a Wi-Fi access point, configure them to use the same explicit ESP-NOW channel.

When ESP-NOW is used alongside a normal Wi-Fi connection, peer/channel configuration must remain compatible with the channel selected by that connection.

---

# Arduino IDE

Install the ESPressio ESP-Now library together with its dependency:

```text
ESPressio Timing >= 2.1.0
```

Then include:

```cpp
#include <ESPressio_ESPNow.hpp>
```

---

# Design Constraints

## One ESPNowTransport

ESP-NOW callback registration is global to the ESP-NOW subsystem.

`ESPNowTransport` is therefore deliberately a singleton.

Higher-level ESPressio ESP-NOW facilities should share this transport rather than each independently initializing ESP-NOW and replacing callbacks.

## One Handler Per Protocol

Version `0.1.0` allows one active handler for each ESPressio ESP-NOW protocol identifier.

Registering another handler for the same identifier replaces the previous one.

## Receive Queue

The Wi-Fi callback never waits for receive-queue capacity.

If the receive queue is full, an incoming ESPressio frame is dropped rather than blocking the Wi-Fi task.

Applications experiencing sustained queue overflow should increase:

```cpp
ReceiveQueueLength
```

or reduce protocol processing latency.

## Application-Level Delivery

A successful call to:

```cpp
esp_now_send()
```

means the frame was accepted for ESP-NOW transmission; it does not by itself provide an application-level acknowledgement.

The clock synchronization protocol naturally provides a response to each request, and synchronization requests whose responses never arrive simply do not produce Timing samples.

Future general-purpose reliable ESP-NOW transports may implement their own acknowledgement/retry semantics above the transport layer.

---

# Version 0.1.0 Scope

The initial release intentionally focuses on establishing the architecture and one production-useful integration.

Included:

```text
ESP-NOW initialization
shared transport
peer management
versioned ESPressio framing
receive-task handoff
protocol dispatch
System Clock synchronization
client/reference roles
hierarchical synchronization
Timing 2.1 integration
```

Potential future additions belong in this ESP-NOW-specific library rather than in the libraries whose functionality they transport:

```text
Serializable object transport
Serializable Event transport
Event routing
peer discovery
device presence
request/response abstractions
reliable delivery
broadcast services
encrypted peer helpers
network topology helpers
```

---

# Summary

ESPressio ESP-Now `0.1.0` establishes a reusable ESP-NOW transport layer for the ESPressio ecosystem.

The first integration synchronizes the shared ESPressio System Clock across ESP32 devices while preserving the architecture established in ESPressio Timing 2.1:

```text
ESP-NOW handles communication.

ESPressio Timing handles time.
```

The resulting system supports:

```text
one reference -> many clients

hierarchical references

four-timestamp synchronization

latency-aware clock-offset estimation

clock slew and drift correction

shared System Clock synchronization
```

without introducing ESP-NOW-specific code into ESPressio Timing itself.
