# Changelog

## 0.2.0

- Added optional `ESPNowEventTransport` implementing ESPressio Event 5.4 `IEventTransport`.
- Added ESP-NOW Event Transport protocol identifier.
- Added multi-peer Event packet fan-out.
- Added fragmentation/reassembly so Event Transport packets can exceed one ESP-NOW frame.
- Preserved callback isolation by performing Event processing outside the Wi-Fi receive callback.
- Kept ESPressio Event and Serializable optional for non-Event ESPressio ESP-Now users.
- Added Event Transport example and documentation.

## 0.1.0

Initial ESPressio ESP-Now release.

### ESP-NOW transport

- Adds `ESPNowTransport` as the shared ESP-NOW transport foundation.
- Provides Arduino-ESP32 Wi-Fi/ESP-NOW initialization.
- Provides ESP-NOW peer registration/removal.
- Adds small versioned protocol framing.
- Dispatches received protocol frames outside the Wi-Fi callback.
- Uses a fixed-size FreeRTOS receive queue to avoid lengthy Wi-Fi-task work.
- Keeps the initial wire format within ESP-NOW v1's 250-byte interoperability limit.

### System Clock synchronization

- Adds `ESPNowClockSynchronizer`.
- Supports client, reference, and client/reference roles.
- Implements a four-timestamp request/response exchange.
- Captures receive timestamps immediately in the ESP-NOW callback path.
- Converts raw callback timestamps back into the disciplined System Clock time domain.
- Feeds completed samples into ESPressio Timing 2.1's transport-neutral clock-discipline interface.
- Supports periodic synchronization through `Update()`.
- Rejects duplicate/stale responses using synchronization sequence numbers.
- Supports multiple clients synchronizing against one reference.
- Supports hierarchical synchronization through `ClientAndReference` mode.
