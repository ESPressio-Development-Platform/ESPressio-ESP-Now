# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications, distributed System Clock synchronization, optional Event and Command transports, and optional transport-neutral authenticated encryption through ESPressio Security.

## Current Version — 0.5.3

ESPressio ESP-Now **0.5.3** is a reliability patch over 0.5.2. It fixes false peer expiry when discovery advertisements are transiently missed by separating peer liveness into `Alive`, `Suspect`, and `Expired` states and by treating any validated ESPressio ESP-NOW frame as positive liveness evidence.

Current dependency model:

```text
Required
    ESPressio Timing >= 2.2.4 < 3.0.0
    ESPressio Observable >= 3.0.1 < 4.0.0

Optional Event Transport
    ESPressio Event >= 5.8.3 < 6.0.0

Optional Command Transport
    ESPressio Command >= 0.3.0 < 1.0.0

Optional Secure Transport
    ESPressio Security >= 0.2.0 < 1.0.0
```

`ESPNowTransport` exposes observable initialization, shutdown, peer-add/remove, send success/failure lifecycle information, and every validated inbound ESPressio frame through `IESPNowTransportObserver::OnESPNowFrameReceived`. Protocol receive handlers remain the authoritative data-delivery mechanism.

0.5.3 introduces the reusable `ESPNowPeerLivenessTracker`. A missed discovery window can move a peer into `Suspect`, but only sustained absence reaches `Expired`; valid addressed protocol traffic refreshes liveness immediately. This prevents temporary broadcast loss from tearing down a still-healthy destination.

The Event relationship deliberately remains a compatible 5.x opt-in range rather than becoming a core dependency. The validated baseline is Event 5.8.3. Event currently also hosts an ESP-Now-specific Observer bridge, so strengthening both directions further would reinforce a circular optional dependency. The preferred future architecture moves `ESPNowTransportEventBridge` downstream into ESP-Now's optional Event integration, or into a dedicated integration package.

## Compatibility

ESPressio ESP-Now `0.5.3` targets the ESP32 family under Arduino-ESP32 and requires C++17.

The common ESP-NOW transport uses Espressif ESP-NOW/Wi-Fi APIs plus FreeRTOS queues/tasks. The initial ESPressio wire format remains within the classic 250-byte ESP-NOW payload limit for broad compatibility, while higher-level Event, Command, and Security integrations provide their own bounded fragmentation where required.

## ESPressio Development Platform

ESPressio libraries are discrete, composable components built around a common design ethos:

- **Light-weight** — minimise memory and operational overhead.
- **Ease of Use** — strongly typed abstractions over low-level facilities.
- **Object-Oriented** — a type for everything and everything in a type.
- **SOLID** — keep responsibilities and dependencies explicit.

ESPressio ESP-Now owns ESP-NOW-specific peer/radio/framing concerns. Timing owns clock discipline, Event owns Event semantics, Command owns Command semantics, and Security owns cryptography/authentication/replay protection.

## License

ESPressio and its component libraries are licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).

## ESPressio Library Dependencies

### Required for 0.5.3

```text
ESPressio Timing >= 2.2.4 < 3.0.0
ESPressio Observable >= 3.0.1 < 4.0.0
Arduino-ESP32
```

Timing 2.2.4 carries Units 0.2.3 downstream. Serializable 0.10.2 remains optional through selected Serializable Unit/Event facilities and is not a core ESP-Now dependency.

### Optional for 0.5.3

```text
Event Transport
    ESPressio Event >= 5.8.3 < 6.0.0

Command Transport
    ESPressio Command >= 0.3.0 < 1.0.0

Secure Transport
    ESPressio Security >= 0.2.0 < 1.0.0
```

Optional integrations are intentionally not pulled into the normal umbrella merely because their implementations exist.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md), [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md), and [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md).

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
ESPNowCommandTransport             (optional Command dependency)
ESPNowSecureTransport              (optional Security dependency)
ESPNowSecurityProtocol
```

`IESPNowTransportObserver` is part of the current 0.5.x core transport API. In 0.5.3, `OnESPNowFrameReceived` allows higher-level integrations to observe every validated inbound ESPressio frame without replacing protocol handlers.

## PlatformIO

Core ESP-NOW/Timing usage:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.3
    https://github.com/Flowduino/ESPressio-Timing@^2.2.4
    https://github.com/Flowduino/ESPressio-Observable@^3.0.1

build_flags =
    -std=gnu++17

build_unflags =
    -std=gnu++11
    -fno-rtti
```

Add Security when selecting the secure adapter:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.3
    https://github.com/Flowduino/ESPressio-Security@^0.2.0
```

Add Command/Event dependencies only when those adapters are selected. The Event integration is validated against ESPressio Event 5.8.3.

## Header Structure

Core umbrella:

```cpp
#include <ESPressio_ESPNow.hpp>
```

The umbrella contains the core transport, peer-liveness tracker, and clock synchronizer but deliberately does not include dependency-bearing optional adapters.

Optional integrations:

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowCommandTransport.hpp>
#include <ESPressio_ESPNowSecureTransport.hpp>
```

## ESP-NOW Transport

`ESPNowTransport` is a process-wide transport because Espressif's receive callback is global to the ESP-NOW subsystem.

```cpp
auto& transport = ESPNow::ESPNowTransport::GetInstance();
transport.Initialize();
```

Incoming Wi-Fi callback data is copied into a bounded FreeRTOS queue. Protocol validation and application handlers execute later on the ESPressio receive task, avoiding long work in the Wi-Fi callback.

Every successfully validated inbound ESPressio frame is also surfaced to transport observers. This is deliberately separate from protocol delivery: observers can use frame arrival as operational evidence, while the registered protocol handler remains responsible for consuming the payload.

### Configuration

`ESPNowTransportConfig` exposes:

```text
InitializeWiFi
Channel
ReceiveTaskStackSize
ReceiveTaskPriority
ReceiveTaskCore
ReceiveQueueLength
```

## Peer Management

```cpp
ESPNow::ESPNowPeerConfig peer;
peer.Address = ESPNow::MacAddress(remoteMac);
peer.Channel = 0;
peer.Encrypt = false;
transport.AddPeer(peer);
```

`peer.Encrypt` controls native ESP-NOW link encryption and remains independent of ESPressio Security application/transport-layer protection.

Successful peer additions/removals and failed peer-management operations are available through the transport observer contract.

### Peer liveness

`ESPNowPeerLivenessTracker` provides transport-independent liveness classification for known ESP-NOW peers:

```text
Alive
    recent discovery or validated ESPressio traffic

Suspect
    expected discovery evidence has been missed, but the peer is not yet
    considered genuinely unavailable

Expired
    the configured hard-expiry interval has elapsed without valid evidence
```

Discovery advertisements are therefore one liveness signal rather than the sole signal. Higher-level peer managers can avoid removing a destination during short radio/broadcast disturbances and only perform destructive teardown once the peer is genuinely expired.

## ESPressio ESP-NOW Wire Format

The common outer frame carries:

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

## System Clock Synchronization

`ESPNowClockSynchronizer` transports ESPressio Timing synchronization exchanges over ESP-NOW. Timing remains responsible for offset/RTT estimation, filtering, phase correction, drift estimation, and System Clock discipline.

Supported roles are:

```text
Disabled
Client
Reference
ClientAndReference
```

A Client periodically exchanges four timestamps with a configured Reference. `ClientAndReference` supports hierarchical topologies where a synchronized node also serves downstream clients.

```cpp
ESPNow::ESPNowClockSynchronizationConfig config;
config.Mode = ESPNow::ESPNowClockSynchronizationMode::Client;
config.ReferencePeer = ESPNow::MacAddress(referenceMac);
config.SynchronizationIntervalMilliseconds = 1000;

ESPNow::ESPNowClockSynchronizer sync;
sync.Initialize(config);
```

Call `sync.Update()` regularly for Client modes.

## Event Transport

`ESPNowEventTransport` is an optional concrete ESPressio Event transport for Serializable Events. It supports multiple destination peers and bounded Event fragmentation/reassembly.

The Event integration remains distinct from Command and Security semantics.

For 0.5.3, Event integration is compiled in CI against ESPressio Event 5.8.3 and its coordinated dependency generation. `ESPNowTransportEventBridge` remains supplied by ESPressio Event 5.8.x for compatibility. The dependency audit identifies this reciprocal optional relationship as architectural debt: because ESP-Now already consumes Event for `ESPNowEventTransport`, the bridge should ultimately move downstream into this Event integration rather than requiring Event to consume ESP-Now.

## Command Transport

0.3.0 introduced `ESPNowCommandTransport` for asynchronous structured ESPressio Command invocation/result exchange.

Features include correlation IDs, fragmentation/reassembly, per-peer isolation, policy hooks, timeouts, duplicate-request suppression and cached-result replay. See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md).

## Secure Transport

0.4.0 introduced `ESPNowSecureTransport`, backed by ESPressio Security.

Architecture:

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

`ESPNowSecureTransport` owns no cryptographic algorithms. It asks `Security::TransportSecurity` to protect or unprotect opaque payloads and owns only ESP-NOW-specific framing, destination/source handling, and fragmentation.

The authenticated ESPressio Security envelope provides algorithm/key IDs, sender identity, session epoch, sequence/replay protection, AEAD ciphertext and tag. Plaintext is delivered only after authentication/decryption and replay validation succeed.

### Secure Fragmentation

A Security envelope is larger than its plaintext due to authenticated metadata, nonce, and tag. `ESPNowSecurityProtocol` can therefore fragment one protected envelope over multiple ESP-NOW frames.

It supports:

- up to eight fragments per envelope;
- out-of-order arrival;
- duplicate fragment suppression;
- source/message isolation;
- bounded total envelope size;
- malformed fragment rejection.

### Secure Send

```cpp
#include <ESPressio_ESPNowSecureTransport.hpp>
#include <ESPressio_Security.hpp>

ESPNow::ESPNowSecureTransport secure;
secure.Initialize(security);

Security::SecurityResult result;
secure.Send(peer, applicationProtocol, data, size, &result);
```

### Secure Receive

```cpp
secure.SetReceiveHandler(
    [](const ESPNow::ESPNowReceivedFrame& frame,
       const Security::UnprotectedPayload& opened) {
        // opened.Data is authenticated plaintext.
        // opened.Protocol is the authenticated application protocol.
    }
);
```

Security failures can be observed with the existing secure-adapter failure mechanism without exposing secret key material; the 0.5.x transport observer adds general ESP-NOW transport/peer lifecycle diagnostics rather than duplicating the Security observer contract.

See [SECURITY_INTEGRATION.md](SECURITY_INTEGRATION.md).

## Native ESP-NOW Encryption vs ESPressio Security

The mechanisms are complementary:

```text
Native ESP-NOW encryption
    link/peer-specific ESP-NOW protection

ESPressio Security
    transport-independent AEAD, sender/session identity,
    protocol binding, key IDs, replay protection
```

Applications may use either or both. ESPressio Security is useful when the same application protocol must retain consistent security semantics across ESP-NOW and other transports.

## Examples

The repository includes examples for:

```text
System Clock Reference/Client topologies
Event transport
CommandPeer
SecurePeer
```

`examples/SecurePeer/SecurePeer.ino` demonstrates AES-256-GCM registration, key provisioning, per-session Security state, protected ESP-NOW send, authenticated receive, and failure observation.

Example keys/MAC addresses are placeholders and must not be treated as production provisioning guidance.

## Testing

The permanent host suite protects the core and integration contracts, including peer-liveness behavior:

```text
ESPNowCoreTypes
ESPNowPeerLivenessTracker
ESPNowCommandTransport
ESPNowSecurityProtocol
```

Peer-liveness regression coverage includes missed discovery windows, transition through `Suspect`, refresh from valid protocol traffic, genuine hard expiry, and rediscovery. Security-protocol tests cover protocol allocation, fragmentation, out-of-order reassembly, duplicate fragments, malformed frames, and maximum envelope bounds.

The 0.5.3 candidate is also validated by a permanent ESP32 Event integration build against Event 5.8.3, Threads 3.1.4, Timing 2.2.4, Units 0.2.3, Observable 3.0.1, and Serializable 0.10.2.

Hardware validation for the #15 fix transmitted Event messages 2–22 from one device to the other with all 21 arriving in order and no gaps; the reverse startup Event also arrived, and neither device reported peer expiry/removal during the test.

## Compatibility

0.5.3 is a backward-compatible reliability patch:

- core `ESPNowTransport` send/peer-management APIs remain compatible with 0.5.2;
- `IESPNowTransportObserver` gains validated-frame observation for operational integrations;
- clock synchronization APIs are unchanged;
- Event Transport APIs remain compatible and are validated against Event 5.8.3;
- Command Transport APIs are unchanged;
- Security integration remains opt-in;
- Observable remains the required lifecycle-observation dependency introduced by 0.5.0;
- Event, Command, and Security integrations remain opt-in.

## Contributing

Issues and contributions are welcome through the GitHub repository. Changes to transport framing, synchronization, peer liveness, Command/Event integrations, or Security integration should include corresponding regression coverage.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).