# ESPressio ESP-Now Security Integration

ESPressio ESP-Now 0.4.0 adds an optional adapter for ESPressio Security 0.1.x.

## Dependency

Core ESPressio ESP-Now remains independent of Security. Applications selecting the secure adapter must provide:

```text
ESPressio Security >= 0.1.0 < 1.0.0
```

PlatformIO example:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.4.0
    https://github.com/Flowduino/ESPressio-Security@^0.1.0
```

## Placement

```text
Event / Command / Clock / application payload
                    |
                    v
          ESPressio Security
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

`ESPNowSecureTransport` does not implement cryptography. It delegates protection/authentication/replay handling to `Security::TransportSecurity` and owns only ESP-NOW-specific transport concerns.

## Wire Protocol

ESP-NOW protocol allocation is:

```text
1   Clock Synchronization
2   Event Transport
3   Command Transport
4   Secure Transport
64+ User/Application protocols
```

The outer ESP-NOW protocol is `SecureTransport`. Each secure fragment also carries the original application protocol as a routing field. This is required because `Disabled` and `Preferred` policies may legitimately carry plaintext with no Security envelope. For protected traffic, the same protocol ID is also authenticated inside the ESPressio Security envelope; changing only the outer routing value therefore causes `ProtocolMismatch` and the payload is rejected before application processing.

Because the authenticated envelope adds metadata, nonce, and tag overhead, secure envelopes may exceed one ESP-NOW v1-compatible payload. `ESPNowSecurityProtocol` therefore fragments an envelope into bounded frames and reassembles them before authentication/decryption.

Reassembly accepts out-of-order fragments, ignores duplicate fragments, is bounded to eight fragments per envelope, and is isolated by source MAC/message ID. Concurrent in-flight messages from the same source use independent reassembly slots.

## Configuration

Configure Security exactly as for any other transport:

```cpp
Security::AES256GCMCipher cipher;
Security::AeadCipherRegistry ciphers;
Security::StaticKeyProvider keys;
Security::ESP32RandomSource random;

ciphers.Register(cipher);
keys.Add(1, Security::AeadAlgorithm::AES256GCM, key, 32);

Security::TransportSecurityConfig config;
config.Policy = Security::TransportSecurityPolicy::Required;
config.OutboundAlgorithm = Security::AeadAlgorithm::AES256GCM;
config.OutboundKeyID = 1;
config.SenderID = ESP.getEfuseMac();

Security::TransportSecurity security(ciphers, keys, random, config);
ESPNow::ESPNowSecureTransport secure;
secure.Initialize(security);
```

`SessionID = 0` uses Security's automatic per-session epoch generation. Receivers therefore distinguish a sender reboot/new session from a replay within an existing session.

## Sending

```cpp
Security::SecurityResult result;
secure.Send(peer, protocol, bytes, length, &result);
```

If `Required` policy cannot protect the message, the send fails before application plaintext is handed to ESP-NOW.

## Receiving

```cpp
secure.SetReceiveHandler(
    [](const ESPNow::ESPNowReceivedFrame& radioFrame,
       const Security::UnprotectedPayload& opened) {
        // opened.Data is the accepted application payload.
        // opened.Protocol is the original application protocol.
        // opened.Protected tells whether AEAD protection was used.
    }
);
```

Under `Required`, the callback is reached only after envelope reassembly, AEAD authentication/decryption, protocol binding, and replay validation have succeeded. Under `Preferred` or `Disabled`, accepted plaintext is routed using the same outer application-protocol field and is marked `Protected == false`.

Security failures can be observed separately:

```cpp
secure.SetSecurityFailureHandler(...);
```

Never log key bytes. `SecurityResult` and `UnprotectedPayload` expose identifiers and status, not secret key material.

## Native ESP-NOW Encryption

`ESPNowPeerConfig::Encrypt` remains available. It is independent from ESPressio Security.

Applications may use:

- native ESP-NOW encryption only;
- ESPressio Security only; or
- both as defense-in-depth.

ESPressio Security provides transport-independent authenticated encryption and replay/session semantics that remain consistent when the same protocol is moved to another concrete transport.

## Higher-Level Protocols

0.4.0 intentionally does not make Event, Command, or Timing depend directly on Security. The secure adapter protects opaque payloads. Applications or future adapters can therefore place any higher-level serialized protocol through the secure path without adding cryptographic concerns to those libraries.

## Resource Limits

The ESP-NOW secure fragmentation layer permits up to eight fragments per protected envelope. Applications should still configure `TransportSecurityConfig::MaximumPlaintextBytes` to a value appropriate for the device and protocol.

## Example

See:

```text
examples/SecurePeer/SecurePeer.ino
```

The example uses a hard-coded demonstration key only to keep the sample self-contained. Production keys must be provisioned using an appropriate secure mechanism.
