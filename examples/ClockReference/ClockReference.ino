#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_Timing.hpp>

using namespace ESPressio;

ESPNow::ESPNowClockSynchronizer
    clockSynchronizer;


void setup() {
    Serial.begin(115200);

    ESPNow::ESPNowTransportConfig
        transportConfig;

    /*
     * 0 means use the currently selected Wi-Fi channel.
     * Set a concrete channel here when the devices are not associated with
     * another Wi-Fi network.
     */
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
        "Reference MAC: "
    );

    Serial.println(
        WiFi.macAddress()
    );

    /*
     * If this device has an external absolute-time source (NTP, RTC, GPS,
     * etc.), establish that time on SystemClock<> before clients begin
     * synchronizing.
     *
     * Otherwise clients simply synchronize to this device's local System
     * Clock epoch.
     */

    ESPNow::
        ESPNowClockSynchronizationConfig
            synchronizationConfig;

    synchronizationConfig.Mode =
        ESPNow::
            ESPNowClockSynchronizationMode::
                Reference;

    if (
        !clockSynchronizer.Initialize(
            synchronizationConfig
        )
    ) {
        Serial.println(
            "Failed to initialize clock reference"
        );
    }
}


void loop() {
    /*
     * Reference requests are handled by ESPNowTransport's receive-processing
     * task. No polling is required.
     */

    delay(1000);
}
