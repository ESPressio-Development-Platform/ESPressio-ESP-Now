# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications, distributed System Clock synchronization, optional Event and Command transports, optional lifecycle-to-Event bridges, and optional authenticated payload protection through ESPressio Security.

## Current Version — 0.6.0

ESPressio ESP-Now 0.6.0 preserves the mature 0.5.x transport, synchronization, Command, Security and peer-liveness functionality while correcting ownership of ESP-Now-specific Event integration. ESP-Now now owns its concrete Event Transport, ESP-Now lifecycle Event types and `ESPNowTransportEventBridge`.

## Why ESPressio ESP-Now?

Espressif's ESP-NOW API is low-level and callback-oriented. ESPressio ESP-Now adds reusable framing, protocol allocation, peer abstractions, receive-task isolation, lifecycle observation, distributed Timing integration and optional higher-level protocol adapters.

```text
Event / Command / Clock Sync / application payload
                     |
                     v
              ESPNowTransport
                     |
                     v
                  ESP-NOW
                     |
                     v
                 Wi-Fi radio
```

Application code can therefore operate in terms of the ESPressio protocol it actually cares about instead of repeatedly rebuilding callback, peer, queue and framing infrastructure.

## ESPressio Development Platform

ESPressio libraries are discrete, composable components built around a common design ethos: light-weight implementation, strongly typed ease of use, object-oriented APIs and explicit SOLID dependency boundaries.

ESP-Now owns ESP-NOW-specific peer/radio/framing concerns. Timing owns clock discipline, Event owns generic Event semantics, Command owns Command semantics, and Security owns cryptography/authentication/replay protection.

## License

Apache License 2.0. See [LICENSE](LICENSE).

## Namespace

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

Command integration
    ESPressio Command >= 0.4.0 < 1.0.0

Secure Transport
    ESPressio Security >= 0.3.0 < 1.0.0
```

The normal `ESPressio_ESPNow.hpp` umbrella remains free of Event, Command and Security includes.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), and [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md).

## Installation

Core transport/Timing use:

```ini
lib_deps =
    flowduino/ESPressio-ESP-Now@^0.6.0
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

Every successfully validated inbound ESPressio frame is also surfaced to transport observers. This is deliberately separate from protocol delivery: observers can use frame arrival as operational/liveness evidence while the registered protocol handler remains authoritative for consuming the payload.

## Configuration

`ESPNowTransportConfig` exposes the principal transport/runtime settings:

```text
InitializeWiFi
Channel
ReceiveTaskStackSize
ReceiveTaskPriority
ReceiveTaskCore
ReceiveQueueLength
```

This makes receive-task resources explicit instead of hiding them in the transport implementation.

# Peer management

Add a peer using `ESPNowPeerConfig`:

```cpp
ESPressio::ESPNow::ESPNowPeerConfig peer;
peer.Address = ESPressio::ESPNow::MacAddress(remoteMac);
peer.Channel = 0;
peer.Encrypt = false;

transport.AddPeer(peer);
```

`peer.Encrypt` controls native ESP-NOW link encryption and is independent of ESPressio Security application/transport-layer protection.

Transport observers expose peer-add/remove and relevant failure lifecycle information for diagnostics.

# Peer-liveness reliability

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

Discovery advertisements are only **one** liveness signal. Any validated ESPressio ESP-NOW frame is positive evidence that the peer is alive.

This distinction prevents transient broadcast/discovery loss from immediately tearing down an otherwise healthy Event destination or synchronization peer.

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

# Compatibility

0.6.0 changes **ownership**, not the underlying ESP-Now transport protocol semantics. Core transport, synchronization, wire framing, protocol IDs, peer-liveness APIs, Command transport and Security transport semantics remain compatible with the completed 0.5.x generation.

Applications using ESP-Now-specific Event headers now obtain them from ESPressio ESP-Now 0.6.0 rather than ESPressio Event.

# Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.
