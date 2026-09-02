# Peers and Interfaces

ESPressio ESP-Now maintains a bounded logical peer registry separately from the native ESP-NOW peer table.

This separation lets the transport reconstruct/reconcile native peer state after WiFi mode or interface changes without forcing the application to rebuild its logical topology.

## Interface policy

`ESPNowPeerConfig::Interface` may be explicit or `Auto`.

`Auto` is a **lifetime policy**, not a one-time decision at registration.

Resolution follows the current shared-radio state:

```text
AP-only                  -> AccessPoint
STA-only                 -> Station
AP+STA, STA connected    -> Station
AP+STA, STA disconnected -> AccessPoint when AP is active
```

When the WiFi coordinator is attached, its authoritative radio binding takes precedence over fallback inference.

Validated unicast receive metadata can still supply an interface hint for an `Auto` peer.

## Explicit peers

Peers explicitly configured for `Station` or `AccessPoint` do not silently migrate. If the active radio state no longer satisfies that explicit choice, send diagnostics should report the mismatch rather than changing application intent.

## Channel policy

A peer channel of `0` means follow the current WiFi radio channel and is the recommended setting when WiFi owns radio configuration.

An explicit non-zero channel expresses a concrete requirement and may therefore become incompatible when infrastructure WiFi selects another channel.

## Reconciliation

`ReconcileManagedPeers()` reprograms native peer state from the logical registry after radio transitions. Lightweight reconciliation is preferred; a full ESP-NOW deinit/init rebuild is an escalation path when the driver rejects the lighter rebind.

Logical peer definitions remain owned by ESP-Now across such a rebuild.

## Extension rules

Do not conflate logical node identity, current transport endpoint MAC and native peer-table entries. These have different lifetimes and semantics.

Extensions should preserve explicit-versus-Auto intent and should not silently mutate an explicitly selected interface/channel to make a send succeed.