#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_Timing.hpp>

using namespace ESPressio;

/*
 * Replace with the Station MAC printed by the ClockReference example.
 */
static const uint8_t ReferenceMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x00
};

ESPNow::ESPNowClockSynchronizer
    clockSynchronizer;


void setup() {
    Serial.begin(115200);

    ESPNow::ESPNowTransportConfig
        transportConfig;

    transportConfig.Channel = 0;

    if (
        !ESPNow::ESPNowTransport::
            GetInstance().
            Initialize(
                transportConfig
            )
    ) {
        Serial.println(
            "Failed to initialize ESP-NOW"
        );

        return;
    }

    Serial.print(
        "Client MAC: "
    );

    Serial.println(
        WiFi.macAddress()
    );

    ESPNow::
        ESPNowClockSynchronizationConfig
            synchronizationConfig;

    synchronizationConfig.Mode =
        ESPNow::
            ESPNowClockSynchronizationMode::
                Client;

    synchronizationConfig.ReferencePeer =
        ESPNow::MacAddress(
            ReferenceMac
        );

    synchronizationConfig.
        SynchronizationIntervalMilliseconds =
            1000;

    synchronizationConfig.AdjustmentMode =
        Timing::
            ClockSynchronizationAdjustmentMode::
                SlewOnly;

    if (
        !clockSynchronizer.Initialize(
            synchronizationConfig
        )
    ) {
        Serial.println(
            "Failed to initialize clock client"
        );

        return;
    }

    /*
     * Send the first exchange immediately.
     */
    clockSynchronizer.
        RequestSynchronization();
}


void loop() {
    /*
     * Update() schedules periodic synchronization requests. Received packets
     * themselves are handled asynchronously by ESPNowTransport.
     */
    clockSynchronizer.Update();

    static uint32_t
        lastReport = 0;

    if (
        millis() -
            lastReport >=
        2000
    ) {
        lastReport =
            millis();

        const auto status =
            clockSynchronizer.
                GetSynchronizationStatus();

        Serial.printf(
            "state=%u offset=%lldns rtt=%lluns drift=%.3fppm accepted=%u rejected=%u\n",
            static_cast<unsigned int>(
                status.State
            ),
            static_cast<long long>(
                status.
                    FilteredOffsetNanoseconds
            ),
            static_cast<
                unsigned long long
            >(
                status.
                    LastRoundTripDelayNanoseconds
            ),
            status.EstimatedDriftPpm,
            status.AcceptedSampleCount,
            status.RejectedSampleCount
        );
    }

    delay(10);
}
