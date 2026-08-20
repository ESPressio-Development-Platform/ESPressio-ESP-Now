#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <ESPressio_Security.hpp>

#include "ESPressio_ESPNowSecurityProtocol.hpp"
#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio::ESPNow {

class ESPNowSecureTransport final {
public:
    using ReceiveHandler = std::function<void(const ESPNowReceivedFrame&, const Security::UnprotectedPayload&)>;
    using SecurityFailureHandler = std::function<void(const ESPNowReceivedFrame&, const Security::SecurityResult&)>;

    explicit ESPNowSecureTransport(ESPNowTransport& transport = ESPNowTransport::GetInstance())
        : _transport(transport) {}

    bool Initialize(Security::TransportSecurity& security) {
        if (_initialized) return true;
        _security = &security;
        const bool registered = _transport.RegisterProtocolHandler(
            static_cast<uint8_t>(ESPNowProtocol::SecureTransport),
            [this](const ESPNowReceivedFrame& frame) { HandleFrame(frame); }
        );
        _initialized = registered;
        return registered;
    }

    void Shutdown() {
        if (!_initialized) return;
        _transport.UnregisterProtocolHandler(static_cast<uint8_t>(ESPNowProtocol::SecureTransport));
        _security = nullptr; _initialized = false; _nextMessageID = 1; _replacementCursor = 0;
        for (auto& slot : _reassembly) slot.State.Reset();
    }

    bool GetIsInitialized() const noexcept { return _initialized; }

    void SetReceiveHandler(ReceiveHandler handler) { _receiveHandler = std::move(handler); }
    void SetSecurityFailureHandler(SecurityFailureHandler handler) { _failureHandler = std::move(handler); }

    bool Send(const MacAddress& destination, uint8_t protocol, const void* payload, std::size_t payloadLength, Security::SecurityResult* securityResult = nullptr) {
        if (!_initialized || _security == nullptr || destination.IsZero() || (payload == nullptr && payloadLength != 0)) return false;
        std::vector<uint8_t> secured;
        auto result = _security->Protect(protocol, static_cast<const uint8_t*>(payload), payloadLength, secured);
        if (securityResult) *securityResult = result;
        if (!result.Success || secured.empty()) return false;
        std::vector<std::vector<uint8_t>> fragments;
        uint32_t messageID = _nextMessageID++;
        if (messageID == 0) messageID = _nextMessageID++;
        if (!ESPNowSecurityProtocol::FragmentEnvelope(protocol, messageID, secured.data(), secured.size(), fragments)) return false;
        for (const auto& fragment : fragments) {
            if (!_transport.Send(destination, static_cast<uint8_t>(ESPNowProtocol::SecureTransport), fragment.data(), fragment.size())) return false;
        }
        return true;
    }

private:
    struct ReassemblySlot { ESPNowSecurityProtocol::ReassemblyState State; };

    ESPNowTransport& _transport;
    Security::TransportSecurity* _security = nullptr;
    ReceiveHandler _receiveHandler;
    SecurityFailureHandler _failureHandler;
    std::array<ReassemblySlot, 8> _reassembly{};
    uint32_t _nextMessageID = 1;
    std::size_t _replacementCursor = 0;
    bool _initialized = false;

    ReassemblySlot& SlotFor(const MacAddress& source, uint32_t messageID) {
        for (auto& slot : _reassembly) {
            if (slot.State.MessageID == messageID && slot.State.Source == source) return slot;
        }
        for (auto& slot : _reassembly) {
            if (slot.State.MessageID == 0) return slot;
        }
        auto& slot = _reassembly[_replacementCursor++ % _reassembly.size()];
        slot.State.Reset();
        return slot;
    }

    void HandleFrame(const ESPNowReceivedFrame& frame) {
        if (_security == nullptr) return;
        ESPNowSecurityProtocol::Fragment fragment;
        if (!ESPNowSecurityProtocol::DecodeFragment(frame.Payload, frame.PayloadLength, fragment)) return;
        std::vector<uint8_t> envelope;
        auto& slot = SlotFor(frame.Source, fragment.MessageID);
        const uint8_t protocol = fragment.ApplicationProtocol;
        if (!ESPNowSecurityProtocol::AcceptFragment(slot.State, frame.Source, fragment, envelope) || envelope.empty()) return;
        Security::UnprotectedPayload opened;
        auto result = _security->Unprotect(protocol, envelope.data(), envelope.size(), opened);
        if (!result.Success) {
            if (_failureHandler) _failureHandler(frame, result);
            return;
        }
        if (_receiveHandler) _receiveHandler(frame, opened);
    }
};

}
