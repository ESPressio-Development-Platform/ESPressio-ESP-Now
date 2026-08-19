#include <Arduino.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_ESPNowEventTransport.hpp>

#include <ESPressio_Event.hpp>
#include <ESPressio_EventListener.hpp>
#include <ESPressio_EventTransport.hpp>
#include <ESPressio_Serializable.hpp>

using namespace ESPressio;

/*
 * Replace with the MAC address of the other ESP32.
 *
 * Load the same example on two devices, changing RemoteMac on each device to
 * the station MAC of the other device.
 */
static const uint8_t RemoteMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x01
};


class DistributedCounterEvent :
    public Event::Event<>,
    public Serializable::
        SerializableBase<
            DistributedCounterEvent
        > {

public:
    int32_t Counter = 0;

    ESPRESSIO_SERIALIZABLE_TYPE(
        DistributedCounterEvent
    )

    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)

    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY(
            "counter",
            Counter
        )
    )
};


ESPRESSIO_EVENT_TRANSPORT_TYPE(
    DistributedCounterEvent,
    "flowduino.example.espnow.distributed-counter.v1"
)


class CounterListener final :
    public Event::EventListener {

private:
    Event::EventListenerHandlePtr
        _handle;

public:
    CounterListener() {
        _handle =
            RegisterListener<
                DistributedCounterEvent
            >(
                [](
                    DistributedCounterEvent*
                        event,
                    Event::EventDispatchMethod,
                    Event::EventPriority
                ) {
                    const auto context =
                        event->
                            __getDispatchContext();

                    Serial.printf(
                        "Counter=%ld origin=%s message=%llu\n",
                        static_cast<long>(
                            event->Counter
                        ),
                        context.Origin ==
                            Event::EventOrigin::
                                Remote
                            ? "remote"
                            : "local",
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

CounterListener
    counterListener;

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

    ESPNow::ESPNowPeerConfig
        peer;

    peer.Address =
        ESPNow::MacAddress(
            RemoteMac
        );

    peer.Channel = 0;
    peer.Encrypt = false;

    if (!espNow.AddPeer(peer)) {
        Serial.println(
            "Failed to add ESP-NOW peer."
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

    espNowEventTransport.
        AddDestination(
            peer.Address
        );

    auto& eventTransports =
        Event::EventTransportManager::
            GetInstance();

    eventTransports.
        RegisterTransport(
            &espNowEventTransport
        );

    /*
     * Event 5.4 per-transport routing:
     * this Event is bidirectional specifically over ESP-NOW.
     */
    eventTransports.
        RegisterBidirectionalEvent<
            DistributedCounterEvent
        >(
            &espNowEventTransport
        );

    eventTransports.Initialize();

    Serial.print(
        "Local ESP-NOW station MAC: "
    );

    Serial.println(
        WiFi.macAddress()
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
            new DistributedCounterEvent();

        event->Counter =
            static_cast<int32_t>(
                ++counter
            );

        event->Queue();
    }

    delay(10);
}
