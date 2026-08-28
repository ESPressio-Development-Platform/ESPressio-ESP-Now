# Getting Started

The core ESP-NOW transport is exposed through the singleton `ESPNowTransport`.

```cpp
#include <ESPressio_ESPNow.hpp>

auto& transport = ESPressio::ESPNow::ESPNowTransport::GetInstance();
transport.Initialize();
```

## Worker-backed processing

Receive callbacks perform bounded frame copying into a queue; validation, protocol handling and maintenance execute on an ESPressio worker. Threads infrastructure must therefore be available before transport initialization.

## Configuration

The transport exposes explicit worker and queue settings, including receive-task stack/priority/core, queue length and minimum worker iteration interval.

Incoming frames wait for the next permitted worker iteration rather than bypassing the configured rate limit.

## With ESPressio WiFi

When WiFi owns the shared radio, configure ESP-NOW to follow it:

```cpp
ESPressio::ESPNow::ESPNowTransportConfig config;
config.InitializeWiFi = false;
config.Channel = 0;
transport.Initialize(config);
```

Then attach the optional WiFi coordinator so ESP-NOW reacts to mode/channel/scan transitions.

## Next steps

Read [Transport and Worker Model](Transport-and-Worker-Model), then [Peers and Interfaces](Peers-and-Interfaces) and [WiFi Coexistence](WiFi-Coexistence) before adding peers in a WiFi-coordinated application.