#include <Arduino.h>
#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_ESPNowEventTransport.hpp>

#include <ESPressio_Event.hpp>
#include <ESPressio_EventListener.hpp>
#include <ESPressio_EventTransport.hpp>
#include <ESPressio_Serializable.hpp>

using namespace ESPressio;


/*
 * ESP-NOW broadcast address.
 *
 * Every ESP32 running this example on the same Wi-Fi channel and with the
 * same ESPressio Event Transport registration can receive the Event.
 */
static const uint8_t BroadcastMac[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};


/*
 * Every transmitted Event includes the originating ESP32 station MAC so an
 * application can distinguish which device generated the Event.
 */
class BroadcastCounterEvent :
    public Event::Event<>,
    public Serializable::
        SerializableBase<
            BroadcastCounterEvent
        > {

public:
    uint8_t OriginMac[6] = {
        0, 0, 0, 0, 0, 0
    };

    uint32_t Counter = 0;


    ESPRESSIO_SERIALIZABLE_TYPE(
        BroadcastCounterEvent
    )

    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)

    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY(
            "originMac",
            OriginMac
        ),
        ESPRESSIO_PROPERTY(
            "counter",
            Counter
        )
    )
};


ESPRESSIO_EVENT_TRANSPORT_TYPE(
    BroadcastCounterEvent,
    "flowduino.example.espnow.broadcast-counter.v1"
)


class BroadcastCounterListener final :
    public Event::EventListener {

private:
    Event::EventListenerHandlePtr
        _handle;

public:
    BroadcastCounterListener() {
        _handle =
            RegisterListener<
                BroadcastCounterEvent
            >(
                [](
                    BroadcastCounterEvent*
                        event,
                    Event::EventDispatchMethod,
                    Event::EventPriority
                ) {
                    const auto context =
                        event->
                            __getDispatchContext();

                    Serial.printf(
                        "%s Event from "
                        "%02X:%02X:%02X:%02X:%02X:%02X"
                        " counter=%lu message=%llu\n",
                        context.Origin ==
                            Event::EventOrigin::
                                Remote
                            ? "REMOTE"
                            : "LOCAL",
                        event->OriginMac[0],
                        event->OriginMac[1],
                        event->OriginMac[2],
                        event->OriginMac[3],
                        event->OriginMac[4],
                        event->OriginMac[5],
                        static_cast<unsigned long>(
                            event->Counter
                        ),
                        static_cast<
                            unsigned long long
                        >(
                            context.
                                TransportMessageID
                        )
                    );
                }
            );
    }
};


ESPNow::ESPNowEventTransport
    espNowEventTransport;

BroadcastCounterListener
    broadcastCounterListener;

uint8_t localMac[6] = {
    0, 0, 0, 0, 0, 0
};

uint32_t counter = 0;
uint32_t lastSend = 0;


void setup() {
    Serial.begin(115200);

    auto& espNow =
        ESPNow::ESPNowTransport::
            GetInstance();

    ESPNow::ESPNowTransportConfig
        transportConfig;

    transportConfig.InitializeWiFi =
        true;

    /*
     * Every device participating in the broadcast group must operate on the
     * same Wi-Fi channel.
     *
     * Channel 6 is arbitrary for this example; choose a suitable common
     * channel for your deployment.
     */
    transportConfig.Channel = 6;

    if (
        !espNow.Initialize(
            transportConfig
        )
    ) {
        Serial.println(
            "ESP-NOW initialization failed."
        );

        return;
    }

    WiFi.macAddress(
        localMac
    );

    Serial.printf(
        "Local station MAC: "
        "%02X:%02X:%02X:%02X:%02X:%02X\n",
        localMac[0],
        localMac[1],
        localMac[2],
        localMac[3],
        localMac[4],
        localMac[5]
    );


    /*
     * ESP-NOW requires the broadcast address to be registered as a peer
     * before transmitting broadcast frames.
     */
    ESPNow::ESPNowPeerConfig
        broadcastPeer;

    broadcastPeer.Address =
        ESPNow::MacAddress(
            BroadcastMac
        );

    broadcastPeer.Channel = 6;
    broadcastPeer.Encrypt = false;

    if (
        !espNow.AddPeer(
            broadcastPeer
        )
    ) {
        Serial.println(
            "Failed to add ESP-NOW broadcast peer."
        );

        return;
    }


    if (
        !espNowEventTransport.
            Initialize(
                espNow
            )
    ) {
        Serial.println(
            "Event Transport initialization failed."
        );

        return;
    }

    /*
     * One broadcast destination replaces a list of individual remote MACs.
     */
    if (
        !espNowEventTransport.
            AddDestination(
                broadcastPeer.Address
            )
    ) {
        Serial.println(
            "Failed to add broadcast Event destination."
        );

        return;
    }


    auto& eventTransports =
        Event::EventTransportManager::
            GetInstance();

    eventTransports.
        RegisterTransport(
            &espNowEventTransport
        );

    /*
     * The same Event type is permitted both inbound and outbound over this
     * concrete ESP-NOW transport.
     */
    eventTransports.
        RegisterBidirectionalEvent<
            BroadcastCounterEvent
        >(
            &espNowEventTransport
        );

    eventTransports.Initialize();

    Serial.println(
        "Broadcast Event Transport ready."
    );
}


void loop() {
    const uint32_t now =
        millis();

    if (
        now - lastSend >=
        5000
    ) {
        lastSend = now;

        auto* event =
            new BroadcastCounterEvent();

        memcpy(
            event->OriginMac,
            localMac,
            sizeof(localMac)
        );

        event->Counter =
            ++counter;

        /*
         * This first dispatches locally.
         *
         * EventTransportManager observes the locally-originated dispatch and
         * sends it through ESPNowEventTransport, whose sole destination is
         * the ESP-NOW broadcast address.
         *
         * Every other device running this example receives and re-dispatches
         * it locally with EventOrigin::Remote. Event's loop-prevention logic
         * prevents that remote copy from being broadcast again.
         */
        event->Queue();
    }

    delay(10);
}
