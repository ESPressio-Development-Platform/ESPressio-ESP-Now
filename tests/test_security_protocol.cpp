#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "ESPressio_ESPNowSecurityProtocol.hpp"

using namespace ESPressio::ESPNow;

int main() {
    static_assert(static_cast<uint8_t>(ESPNowProtocol::ClockSynchronization) == 1);
    static_assert(static_cast<uint8_t>(ESPNowProtocol::EventTransport) == 2);
    static_assert(static_cast<uint8_t>(ESPNowProtocol::CommandTransport) == 3);
    static_assert(static_cast<uint8_t>(ESPNowProtocol::SecureTransport) == 4);
    static_assert(static_cast<uint8_t>(ESPNowProtocol::UserBase) == 64);

    constexpr uint8_t applicationProtocol = 73;
    std::vector<uint8_t> payload(ESPNowSecurityProtocol::MaximumFragmentPayload * 2 + 17);
    for (std::size_t i=0;i<payload.size();++i) payload[i]=static_cast<uint8_t>(i & 0xFFu);

    std::vector<std::vector<uint8_t>> frames;
    assert(ESPNowSecurityProtocol::FragmentEnvelope(applicationProtocol, 42, payload.data(), payload.size(), frames));
    assert(frames.size() == 3);

    // #45: the single-buffer streaming encoder must be wire-identical to the
    // retained compatibility helper that materializes all fragments.
    std::vector<uint8_t> streamed;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const std::size_t offset = index * ESPNowSecurityProtocol::MaximumFragmentPayload;
        const std::size_t bytes = std::min(ESPNowSecurityProtocol::MaximumFragmentPayload, payload.size() - offset);
        assert(ESPNowSecurityProtocol::EncodeFragmentPayload(
            applicationProtocol,
            42,
            static_cast<uint16_t>(index),
            static_cast<uint16_t>(frames.size()),
            payload.data() + offset,
            bytes,
            streamed
        ));
        assert(streamed == frames[index]);
    }

    uint8_t macBytes[6] = {1,2,3,4,5,6};
    MacAddress source(macBytes);
    ESPNowSecurityProtocol::ReassemblyState state;
    std::vector<uint8_t> completed;

    // Deliver out of order and verify outer application protocol survives every fragment.
    for (std::size_t n : {std::size_t{2}, std::size_t{0}, std::size_t{1}}) {
        ESPNowSecurityProtocol::Fragment f;
        assert(ESPNowSecurityProtocol::DecodeFragment(frames[n].data(), frames[n].size(), f));
        assert(f.ApplicationProtocol == applicationProtocol);
        assert(ESPNowSecurityProtocol::AcceptFragment(state, source, f, completed));
    }
    assert(completed == payload);

    // Duplicate fragments do not increment reassembly progress twice.
    ESPNowSecurityProtocol::Fragment first;
    assert(ESPNowSecurityProtocol::DecodeFragment(frames[0].data(), frames[0].size(), first));
    assert(ESPNowSecurityProtocol::AcceptFragment(state, source, first, completed));
    assert(ESPNowSecurityProtocol::AcceptFragment(state, source, first, completed));
    assert(completed.empty());

    // A protocol change for the same message ID starts a distinct/rejected reassembly state rather than mixing payloads.
    auto mismatched = first;
    mismatched.ApplicationProtocol = static_cast<uint8_t>(applicationProtocol + 1);
    assert(ESPNowSecurityProtocol::AcceptFragment(state, source, mismatched, completed));
    assert(state.ApplicationProtocol == mismatched.ApplicationProtocol);

    // Malformed magic and fragment index are rejected.
    auto malformed = frames[0]; malformed[0] ^= 0x01;
    ESPNowSecurityProtocol::Fragment f;
    assert(!ESPNowSecurityProtocol::DecodeFragment(malformed.data(), malformed.size(), f));
    f = first; f.Index = f.Count;
    std::vector<uint8_t> encoded;
    assert(!ESPNowSecurityProtocol::EncodeFragment(f, encoded));

    std::vector<uint8_t> tooLarge(ESPNowSecurityProtocol::MaximumEnvelopeBytes + 1, 0xAA);
    assert(!ESPNowSecurityProtocol::FragmentEnvelope(applicationProtocol, 1, tooLarge.data(), tooLarge.size(), frames));

    state.ReleaseStorage();
    for (const auto& fragment : state.Fragments) assert(fragment.capacity() == 0);

    std::cout << "ESP-NOW Security protocol tests passed\n";
}
