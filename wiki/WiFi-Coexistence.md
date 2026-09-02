# WiFi Coexistence

ESP-NOW and conventional WiFi share one ESP32 2.4 GHz radio. They cannot be configured as if they were independent radios.

## Authority model

When ESPressio WiFi is present, WiFi is authoritative for:

- radio mode;
- active interfaces;
- infrastructure STA association;
- scan state;
- effective channel;
- AP/STA MAC endpoints.

ESP-Now follows that state.

## Coordinator

The optional `ESPNowWiFiCoordinator` observes WiFi's dedicated radio lifecycle surface directly.

```text
WiFi transition begins
    -> ESP-NOW marks radio unavailable

WiFi changes mode/channel/interface state

WiFi transition completes
    -> resolve Auto interface
    -> reconcile managed peers
    -> resume transmission

WiFi scan begins
    -> suspend ESP-NOW transmission

WiFi scan completes
    -> reconcile channel/interface
    -> resume transmission
```

Application Events are not used for this low-level coordination because the radio transition requires immediate authoritative lifecycle handling.

## Channel rules

When STA associates to infrastructure WiFi, that network selects the effective channel. In AP+STA mode the SoftAP normally shares the same physical channel.

An active scan hops the shared radio through channels, so ESP-NOW transmission is treated as temporarily unavailable during the scan.

## Coordination contract

The coordinator should react to authoritative WiFi radio snapshots rather than polling native WiFi state independently. ESP-Now must not fight WiFi for mode/channel ownership.

A full ESP-NOW driver rebuild is retained only as an escalation path when lightweight peer/interface reconciliation cannot restore valid native state.