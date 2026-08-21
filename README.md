# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications, distributed System Clock synchronization, optional Event and Command transports, and optional transport-neutral authenticated encryption through ESPressio Security.

## Current Version — 0.5.2

ESPressio ESP-Now **0.5.2** is a dependency-maintenance patch over 0.5.1. It retains the Observable transport/peer lifecycle API while raising the required Timing baseline to 2.2.4.

Current dependency model:

```text
Required
    ESPressio Timing >= 2.2.4 < 3.0.0
    ESPressio Observable >= 3.0.1 < 4.0.0

Optional Event Transport
    ESPressio Event >= 5.8.0 < 6.0.0

Optional Command Transport
    ESPressio Command >= 0.3.0 < 1.0.0

Optional Secure Transport
    ESPressio Security >= 0.2.0 < 1.0.0
```

`ESPNowTransport` exposes observable initialization, shutdown, peer-add/remove, and send success/failure lifecycle information. Protocol receive handlers remain the authoritative data-delivery mechanism.

The Event relationship deliberately remains a compatible 5.x opt-in range rather than being tightened to Event 5.8.2: Event currently also hosts an ESP-Now-specific Observer bridge. Strengthening both directions would reinforce a circular optional dependency. The preferred future architecture moves `ESPNowTransportEventBridge` downstream into ESP-Now's optional Event integration, or into a dedicated integration package.

## Compatibility

ESPressio ESP-Now `0.5.2` targets the ESP32 family under Arduino-ESP32 and requires C++17.

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

### Required for 0.5.2

```text
ESPressio Timing >= 2.2.4 < 3.0.0
ESPressio Observable >= 3.0.1 < 4.0.0
Arduino-ESP32
```

Timing 2.2.4 carries Units 0.2.3 downstream. Serializable 0.10.2 remains optional through selected Serializable Unit/Event facilities and is not a core ESP-Now dependency.

### Optional for 0.5.2

```text
Event Transport
    ESPressio Event >= 5.8.0 < 6.0.0

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
MacAddress
ESPNowClockSynchronizer
ESPNowClockSynchronizationConfig
ESPNowClockSynchronizationMode
ESPNowCommandTransport             (optional Command dependency)
ESPNowSecureTransport              (optional Security dependency)
ESPNowSecurityProtocol
```

`IESPNowTransportObserver` is part of the current 0.5.x core transport API.

## PlatformIO

Core ESP-NOW/Timing usage:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.2
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
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.5.2
    https://github.com/Flowduino/ESPressio-Security@^0.2.0
```

Add Command/Event dependencies only when those adapters are selected.

## Header Structure

Core umbrella:

```cpp
#include <ESPressio_ESPNow.hpp>
```

The umbrella contains the core transport and clock synchronizer but deliberately does not include dependency-bearing optional adapters.

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

For 0.5.x compatibility, `ESPNowTransportEventBridge` remains supplied by ESPressio Event 5.8. The dependency audit for issue #10 identifies this reciprocal optional relationship as architectural debt: because ESP-Now already consumes Event for `ESPNowEventTransport`, the bridge should ultimately move downstream into this Event integration rather than requiring Event to consume ESP-Now.

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

The permanent host suite protects both existing and new contracts:

```text
ESPNowCoreTypes
ESPNowCommandTransport
ESPNowSecurityProtocol
```

Security-protocol tests cover protocol allocation, fragmentation, out-of-order reassembly, duplicate fragments, malformed frames, and maximum envelope bounds.

The 0.5.2 candidate validates the native transport observer contract and the refreshed Timing/Units/Observable dependency generation. GitHub Actions also compile real ESP32 examples against the coordinated dependencies.

## Compatibility

0.5.2 is a backward-compatible dependency-maintenance patch:

- core `ESPNowTransport` APIs are unchanged from 0.5.0;
- clock synchronization APIs are unchanged;
- Event Transport APIs are unchanged;
- Command Transport APIs are unchanged;
- Security integration remains opt-in;
- Observable remains the required lifecycle-observation dependency introduced by 0.5.0;
- Event, Command, and Security integrations remain opt-in.

## Contributing

Issues and contributions are welcome through the GitHub repository. Changes to transport framing, synchronization, Command/Event integrations, or Security integration should include corresponding regression coverage.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).