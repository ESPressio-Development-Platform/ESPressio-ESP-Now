# ESPressio Command over ESP-NOW

ESPressio ESP-Now 0.8.0 provides an optional transport for invoking **ESPressio Command >= 1.0.1 < 2.0.0** operations between ESP32 peers over ESP-NOW.

The integration is intentionally separate from ESPressio Event Transport:

```text
Command = request/intention: do something
Event   = notification/fact: something happened
```

Commands are therefore transported directly as `CommandInvocation` requests and `CommandResult` responses rather than being wrapped in Events.

## Dependencies

The integration requires:

```text
ESPressio ESP-Now >= 0.8.0 < 1.0.0
ESPressio Command >= 1.0.1 < 2.0.0
ESPressio Timing >= 2.2.5 < 3.0.0
ESPressio Observable >= 3.0.2 < 4.0.0
Arduino-ESP32
```

`ESPressio Command` remains opt-in. The normal `ESPressio_ESPNow.hpp` umbrella does not include the Command integration headers.

No ESPressio Serializable or ArduinoJson dependency is required by the Command protocol.

## Headers

```cpp
#include <ESPressio_Command.hpp>
#include <ESPressio_ESPNowCommandTransport.hpp>
```

The host-testable protocol/endpoint layers can also be included directly:

```cpp
#include <ESPressio_ESPNowCommandProtocol.hpp>
#include <ESPressio_ESPNowCommandEndpoint.hpp>
```

## Architecture

```text
Application A
    |
    | CommandInvocation
    v
ESPNowCommandTransport
    |
    | Command protocol fragment(s)
    v
ESPNowTransport
    |
    v
ESP-NOW
    |
    v
ESPNowTransport
    |
    v
ESPNowCommandTransport
    |
    v
CommandRegistry
    |
    v
Command callback
    |
    v
CommandResult
    |
    +---------------- correlated response ---------------->
```

`ESPNowCommandTransport` reuses the process-wide `ESPNowTransport`; it does not initialize a second ESP-NOW subsystem or duplicate peer management.

## Protocol identifier

The Command transport uses:

```cpp
ESPNowProtocol::CommandTransport
```

with protocol ID `3`.

The existing IDs remain:

```text
1  ClockSynchronization
2  EventTransport
3  CommandTransport
64+ User/application protocols
```

## Initializing a Command-capable peer

Initialize the shared ESP-NOW transport first:

```cpp
auto& transport =
    ESPNow::ESPNowTransport::GetInstance();

transport.Initialize();
```

Register the remote peer using the normal ESP-NOW peer API:

```cpp
static const uint8_t RemoteMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x01
};

ESPNow::ESPNowPeerConfig peer;
peer.Address = ESPNow::MacAddress(RemoteMac);
peer.Channel = 0;
peer.Encrypt = false;

transport.AddPeer(peer);
```

Register application Commands in ESPressio Command:

```cpp
auto& commands =
    Command::CommandRegistry::GetInstance();

commands.Command("gpio")
    .Command("write")
    .Parameter<int>("pin");
```

A complete executable node might be:

```cpp
auto& write =
    commands.Command("gpio")
        .Command("write");

write.Parameter<int>("pin").Range(0, 48);
write.Parameter<bool>("state");

write.OnExecute(
    [](const Command::CommandContext& context) {
        const int pin = context.Get<int>("pin");
        const bool state = context.Get<bool>("state");

        pinMode(pin, OUTPUT);
        digitalWrite(pin, state ? HIGH : LOW);

        return Command::CommandResult::Ok("GPIO updated");
    }
);
```

Initialize Command transport:

```cpp
ESPNow::ESPNowCommandTransport commandTransport;

ESPNow::ESPNowCommandTransportConfig config;

commandTransport.Initialize(
    transport,
    commands,
    config
);
```

## Invoking a remote Command

Commands are asynchronous because ESP-NOW transport itself is asynchronous.

Command 1.0.0 structured values can be supplied natively:

```cpp
Command::CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.positional = {2, true};

commandTransport.Invoke(
    ESPNow::MacAddress(RemoteMac),
    invocation,
    [](const Command::CommandResult& result) {
        if (result.success) {
            Serial.println(result.message.c_str());
        } else {
            Serial.printf(
                "Command failed (%d): %s\n",
                result.code,
                result.message.c_str()
            );
        }
    }
);
```

String-backed values remain valid too:

```cpp
invocation.positional = {"2", "high"};
```

Call:

```cpp
commandTransport.Update();
```

regularly from the application loop so request timeouts and bounded reassembly/duplicate caches can expire even while no packets are arriving.

## Structured request format

Commands are serialized directly from `CommandInvocation` into a compact binary payload containing:

```text
Command path
positional parameters
named parameters
raw caller text
```

Results contain:

```text
success/failure
result code
result message
```

Every request/result pair carries the same 64-bit request ID.

### Command 1.0.0 typed values and protocol-v1 compatibility

The ESP-Now Command wire protocol intentionally remains **version 1**. Protocol v1 was defined with string-valued positional and named parameters, so ESP-Now 0.8.0 preserves that wire representation rather than forcing an incompatible protocol revision.

When encoding a Command 1.x invocation:

```text
CommandValue -> CommandValue::ToString() -> protocol-v1 string
```

When decoding:

```text
protocol-v1 string -> string-backed CommandValue
```

The receiving `CommandRegistry` remains authoritative for converting and validating those values against the registered parameter definitions. Native scalar type identity is therefore not carried across protocol v1, but existing peers remain interoperable and existing Command validation semantics are retained.

A null `CommandValue` has no representation in protocol v1 and is rejected rather than silently converted.

## Fragmentation

The underlying ESPressio ESP-NOW transport deliberately stays within the classic 250-byte ESP-NOW v1 payload limit.

Its common outer header consumes 8 bytes, leaving at most 242 bytes for a protocol payload.

The Command transport adds its own fragment header and automatically splits larger encoded requests/results across multiple ESP-NOW frames.

Reassembly is:

- bounded by `MaximumMessageBytes`;
- bounded by `MaximumReassemblies`;
- isolated by remote peer, request ID and message type;
- tolerant of out-of-order fragments;
- tolerant of exact duplicate fragments;
- invalidated when duplicate fragment contents disagree; and
- expired after `ReassemblyTimeoutMilliseconds`.

## Duplicate request protection

Commands may have physical/system side effects, so retransmitted or duplicate requests must not execute twice.

Completed inbound requests are cached by:

```text
remote peer + request ID
```

for a bounded period.

If the same request is received again during that period, the previous encoded `CommandResult` is re-sent without invoking the Command callback a second time.

Configure this behavior with:

```cpp
config.Endpoint.MaximumDuplicateResults = 8;
config.Endpoint.DuplicateResultLifetimeMilliseconds = 5000;
```

The cache is bounded; it is not a permanent transaction ledger.

## Timeouts and resource limits

`ESPNowCommandEndpointConfig` provides:

```cpp
MaximumProtocolPayloadBytes
MaximumMessageBytes
MaximumOutstandingRequests
MaximumReassemblies
MaximumDuplicateResults
RequestTimeoutMilliseconds
ReassemblyTimeoutMilliseconds
DuplicateResultLifetimeMilliseconds
```

Defaults are intended to be conservative for embedded systems.

When a request exceeds `RequestTimeoutMilliseconds`, its completion callback receives:

```text
ESP-NOW Command request timed out
```

with a non-success `CommandResult`.

## Invocation metadata and policy

Incoming requests expose `ESPNowCommandInvocationContext` containing:

```text
Transport = "esp-now"
RemotePeer
RequestID
CommandInvocation
```

A policy hook can reject remote requests before the registered Command executes:

```cpp
commandTransport.SetPolicy(
    [](const ESPNow::ESPNowCommandInvocationContext& context) {
        if (context.Metadata.RemotePeer.Bytes[0] != 0x24) {
            return Command::CommandResult::Error(
                "Peer is not authorized"
            );
        }

        return Command::CommandResult::Ok();
    }
);
```

Use this for application-owned:

- authorization;
- allow/deny lists;
- rate limiting;
- remote/local policy;
- auditing; and
- diagnostics.

The transport does not decide which application Commands are safe to expose remotely.

## Result observation

Remote execution can be observed independently from the Command callback:

```cpp
commandTransport.SetResultObserver(
    [](const ESPNow::ESPNowCommandInvocationContext& context,
       const Command::CommandResult& result) {
        // Audit/diagnostics.
    }
);
```

## Encryption

Command transport uses the peer/security configuration already provided by `ESPNowTransport` and the ESP-NOW stack.

To use ESP-NOW peer encryption, configure the peer through `ESPNowPeerConfig` in the same way as other ESPressio ESP-NOW protocols.

The Command protocol does not implement its own cryptography.

## Event + Command together

A useful pattern is:

```text
Remote peer
    |
    | Command: gpio write 2 high
    v
Device
    |
    | Event: GpioStateChangedEvent
    v
Interested peers
```

Command handles intent; Event handles asynchronous observation of the resulting state change.

## PlatformIO

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-ESP-Now@^0.8.1
    https://github.com/ESPressio-Development-Platform/ESPressio-Command@^1.0.1
```

Timing remains the required ESP-Now foundation and is declared by ESPressio ESP-Now itself.

## Testing

The host test suite validates the radio-independent Command protocol/endpoint behavior, including:

- request/result serialization;
- Command 1.x typed-value normalization at the protocol-v1 boundary;
- null-value rejection;
- correlation IDs;
- fragmented messages;
- out-of-order fragments;
- exact duplicate fragments;
- missing-fragment expiry;
- malformed magic/version/header rejection;
- oversized-message rejection;
- per-peer reassembly isolation;
- request timeouts;
- maximum outstanding requests;
- policy rejection;
- result observation;
- peer metadata;
- duplicate request suppression/result replay; and
- deterministic shutdown completion.

Concrete `ESPNowCommandTransport` remains a thin adapter over already-established `ESPNowTransport` send/receive routing.
