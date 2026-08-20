#include <Arduino.h>
#include <WiFi.h>

#include <ESPressio_ESPNow.hpp>
#include <ESPressio_ESPNowSecureTransport.hpp>
#include <ESPressio_Security.hpp>

using namespace ESPressio;

static const uint8_t RemoteMacBytes[6] = {0x24,0x6F,0x28,0x00,0x00,0x01};
static constexpr uint8_t DemoProtocol = 70;

Security::AES256GCMCipher cipher;
Security::AeadCipherRegistry ciphers;
Security::StaticKeyProvider keys;
Security::ESP32RandomSource randomSource;
Security::TransportSecurity* security = nullptr;
ESPNow::ESPNowSecureTransport secureTransport;

void setup() {
    Serial.begin(115200);
    delay(500);

    auto& raw = ESPNow::ESPNowTransport::GetInstance();
    if (!raw.Initialize()) {
        Serial.println("ESP-NOW initialization failed");
        return;
    }

    ESPNow::ESPNowPeerConfig peer;
    peer.Address = ESPNow::MacAddress(RemoteMacBytes);
    peer.Encrypt = false; // ESPressio Security protects the application payload.
    raw.AddPeer(peer);

    ciphers.Register(cipher);
    const uint8_t demoKey[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
    };
    keys.Add(1, Security::AeadAlgorithm::AES256GCM, demoKey, sizeof(demoKey));

    Security::TransportSecurityConfig config;
    config.Policy = Security::TransportSecurityPolicy::Required;
    config.OutboundAlgorithm = Security::AeadAlgorithm::AES256GCM;
    config.OutboundKeyID = 1;
    config.SenderID = ESP.getEfuseMac();

    static Security::TransportSecurity securityInstance(ciphers, keys, randomSource, config);
    security = &securityInstance;

    secureTransport.SetReceiveHandler([](const ESPNow::ESPNowReceivedFrame& frame, const Security::UnprotectedPayload& opened) {
        Serial.printf("secure RX protocol=%u sender=%llu session=%llu sequence=%llu bytes=%u\n",
            opened.Protocol,
            static_cast<unsigned long long>(opened.SenderID),
            static_cast<unsigned long long>(opened.SessionID),
            static_cast<unsigned long long>(opened.Sequence),
            static_cast<unsigned>(opened.Data.size()));
        (void)frame;
    });

    secureTransport.SetSecurityFailureHandler([](const ESPNow::ESPNowReceivedFrame&, const Security::SecurityResult& result) {
        Serial.printf("secure RX rejected error=%u\n", static_cast<unsigned>(result.Error));
    });

    if (!secureTransport.Initialize(*security)) {
        Serial.println("Secure transport initialization failed");
        return;
    }

    const char message[] = "authenticated ESP-NOW payload";
    Security::SecurityResult result;
    const bool sent = secureTransport.Send(peer.Address, DemoProtocol, message, sizeof(message) - 1, &result);
    Serial.printf("secure send=%s protected=%s\n", sent ? "OK" : "FAILED", result.Protected ? "yes" : "no");
}

void loop() {
    delay(1000);
}
