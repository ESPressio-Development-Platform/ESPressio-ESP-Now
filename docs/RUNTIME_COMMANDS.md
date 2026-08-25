# ESP-NOW runtime commands

`ESPressio_ESPNowCommandHandler.hpp` is an optional ESPressio Command integration. It is intentionally not included by the normal `ESPressio_ESPNow.hpp` umbrella because doing so would make ESPressio Command a mandatory core dependency.

Initialize it with the same `ESPNowTransportConfig` used to initialize the transport and provide a radio-channel authority callback when `espnow channel` should be available.

```cpp
ESPNow::ESPNowCommandHandler handler;
ESPNow::ESPNowCommandHandler::Options options;
options.SetRadioChannel = [](uint8_t channel) {
    // Change the physical shared WiFi radio through the application's radio owner.
    return true;
};
handler.Initialize(Command::CommandRegistry::GetInstance(), transport, transportConfig, options);
```

## Commands

- `espnow status` — transport initialization, logical binding, native WiFi mode/channel, worker interval/high-water measurement, and last native send result.
- `espnow config` — initialization settings plus current logical binding. Initialization-only values are explicitly labelled.
- `espnow channel <1-14>` — ask the supplied radio authority to change the physical radio channel, read back the actual native channel, then reconcile ESP-NOW managed peers to it.
- `espnow binding status` — show preferred interface, channel and availability.
- `espnow binding interface <auto|sta|ap>` — change ESP-NOW's preferred interface and reconcile managed peers without changing the native WiFi mode.
- `espnow binding channel <0-14>` — change the logical peer binding channel; `0` preserves the follow-current-radio convention.
- `espnow binding available <true|false>` — deliberately suspend/resume ESP-NOW access to the radio for diagnostics.
- `espnow reconcile` — force native ESP-NOW reinitialization and reconstruction of managed peers from logical state.

## Radio ownership

`espnow channel` never assumes ownership of WiFi itself. Applications must provide the channel setter appropriate to their architecture:

- When ESPressio WiFi is present, route the callback through `WiFiManager::Configure()` so the WiFi/ESP-NOW transition coordinator can suspend and rebuild native ESP-NOW safely.
- In standalone ESP-NOW applications that intentionally own the radio, the callback may use `esp_wifi_set_channel()` directly.
- In infrastructure STA mode, a requested channel may be impossible because the station must follow its associated access point. The callback should fail rather than report a misleading success.

## Initialization-only settings

Worker stack size, receive queue length, worker priority/core, `InitializeWiFi`, and the worker iteration interval are currently initialization-time configuration. They are visible via `espnow config`, but this handler does not pretend they can be safely changed on a live task. A future controlled full transport restart may make some of them mutable.
