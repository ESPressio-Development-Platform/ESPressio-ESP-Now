# Clock Synchronization

`ESPNowClockSynchronizer` carries ESPressio Timing synchronization exchanges over ESP-NOW.

The responsibility split is deliberate:

```text
ESP-NOW transport
    captures/carries timestamps
             |
             v
ESPressio Timing
    validates samples
    estimates offset/delay
    disciplines SystemClock
```

ESP-Now does not own clock-discipline algorithms, phase filtering, drift estimation or slew policy.

## Transport role

The synchronizer exchanges the four timestamps required by Timing's two-way synchronization model and submits the completed sample to the Timing synchronization target.

This keeps the same Timing discipline usable over other transports without embedding ESP-NOW concepts into Timing.

## Radio coordination

Clock synchronization is subject to the same WiFi/ESP-NOW shared-radio rules as other ESP-NOW traffic. A scan or radio transition can temporarily make synchronization traffic unavailable; the transport should surface that condition rather than bypassing coordination.

## Precision guidance

Timestamp capture should occur as close as practical to the relevant transport send/receive boundary, while the heavier synchronization calculation remains outside native callback context.

Do not substitute application-loop timestamps for transport-boundary timestamps when sub-millisecond synchronization quality matters.

## Extension rule

Changes to synchronization framing or timestamp capture belong in the ESP-NOW transport adapter. Changes to estimation/discipline policy belong in ESPressio Timing.