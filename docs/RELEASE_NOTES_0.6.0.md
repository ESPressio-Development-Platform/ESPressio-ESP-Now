# ESPressio ESP-Now 0.6.0

This release relocates all ESP-Now-specific Event lifecycle types and `ESPNowTransportEventBridge` from ESPressio Event into ESPressio ESP-Now, alongside the existing `ESPNowEventTransport`. This removes the reciprocal optional Event/ESP-Now dependency while preserving the existing ESP-Now-specific public header and class names.

The core ESP-Now dependency surface remains Timing and Observable only. Event 6.0.0, Command 0.4.0, and Security 0.3.0 remain opt-in integrations.
