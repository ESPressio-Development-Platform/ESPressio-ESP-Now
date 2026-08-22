# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

## Current Version — 0.8.0

ESPressio ESP-Now **0.8.0** hardens the shared receive task for integrated protocol workloads. The default receive-task stack is increased from 4096 to 8192 bytes, and `ESPNowTransport` now exposes `GetReceiveTaskMinimumFreeStackBytes()` so hardware tests and applications can inspect observed receive-task stack headroom. Existing transport configuration remains source-compatible, and ESP-NOW wire framing and protocol identifiers are unchanged.

The public Event integration header and class names remain:

```cpp
ESPressio::ESPNow
```

Principal public types include:

```text
ESPNowTransport
ESPNowTransportConfig
ESPNowPeerConfig
ESPNowReceivedFrame
ESPNowProtocol
ESPNowPeerLivenessTracker
ESPNowPeerLivenessState
MacAddress
ESPNowClockSynchronizer
ESPNowClockSynchronizationConfig
ESPNowClockSynchronizationMode
ESPNowEventTransport               (optional Event)
ESPNowTransportEventBridge         (optional Event)
ESPNowCommandTransport             (optional Command)
ESPNowSecureTransport              (optional Security)
ESPNowSecurityProtocol
```

## Dependencies

Required:

```text
ESPressio Timing >= 2.2.4 < 3.0.0
ESPressio Observable >= 3.0.1 < 4.0.0
Arduino-ESP32
```

Optional:

```text
Event integration
    ESPressio Event >= 6.0.0 < 7.0.0

Optional Command integration
    ESPressio Command >= 1.0.0 < 2.0.0

Secure Transport
    ESPressio Security >= 0.3.0 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command and Security includes.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), and [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md).

## Installation

Core transport/Timing use:

```ini
lib_deps =
    flowduino/ESPressio-ESP-Now@^0.8.0
    flowduino/ESPressio-Timing@^2.2.4
    flowduino/ESPressio-Observable@^3.0.1

build_flags =
    -std=gnu++17
    -frtti

build_unflags =
    -std=gnu++11
    -fno-rtti
```

Add Event, Command and Security only when the corresponding adapters are selected.

# Header structure

Core umbrella:

```cpp
#include <ESPressio_ESPNow.hpp>
```

Optional integrations:

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
#include <ESPressio_ESPNowCommandTransport.hpp>
#include <ESPressio_ESPNowSecureTransport.hpp>
```

# ESP-NOW Transport

`ESPNowTransport` is process-wide because Espressif's ESP-NOW receive callback is global to the subsystem.

```cpp
auto& transport =
    ESPressio::ESPNow::ESPNowTransport::GetInstance();

transport.Initialize();
```

Incoming Wi-Fi callback data is copied into a bounded FreeRTOS queue. Protocol validation and application handlers run later on the ESPressio receive task, avoiding expensive application work inside the Wi-Fi callback.

The default receive-task stack is **8192 bytes** in 0.8.0. Applications with unusual protocol-handler depth may still override it explicitly:

```cpp
ESPressio::ESPNow::ESPNowTransportConfig config;
config.ReceiveTaskStackSize = 12288;
transport.Initialize(config);
```

The receive task executes registered protocol handlers synchronously, so integrated Command, Event, Security, Observable and diagnostic work contributes to the task's stack requirement. Applications and hardware stress tests can inspect the minimum unused receive-task stack observed since initialization:

```cpp
const uint32_t minimumFreeBytes =
    transport.GetReceiveTaskMinimumFreeStackBytes();
```

The diagnostic returns `0` when the receive task is unavailable, including before initialization and after shutdown.

Every successfully validated inbound ESPressio frame is also surfaced to transport observers. This is deliberately separate from protocol delivery: observers can use frame arrival as operational/liveness evidence while the registered protocol handler remains authoritative for consuming the payload.

## Command 1.x compatibility

Command 1.0.0 allows structured invocations to retain native scalar `CommandValue` types. The existing ESP-Now Command protocol remains **version 1** and intentionally preserves its historical string-valued wire representation:

```text
CommandValue -> ToString() -> ESP-Now Command protocol-v1 string
protocol-v1 string -> string-backed CommandValue
```

This means typed invocations such as integer and boolean values can be submitted through the ESP-Now Command API without breaking existing protocol-v1 peers. The receiving Command Registry remains responsible for typed parameter conversion and validation. Native scalar type identity is not carried across protocol v1, and null values are rejected because protocol v1 has no null representation.

## Event integration ownership

The dependency direction remains:

```text
Event 6.0.0
    ^
    |
    | optional
    |
ESP-Now 0.8.0
    +-- ESPNowEventTransport
    +-- ESPNow lifecycle Event types
    +-- ESPNowTransportEventBridge
```

`ESPNowTransportEventBridge` observes `ESPNowTransport` lifecycle notifications and publishes their Event representations. Because those concepts belong to ESP-Now, the bridge and Event types are owned by ESP-Now.

Add a peer using `ESPNowPeerConfig`:

```cpp
ESPressio::ESPNow::ESPNowPeerConfig peer;
peer.Address = ESPressio::ESPNow::MacAddress(remoteMac);
peer.Channel = 0;
peer.Encrypt = false;

transport.AddPeer(peer);
```

`peer.Encrypt` controls native ESP-NOW link encryption and is independent of ESPressio Security application/transport-layer protection.

0.8.0 does not change ESP-NOW radio framing, Event transport semantics, clock synchronization, peer-liveness behavior, Security transport semantics, or the ESP-Now Command protocol-v1 wire layout. Existing callers remain source-compatible; the new stack diagnostic is additive.

`ESPNowPeerLivenessTracker` classifies known peers as:

```text
Alive
    recent discovery or validated ESPressio traffic

Suspect
    expected discovery evidence was missed, but the peer is not yet
    considered genuinely unavailable

Expired
    the hard-expiry interval elapsed without valid evidence
```

## Final coordinated generation

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
ESP-Now       0.8.0
Serial        0.7.1
```

## Dependency documentation

See **[ESPressio Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.md)** for the complete 0.8.0 ESP-Now dependency direction and coordinated release generation. Downstream ESP-Now version baselines are updated separately as part of the release cascade.

Higher-level peer managers should use `Suspect` as a warning state and reserve destructive removal for `Expired`.

# Wire format and protocol allocation

The common ESPressio ESP-NOW outer frame carries:

```text
magic
wire version
protocol identifier
payload length
```

Protocol allocation is:

```text
1      Clock Synchronization
2      Event Transport
3      Command Transport
4      Secure Transport
64+    User/Application protocols
```

This lets multiple ESPressio protocols safely share one process-wide ESP-NOW transport.

# System Clock synchronization

`ESPNowClockSynchronizer` carries ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for offset/RTT estimation, filtering, phase correction, drift estimation and System Clock discipline.

Supported roles are:

```text
Disabled
Client
Reference
ClientAndReference
```

A Client periodically exchanges the four synchronization timestamps with a configured Reference. `ClientAndReference` supports hierarchical deployments where a synchronized node also serves downstream clients.

```cpp
ESPressio::ESPNow::ESPNowClockSynchronizationConfig config;
config.Mode =
    ESPressio::ESPNow::ESPNowClockSynchronizationMode::Client;
config.ReferencePeer =
    ESPressio::ESPNow::MacAddress(referenceMac);
config.SynchronizationIntervalMilliseconds = 1000;

ESPressio::ESPNow::ESPNowClockSynchronizer synchronizer;
synchronizer.Initialize(config);
```

Call `Update()` regularly when operating in a Client-capable mode.

The architectural boundary is intentional:

```text
ESP-Now
    captures/transports timestamps

Timing
    validates samples and disciplines SystemClock
```

# Event Transport

`ESPNowEventTransport` is an optional concrete ESPressio Event transport for Serializable Events. It supports multiple destination peers and bounded Event fragmentation/reassembly.

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
```

Event owns the generic Event Transport abstraction and routing semantics; ESP-Now owns the concrete radio transport.

The relationship is now strictly one-way:

```text
ESP-Now Event integration - - -> Event 6.x
```

# ESP-Now lifecycle Events

0.6.0 also owns the Event representation of its own transport lifecycle:

```cpp
#include <ESPressio_ESPNowEvents.hpp>
#include <ESPressio_ESPNowTransportEventBridge.hpp>
```

Initialize the bridge when transport lifecycle facts should also be dispatched asynchronously as Events.

The bridge consumes the existing `IESPNowTransportObserver` callbacks for initialization/shutdown, peer addition/removal and send success/failure. Core transport observation remains synchronous; Event representation is opt-in.

This placement eliminates the former Event↔ESP-Now reciprocal optional dependency.

# Command Transport

`ESPNowCommandTransport` provides asynchronous structured ESPressio Command invocation/result exchange over ESP-NOW.

Features include:

- correlation IDs;
- fragmentation/reassembly;
- per-peer isolation;
- policy hooks;
- request timeouts;
- duplicate-request suppression; and
- cached-result replay.

Conceptually:

```text
Remote CommandInvocation
        |
        v
ESPNowCommandTransport
        |
        v
ESPNowTransport
        |
        v
remote peer
```

See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md) for the full contract.

# Secure Transport

`ESPNowSecureTransport` integrates ESP-NOW with ESPressio Security:

```text
Event / Command / Clock / application payload
                    |
                    v
           TransportSecurity
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

`ESPNowSecureTransport` owns no cryptographic algorithms. It asks `Security::TransportSecurity` to protect/unprotect opaque payloads and owns only ESP-NOW-specific framing, peer addressing and fragmentation.

## Secure fragmentation

A protected Security envelope is larger than its plaintext due to authenticated metadata, nonce and tag. `ESPNowSecurityProtocol` can fragment one protected envelope across multiple ESP-NOW frames.

It supports:

- up to eight fragments per envelope;
- out-of-order arrival;
- duplicate fragment suppression;
- source/message isolation;
- bounded total envelope size; and
- malformed fragment rejection.

## Secure send

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
#include <ESPressio_Security.hpp>

ESPressio::ESPNow::ESPNowSecureTransport secure;
secure.Initialize(security);

ESPressio::Security::SecurityResult result;
secure.Send(peer, applicationProtocol, data, size, &result);
```

## Secure receive

```cpp
secure.SetReceiveHandler(
    [](const ESPressio::ESPNow::ESPNowReceivedFrame& frame,
       const ESPressio::Security::UnprotectedPayload& opened) {
        // opened.Data is authenticated plaintext.
        // opened.Protocol is the authenticated application protocol.
    }
);
```

Plaintext is delivered only after Security authentication/decryption and replay validation succeed.

See [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md).

# Native ESP-NOW encryption vs ESPressio Security

The mechanisms protect different layers:

```text
Native ESP-NOW encryption
    peer/link-specific ESP-NOW protection

ESPressio Security
    transport-independent AEAD
    sender/session identity
    protocol binding
    key IDs
    replay protection
```

Applications may use either or both. ESPressio Security is particularly useful when the same application protocol should have consistent security semantics across ESP-NOW and other transports.

# Observable transport lifecycle

`IESPNowTransportObserver` provides synchronous observation of meaningful transport lifecycle state, including initialization, shutdown, peers, sends and validated inbound ESPressio frames.

Protocol receive handlers remain authoritative for actual protocol data delivery. The Observer surface exists for lifecycle diagnostics, metrics, peer-liveness integration and optional Event bridging.

# Examples

The repository includes examples covering System Clock reference/client topologies, Event transport, Command peers and secure peers.

A secure peer example demonstrates AES-256-GCM registration, key provisioning, protected ESP-NOW send, authenticated receive and failure observation. Example keys and MAC addresses are placeholders only.

# Testing and reliability

The permanent host/ESP32 validation covers core types, peer liveness, Command transport, Security framing, Event integration, fragmentation/reassembly and the released dependency generation.

Peer-liveness regression coverage includes missed discovery windows, transition through `Suspect`, refresh from valid protocol traffic, genuine hard expiry and rediscovery.

Hardware stress testing should monitor `GetReceiveTaskMinimumFreeStackBytes()` while exercising integrated protocol workloads. The 8192-byte default addresses the stack-canary failure reproduced in EventConsole-Lab; applications with deeper custom protocol handlers can increase `ReceiveTaskStackSize` explicitly.

# Compatibility

0.8.0 is source-compatible with 0.7.x. It extends the public transport interface with receive-task stack diagnostics and increases the default receive-task stack allocation; wire framing, protocol IDs and peer interoperability are unchanged.

Applications using ESP-Now-specific Event headers continue to obtain them from ESPressio ESP-Now rather than ESPressio Event.

# Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.
