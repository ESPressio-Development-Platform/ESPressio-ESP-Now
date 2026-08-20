# ESPressio ESP-Now

ESP-NOW transport and distributed ESPressio implementations for the Flowduino ESPressio Development Platform.

ESPressio ESP-Now provides a reusable ESP-NOW transport foundation for ESP32-family applications, distributed System Clock synchronization, optional Event and Command transports, optional authenticated encryption through ESPressio Security, and native Observable transport/peer lifecycle notifications.

## Current Development Version

This branch targets **ESPressio ESP-Now 0.5.0**.

0.5.0 makes the shared `ESPNowTransport` lifecycle observable and refreshes the optional Command and Security baselines to the new observable-aware releases.

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Dependency model

### Required

- **ESPressio Timing >= 2.2.2 and < 3.0.0** — System Clock synchronization support retained from the existing architecture.
- **ESPressio Observable >= 3.0.1 and < 4.0.0** — transport/peer lifecycle observation introduced in 0.5.0.

### Optional

- **ESPressio Command >= 0.3.0 and < 1.0.0** — distributed Command transport.
- **ESPressio Security >= 0.2.0 and < 1.0.0** — authenticated/encrypted ESP-NOW transport.
- **ESPressio Event >= 5.8.0 and < 6.0.0** — distributed Serializable Event transport and optional observer-to-Event lifecycle bridge.

The umbrella `ESPressio_ESPNow.hpp` intentionally does not include Command, Security or Event integration headers automatically.

```text
Timing ----------------------------> ESP-Now clock synchronization
Observable ------------------------> ESPNowTransport lifecycle
Command ------- optional ----------> ESP-NOW Command transport
Security ------ optional ----------> ESP-NOW secure transport
Event --------- optional ----------> ESP-NOW Event transport / lifecycle bridge
```

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) for the wider ecosystem view.

## PlatformIO

```ini
lib_deps =
    flowduino/ESPressio-ESPNow@^0.5.0
    flowduino/ESPressio-Timing@^2.2.2
    flowduino/ESPressio-Observable@^3.0.1
```

Add Command, Security or Event only when those integrations are selected.

## Core transport

```cpp
#include <ESPressio_ESPNow.hpp>

using namespace ESPressio::ESPNow;

auto& transport = ESPNowTransport::GetInstance();
transport.Initialize();
```

`ESPNowTransport` provides:

- ESP-NOW initialization/shutdown;
- peer management;
- protocol-ID based payload routing;
- bounded ESP-NOW wire framing;
- receive-callback isolation through the library's receive task; and
- send submission through the shared transport instance.

Higher-level protocols register their own protocol handlers rather than owning a separate ESP-NOW stack.

## Observable transport lifecycle

0.5.0 adds `IESPNowTransportObserver`:

```cpp
class TransportObserver final :
    public ESPressio::ESPNow::IESPNowTransportObserver {
public:
    void OnESPNowTransportInitialized() override {
        // ESP-NOW is ready.
    }

    void OnESPNowPeerAdded(
        const ESPressio::ESPNow::MacAddress& address
    ) override {
        // Peer became managed by the transport.
    }

    void OnESPNowSendFailed(
        const ESPressio::ESPNow::MacAddress& destination,
        uint8_t protocol,
        std::size_t payloadLength
    ) override {
        // Submission to ESP-NOW failed.
    }
};

TransportObserver observer;
auto observerHandle = transport.RegisterObserver(&observer);
```

The observer surface covers:

- initialization success;
- initialization failure;
- shutdown;
- peer addition;
- peer removal;
- send acceptance by the ESP-NOW API; and
- send submission failure.

`OnESPNowSendAccepted` means the ESP-NOW API accepted the transmission request; it is not a claim that the remote peer received or processed the packet.

The primary inbound protocol-handler path remains unchanged. Receive delivery is not replaced by Observable because it is the application's actual data-delivery mechanism rather than a lifecycle fact.

## Peer management

`ESPNowPeerConfig` continues to control peer address, channel and ESP-NOW link encryption settings. Peer-added/removed observations are emitted only for actual managed-state transitions; adding a peer that already exists does not invent a duplicate transition.

## System Clock synchronization

The existing ESP-NOW System Clock synchronization implementation remains delegated to ESPressio Timing. ESP-Now owns transport mechanics; Timing owns clock discipline, synchronization policy and timing observations.

## Command transport

Command integration remains opt-in and now targets ESPressio Command 0.3.x. Command's own registry lifecycle is observable at the Command layer; ESP-Now does not duplicate that state. The ESP-NOW Command transport continues to provide correlated requests/results, bounded fragmentation/reassembly and per-peer state.

## Security transport

Security integration remains opt-in and now targets ESPressio Security 0.2.x. `ESPNowSecureTransport` applies Security envelopes before ESP-NOW fragmentation and authenticates/decrypts completed envelopes before application delivery.

Security session establishment, replay resets and authentication/replay failures originate from the shared `TransportSecurity` object and can be observed there directly.

## Event transport

The existing distributed Serializable Event transport remains opt-in and uses ESPressio Event's transport abstractions and routing policy.

ESPressio Event 5.8.0 additionally provides an optional `ESPNowTransportEventBridge`:

```cpp
#include <ESPressio_ESPNowTransportEventBridge.hpp>

ESPressio::Event::ESPNowTransportEventBridge::GetInstance().Initialize(transport);
```

The bridge converts transport/peer/send lifecycle observations into asynchronous Events. ESP-Now itself does not depend on Event.

## Testing

Host tests cover core wire types, Security framing/reassembly, Command transport contracts and the new observer interface. ESP32 CI continues to compile representative distributed examples against Timing, Observable and the current Command generation.

## Compatibility

0.5.0 is intended as a backward-compatible extension of 0.4.x. Existing protocol handlers, peer-management calls, clock synchronization, Event transport, Command transport and Security transport APIs remain supported. Core ESP-Now builds now require Observable 3.x because the shared transport itself is observable.

## License

Apache License 2.0. See [LICENSE](LICENSE).
