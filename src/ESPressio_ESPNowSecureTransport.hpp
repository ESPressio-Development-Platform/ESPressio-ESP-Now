#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Security.hpp>

#include "ESPressio_ESPNowSecurityProtocol.hpp"
#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio::ESPNow {

/// <summary>Protects application payloads with ESPressio Security and carries the resulting envelopes over fragmented ESP-NOW frames.</summary>
class ESPNowSecureTransport final {
public:
    /// <summary>Callback invoked after a complete Security envelope is authenticated/decrypted successfully.</summary>
    using ReceiveHandler = std::function<void(const ESPNowReceivedFrame&, const Security::UnprotectedPayload&)>;
    /// <summary>Callback invoked when a complete Security envelope fails authentication/decryption/replay processing.</summary>
    using SecurityFailureHandler = std::function<void(const ESPNowReceivedFrame&, const Security::SecurityResult&)>;

    /// <summary>Creates a secure transport bound to a non-owning ESP-NOW transport reference.</summary>
    explicit ESPNowSecureTransport(ESPNowTransport& transport = ESPNowTransport::GetInstance())
        : _transport(transport) {}

    /// <summary>Registers the Security protocol handler and binds the TransportSecurity instance used to protect/unprotect payloads.</summary>
    /// <returns>True when the protocol handler registration succeeds.</returns>
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

    /// <summary>Unregisters the Security protocol handler, releases reassembly storage, and detaches the Security provider.</summary>
    void Shutdown() {
        if (!_initialized) return;
        _transport.UnregisterProtocolHandler(static_cast<uint8_t>(ESPNowProtocol::SecureTransport));
        _security = nullptr;
        _initialized = false;
        _nextMessageID = 1;
        _replacementCursor = 0;
        for (auto& slot : _reassembly) slot.State.ReleaseStorage();
    }

    /// <summary>Reports whether the secure protocol handler is currently initialized.</summary>
    bool GetIsInitialized() const noexcept { return _initialized; }

    /// <summary>Sets the callback receiving successfully unprotected application payloads.</summary>
    void SetReceiveHandler(ReceiveHandler handler) { _receiveHandler = std::move(handler); }
    /// <summary>Sets the callback receiving Security failures for complete inbound envelopes.</summary>
    void SetSecurityFailureHandler(SecurityFailureHandler handler) { _failureHandler = std::move(handler); }

    /// <summary>Protects an application payload, fragments the secured envelope, and sends every fragment to a destination peer.</summary>
    /// <param name="destination">Destination ESP-NOW peer.</param>
    /// <param name="protocol">Application protocol identifier carried inside the secured envelope.</param>
    /// <param name="payload">Application bytes to protect.</param>
    /// <param name="payloadLength">Application payload length.</param>
    /// <param name="securityResult">Optional destination for the Security protection result.</param>
    /// <returns>True only when protection succeeds and every fragment is accepted by ESPNowTransport.</returns>
    bool Send(
        const MacAddress& destination,
        uint8_t protocol,
        const void* payload,
        std::size_t payloadLength,
        Security::SecurityResult* securityResult = nullptr
    ) {
        if (!_initialized || _security == nullptr || destination.IsZero() ||
            (payload == nullptr && payloadLength != 0)) return false;

        // The protected envelope may be many ESP-NOW frames long and never
        // requires DMA-capable storage. Keep it in PSRAM-capable System memory
        // from encryption through fragmentation.
        Security::SecurityBuffer secured;
        auto result = _security->Protect(
            protocol,
            static_cast<const uint8_t*>(payload),
            payloadLength,
            secured
        );
        if (securityResult) *securityResult = result;
        if (!result.Success || secured.empty() ||
            secured.size() > ESPNowSecurityProtocol::MaximumEnvelopeBytes) return false;

        uint32_t messageID = _nextMessageID++;
        if (messageID == 0) messageID = _nextMessageID++;

        const std::size_t fragmentCount =
            (secured.size() + ESPNowSecurityProtocol::MaximumFragmentPayload - 1) /
            ESPNowSecurityProtocol::MaximumFragmentPayload;
        if (fragmentCount == 0 ||
            fragmentCount > ESPNowSecurityProtocol::MaximumFragments) return false;

        // Reuse one PSRAM-backed encoded fragment rather than materializing all
        // fragments or allocating one ordinary vector per send iteration.
        ESPNowSecurityProtocol::ByteBuffer encoded;
        encoded.reserve(MaximumFrameSize);
        for (std::size_t index = 0; index < fragmentCount; ++index) {
            const std::size_t offset =
                index * ESPNowSecurityProtocol::MaximumFragmentPayload;
            const std::size_t bytes = std::min(
                ESPNowSecurityProtocol::MaximumFragmentPayload,
                secured.size() - offset
            );
            if (!ESPNowSecurityProtocol::EncodeFragmentPayload(
                    protocol,
                    messageID,
                    static_cast<uint16_t>(index),
                    static_cast<uint16_t>(fragmentCount),
                    secured.data() + offset,
                    bytes,
                    encoded)) return false;
            if (!_transport.Send(
                    destination,
                    static_cast<uint8_t>(ESPNowProtocol::SecureTransport),
                    encoded.data(),
                    encoded.size())) return false;
        }
        return true;
    }

private:
    struct ReassemblySlot {
        ESPNowSecurityProtocol::ReassemblyState State;
    };

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

        Security::UnprotectedPayload opened;
        Security::SecurityResult result;

        // DecodeFragment borrows directly from ESPNowReceivedFrame. Only the
        // one required copy into retained reassembly storage is performed for
        // fragmented envelopes, and that storage is ExternalPreferred.
        {
            ESPNowSecurityProtocol::Fragment fragment;
            if (!ESPNowSecurityProtocol::DecodeFragment(
                    frame.Payload,
                    frame.PayloadLength,
                    fragment)) return;

            ESPNowSecurityProtocol::ByteBuffer envelope;
            auto& slot = SlotFor(frame.Source, fragment.MessageID);
            const uint8_t protocol = fragment.ApplicationProtocol;
            if (!ESPNowSecurityProtocol::AcceptFragment(
                    slot.State,
                    frame.Source,
                    fragment,
                    envelope) || envelope.empty()) return;

            result = _security->Unprotect(
                protocol,
                envelope.data(),
                envelope.size(),
                opened
            );
        }

        if (!result.Success) {
            if (_failureHandler) _failureHandler(frame, result);
            return;
        }
        if (_receiveHandler) _receiveHandler(frame, opened);
    }
};

}