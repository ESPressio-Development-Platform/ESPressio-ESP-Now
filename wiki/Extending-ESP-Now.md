# Extending ESPressio ESP-Now

Extensions should preserve the separation between native callback work, the managed ESPressio worker, logical peer topology and higher-level domain integrations.

## New protocols

Protocol handlers should execute on the ESP-NOW worker rather than inside the native receive callback. Keep framing validation and queued work bounded.

Register periodic/timeout maintenance with the worker so protocol state is not split across unrelated execution contexts.

## New peer behaviours

Preserve the distinction between:

- logical peer configuration;
- native ESP-NOW peer-table state;
- current transport endpoint MAC;
- stable application/device identity.

Do not silently migrate explicitly configured interfaces/channels.

## WiFi coordination

When WiFi is present, treat its radio snapshots/lifecycle as authoritative. New ESP-NOW features must not independently change channel or AP/STA mode behind WiFi's back.

## New integrations

Adapt authoritative upstream contracts rather than reproducing their semantics. Event routing belongs to Event, Command execution to Command, State semantics to State and cryptographic policy to Security.

Keep integrations opt-in.

## Native driver recovery

Prefer least-disruptive peer reconciliation first. A complete `esp_now_deinit()` / `esp_now_init()` cycle should remain an escalation path, with logical configuration retained across the rebuild.

## Testing expectations

Cover native callback bounds, queue saturation, worker serialization, peer reconciliation, explicit/Auto interfaces, STA/AP/APSTA transitions, active scans, channel mismatch, radio-unavailable sends, no-memory failures, and selected integration wire compatibility on real hardware.