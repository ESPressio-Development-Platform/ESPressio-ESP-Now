#include <Arduino.h>
#include <WiFi.h>

#include <ESPressio_Command.hpp>
#include <ESPressio_ESPNow.hpp>
#include <ESPressio_ESPNowCommandTransport.hpp>

using namespace ESPressio;

/*
 * Flash the same example onto two ESP32 devices.
 * Replace RemoteMac on each device with the MAC address of the other device.
 *
 * Type 'i' into Serial to invoke the remote "demo increment" Command.
 */

static const uint8_t RemoteMac[] = {
    0x24, 0x6F, 0x28,
    0x00, 0x00, 0x01
};

ESPNow::ESPNowCommandTransport commandTransport;

volatile uint32_t localCounter = 0;

void RegisterCommands() {
    auto& commands =
        Command::CommandRegistry::GetInstance();

    auto& increment =
        commands.Command("demo")
            .Command("increment")
            .Description("Increment the remote demo counter");

    increment.Parameter<uint32_t>("amount")
        .Optional()
        .Default("1")
        .Range(1, 1000);

    increment.OnExecute(
        [](const Command::CommandContext& context) {
            const uint32_t amount =
                context.Get<uint32_t>("amount");

            localCounter += amount;

            Serial.printf(
                "[Command] incremented by %lu; counter=%lu\n",
                static_cast<unsigned long>(amount),
                static_cast<unsigned long>(localCounter)
            );

            return Command::CommandResult::Ok(
                "counter=" + std::to_string(localCounter)
            );
        }
    );
}

void setup() {
    Serial.begin(115200);
    delay(500);

    auto& transport =
        ESPNow::ESPNowTransport::GetInstance();

    ESPNow::ESPNowTransportConfig transportConfig;
    transportConfig.Channel = 0;

    if (!transport.Initialize(transportConfig)) {
        Serial.println("Failed to initialize ESP-NOW transport");
        return;
    }

    Serial.print("Local MAC: ");
    Serial.println(WiFi.macAddress());

    ESPNow::ESPNowPeerConfig peer;
    peer.Address = ESPNow::MacAddress(RemoteMac);

    if (!transport.AddPeer(peer)) {
        Serial.println("Failed to add remote peer");
        return;
    }

    RegisterCommands();

    ESPNow::ESPNowCommandTransportConfig commandConfig;
    commandConfig.Endpoint.RequestTimeoutMilliseconds = 2000;
    commandConfig.Endpoint.MaximumOutstandingRequests = 4;

    if (!commandTransport.Initialize(
        transport,
        Command::CommandRegistry::GetInstance(),
        commandConfig
    )) {
        Serial.println("Failed to initialize Command transport");
        return;
    }

    commandTransport.SetPolicy(
        [](const ESPNow::ESPNowCommandInvocationContext& context) {
            Serial.printf(
                "[Remote Command] request=%llu from %02X:%02X:%02X:%02X:%02X:%02X\n",
                static_cast<unsigned long long>(context.Metadata.RequestID),
                context.Metadata.RemotePeer.Bytes[0],
                context.Metadata.RemotePeer.Bytes[1],
                context.Metadata.RemotePeer.Bytes[2],
                context.Metadata.RemotePeer.Bytes[3],
                context.Metadata.RemotePeer.Bytes[4],
                context.Metadata.RemotePeer.Bytes[5]
            );

            return Command::CommandResult::Ok();
        }
    );

    Serial.println("Ready. Type 'i' to increment the remote counter.");
}

void loop() {
    commandTransport.Update();

    if (Serial.available() > 0) {
        const char input = static_cast<char>(Serial.read());

        if (input == 'i' || input == 'I') {
            Command::CommandInvocation invocation;
            invocation.path = {"demo", "increment"};
            invocation.positional = {"1"};

            uint64_t requestID = 0;

            const bool accepted = commandTransport.Invoke(
                ESPNow::MacAddress(RemoteMac),
                invocation,
                [](const Command::CommandResult& result) {
                    Serial.printf(
                        "[Remote Result] success=%s code=%d message=%s\n",
                        result.success ? "true" : "false",
                        result.code,
                        result.message.c_str()
                    );
                },
                &requestID
            );

            Serial.printf(
                "Invoke %s; request=%llu\n",
                accepted ? "accepted" : "rejected",
                static_cast<unsigned long long>(requestID)
            );
        }
    }

    delay(5);
}
