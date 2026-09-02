# Sending and Diagnostics

`ESPNowTransport::Send()` remains the simple boolean send surface. Use `SendDetailed()` when the caller needs to distinguish failure classes.

Detailed results include a stable ESPressio classification plus the native ESP-IDF error value for diagnostics.

Representative failure classes include:

```text
NotInitialized
InvalidArgument
NoMemory
PeerNotFound
InterfaceMismatch
ChannelMismatch
RadioUnavailable
Internal
Unknown
```

## `RadioUnavailable`

During a known WiFi radio transition or active scan, ESP-NOW can reject a send as `RadioUnavailable` rather than entering the native driver during a disruptive window.

This is intentionally different from a permanent configuration failure.

## Endpoint identity

`GetLocalEndpointAddress()` reports the MAC of the currently resolved ESP-NOW endpoint. STA and AP interfaces have different MAC addresses, so callers should not assume the STA MAC always represents the active transport endpoint.

If the application requires a stable logical node identity across interface migration, keep that logical identity separate from the current transport endpoint address.

## Observability

`IESPNowTransportObserver` reports initialization, shutdown, peer and send lifecycle plus validated inbound ESPressio frames.

Detailed send-failure observation is additive to the simpler failure callback, letting diagnostics become richer without forcing every observer to consume native detail.

## Operational guidance

Treat `NoMemory`, queue saturation and repeated channel/interface mismatch as telemetry worth surfacing during hardware validation. A boolean send failure alone is insufficient for diagnosing shared-radio and heap-pressure problems.