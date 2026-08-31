#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ESPressio_ESPNowTypes.hpp>

using namespace ESPressio;

namespace {

struct ReleaseProbe {
    unsigned Calls = 0;
    uint16_t Slot = 0;
};

void Release(void* owner, uint16_t slot) noexcept {
    auto* probe = static_cast<ReleaseProbe*>(owner);
    ++probe->Calls;
    probe->Slot = slot;
}

} // namespace

int main() {
    static_assert(ESPNow::MacAddressLength == 6);
    static_assert(ESPNow::MaximumFrameSize == 250);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::ClockSynchronization) == 1);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::EventTransport) == 2);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::CommandTransport) == 3);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::UserBase) == 64);

    static_assert(!std::is_copy_constructible_v<ESPNow::ESPNowReceivedFrameLease>);
    static_assert(!std::is_copy_assignable_v<ESPNow::ESPNowReceivedFrameLease>);
    static_assert(std::is_nothrow_move_constructible_v<ESPNow::ESPNowReceivedFrameLease>);
    static_assert(std::is_nothrow_move_assignable_v<ESPNow::ESPNowReceivedFrameLease>);

    ESPNow::MacAddress zero;
    assert(zero.IsZero());

    const uint8_t bytesA[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t bytesB[6] = {1, 2, 3, 4, 5, 7};

    ESPNow::MacAddress a(bytesA);
    ESPNow::MacAddress a2(bytesA);
    ESPNow::MacAddress b(bytesB);

    assert(!a.IsZero());
    assert(a == a2);
    assert(a != b);

    ESPNow::ESPNowTransportConfig transport;
    assert(transport.InitializeWiFi);
    assert(transport.Channel == 0);
    assert(transport.ReceiveTaskStackSize == 4096);
    assert(transport.ReceiveQueueLength == 6);

    transport.ReceiveTaskStackSize = 8192;
    transport.ReceiveQueueLength = 12;
    assert(transport.ReceiveTaskStackSize == 8192);
    assert(transport.ReceiveQueueLength == 12);

    ESPNow::ESPNowClockSynchronizationConfig clock;
    assert(clock.Mode == ESPNow::ESPNowClockSynchronizationMode::Disabled);
    assert(clock.SynchronizationIntervalMilliseconds == 1000);
    assert(clock.AdjustmentMode == Timing::ClockSynchronizationAdjustmentMode::SlewOnly);

    // #54: moving a receive lease must transfer only ownership metadata. The
    // frame and payload addresses remain identical and the pool slot is returned
    // exactly once by the final owner.
    ESPNow::ESPNowReceivedFrame frame;
    frame.PayloadLength = 3;
    frame.Payload[0] = 0x11;
    frame.Payload[1] = 0x22;
    frame.Payload[2] = 0x33;
    const auto* const frameAddress = &frame;
    const auto* const payloadAddress = frame.Payload;
    ReleaseProbe probe;

    ESPNow::ESPNowReceivedFrameLease first(&frame, &probe, 7, &Release);
    assert(first);
    ESPNow::ESPNowReceivedFrameLease second(std::move(first));
    assert(!first);
    assert(second);
    assert(&second.Frame() == frameAddress);
    assert(second->Payload == payloadAddress);

    ESPNow::ESPNowReceivedFrameLease third;
    third = std::move(second);
    assert(!second);
    assert(third);
    assert(&third.Frame() == frameAddress);
    assert(third->Payload == payloadAddress);
    assert(probe.Calls == 0);
    third.Reset();
    assert(!third);
    assert(probe.Calls == 1);
    assert(probe.Slot == 7);
    third.Reset();
    assert(probe.Calls == 1);

    return 0;
}
