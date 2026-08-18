#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_Timing.hpp>

using namespace ESPressio;

/*
 * Dual-role mode can be useful in a larger topology where a device
 * synchronizes upstream while also serving downstream clients.
 *
 * Replace this with the MAC of the upstream reference.
 */
static const uint8_t UpstreamReferenceMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x00
};

ESPNow::ESPNowClockSynchronizer
    clockSynchronizer;


void setup() {
    Serial.begin(115200);

    if (
        !ESPNow::ESPNowTransport::
            GetInstance().
            Initialize()
    ) {
        return;
    }

    Serial.print(
        "Dual-role MAC: "
    );

    Serial.println(
        WiFi.macAddress()
    );

    ESPNow::
        ESPNowClockSynchronizationConfig
            config;

    config.Mode =
        ESPNow::
            ESPNowClockSynchronizationMode::
                ClientAndReference;

    config.ReferencePeer =
        ESPNow::MacAddress(
            UpstreamReferenceMac
        );

    /*
     * This unit synchronizes to one upstream reference while answering
     * requests from downstream clients.
     */
    clockSynchronizer.Initialize(
        config
    );
}


void loop() {
    clockSynchronizer.Update();
    delay(10);
}
