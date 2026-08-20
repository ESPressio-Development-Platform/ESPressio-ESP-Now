# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications, distributed System Clock synchronization, optional Event and Command transports, and from 0.4.0 an optional transport-neutral authenticated-encryption layer through ESPressio Security.

## Latest Stable Version

The latest Stable Version is **0.4.0**.

## Compatibility

ESPressio ESP-Now `0.4.0` targets the ESP32 family under Arduino-ESP32 and requires C++17.

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

### Required

```text
ESPressio Timing >= 2.2.2 < 3.0.0
Arduino-ESP32
```

### Optional

```text
Event Transport
    ESPressio Event >= 5.7.1 < 6.0.0

Command Transport
    ESPressio Command >= 0.2.0 < 1.0.0

Secure Transport
    ESPressio Security >= 0.1.0 < 1.0.0
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

## PlatformIO

Core ESP-NOW/Timing usage:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.4.0
    https://github.com/Flowduino/ESPressio-Timing@^2.2.2

build_flags =
    -std=gnu++17

build_unflags =
    -std=gnu++11
    -fno-rtti
```

Add Security when selecting the secure adapter:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.4.0
    https://github.com/Flowduino/ESPressio-Security@^0.1.0
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

## ESPressio ESP-NOW Wire Format

The common outer frame carries:

```text
magic
wire version
protocol identifier
payload length
```

Protocol allocation in 0.4.0 is:

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

## Command Transport

0.3.0 introduced `ESPNowCommandTransport` for asynchronous structured ESPressio Command invocation/result exchange.

Features include correlation IDs, fragmentation/reassembly, per-peer isolation, policy hooks, timeouts, duplicate-request suppression and cached-result replay. See [COMMAND_INTEGRATION.md](COMMAND_INTEGRATION.md).

## Secure Transport

0.4.0 introduces `ESPNowSecureTransport` backed by ESPressio Security 0.1.x.

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

Security failures can be observed with `SetSecurityFailureHandler` without exposing secret key material.

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

GitHub Actions also compiles real ESP32 examples against released dependency versions, including the secure adapter with ESPressio Security 0.1.0.

## Compatibility

0.4.0 is a backward-compatible minor release:

- core `ESPNowTransport` APIs are unchanged;
- clock synchronization APIs are unchanged;
- Event Transport APIs are unchanged;
- Command Transport APIs are unchanged;
- Security integration is opt-in;
- core ESP-NOW does not acquire a mandatory Security dependency.

## Contributing

Issues and contributions are welcome through the GitHub repository. Changes to transport framing, synchronization, Command/Event integrations, or Security integration should include corresponding regression coverage.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).
