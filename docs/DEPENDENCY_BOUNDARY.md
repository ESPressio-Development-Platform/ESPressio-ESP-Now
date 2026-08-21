# ESP-Now dependency boundary

For ESPressio ESP-Now 0.6.0, the core library requires Timing and Observable only. Event, Command, and Security are opt-in integrations and must not be included by the normal `ESPressio_ESPNow.hpp` umbrella.

ESP-Now owns all ESP-Now-specific Event integration, including `ESPNowEventTransport`, ESP-Now lifecycle Event types, and `ESPNowTransportEventBridge`. ESPressio Event 6.0.0 must not depend back on ESP-Now.

This invariant is enforced by CI as part of #17.
