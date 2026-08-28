# ESPressio ESP-Now 1.0.0

ESPressio ESP-Now provides ESP-NOW transport infrastructure and distributed ESPressio integrations for the ESPressio Development Platform.

It owns ESP-NOW peer/topology management, bounded receive processing, detailed send diagnostics, clock-synchronization transport and optional Event, Command, Security and State adapters.

## Shared-radio rule

ESP-NOW and conventional WiFi share the same physical ESP32 2.4 GHz radio. When ESPressio WiFi is present, **WiFi is the authority for radio mode and channel; ESP-Now follows that state**.

This rule drives peer interface reconciliation, channel handling, scan suspension and send availability.

## Start here

- [Getting Started](Getting-Started)
- [Transport and Worker Model](Transport-and-Worker-Model)
- [Peers and Interfaces](Peers-and-Interfaces)
- [WiFi Coexistence](WiFi-Coexistence)
- [Sending and Diagnostics](Sending-and-Diagnostics)
- [Clock Synchronization](Clock-Synchronization)
- [Event, Command and Security Integrations](Integrations)
- [State Transport](State-Transport)
- [Memory and Performance](Memory-and-Performance)
- [Extending ESP-Now](Extending-ESP-Now)
- [API Map](API-Map)

## Version baseline

This Wiki documents the intended ESPressio **1.0.0** baseline from `bugfix/39-wifi-coexistence`. Historical pre-1.0 version labels are intentionally not carried into this documentation.