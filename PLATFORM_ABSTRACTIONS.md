# ESPressio ESP-Now Platform Abstraction Audit

This file records the platform-abstraction tranche chronologically for ESPressio-ESP-Now. ESP-NOW itself is intentionally ESP32-specific and remains owned by this library; only shared lower-level concepts are consumed through ESPressio abstractions.

Working branch: `bugfix/39-wifi-coexistence`

## Shared runtime abstraction

The transport no longer directly owns generic timing, queue, or execution telemetry primitives:

- monotonic timestamps are sourced through `ESPressio::System::Clock::Monotonic()`;
- the receive queue is owned through `ESPressio::System::Queue::IMessageQueue`;
- worker stack telemetry is sourced through `ESPressio::System::Execution`;
- receive-worker priority/core configuration uses portable integer values rather than FreeRTOS public types.

Native ESP-NOW and ESP-WiFi radio/interface calls remain local intentionally because ESP-NOW is itself an ESP32-specific transport concept.

## WiFi coexistence ownership

`ESPNowWiFiCoordinator` integrates the ESP-NOW transport with the WiFi-owned radio lifecycle. Native ESP-NOW attachment is suspended across disruptive WiFi radio transitions and rebuilt afterward from the transport's retained logical peer state.

### 2026-08-27 hardware follow-up — AP station topology changes (#44)

A long-running two-device Lab test exposed a one-way failure after a phone associated with StickA's access point. StickA continued to receive and execute traffic from StickB, but StickA's outbound ESP-NOW sends failed across State, Command, Timing, and discovery protocols. Disconnecting the phone did not restore the transmit path.

The WiFi radio state abstraction intentionally describes interface/mode/channel facts and therefore does not encode access-point station count. AP station association/disassociation is exposed separately through `IWiFiObserver`. The coordinator now observes both surfaces:

- `IWiFiRadioObserver` continues to own true radio transitions and scan lifecycle;
- `IWiFiObserver::OnAccessPointStationConnected` and `OnAccessPointStationDisconnected` now treat AP station topology changes as native ESP-NOW reconciliation boundaries;
- topology reconciliation re-reads the current authoritative radio state, forces the existing native ESP-NOW reinitialization path, and replays all retained managed peers;
- logical peer configuration, encryption keys, protocol handlers, worker state, and higher-level transport state are preserved;
- no send retry shim or duplicate peer registry is introduced.

Regression coverage compiles the coordinator as both observer types and explicitly exercises both AP-station topology callbacks in the ESP32 coexistence consumer. The issue remains open pending hardware validation of phone connect/disconnect soak behavior.

No release/version numbers were changed during this work.
