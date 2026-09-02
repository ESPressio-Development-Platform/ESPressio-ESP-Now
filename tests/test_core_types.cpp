#include <cassert>
#include <cstdint>

#include <ESPressio_ESPNowTypes.hpp>

using namespace ESPressio;

int main() {
    static_assert(ESPNow::MacAddressLength == 6);
    static_assert(ESPNow::MaximumFrameSize == 250);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::ClockSynchronization) == 1);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::EventTransport) == 2);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::CommandTransport) == 3);
    static_assert(static_cast<uint8_t>(ESPNow::ESPNowProtocol::UserBase) == 64);

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

    // #47: application Command/Event work now leaves the transport worker via
    // bounded asynchronous handoff. Keep the transport worker at 4096 bytes for
    // the first post-handoff hardware high-water measurement rather than
    // retaining the temporary 6144-byte synchronous-execution safety margin.
    // Applications remain free to request either smaller experimental or larger
    // conservative values after measuring their own workloads.
    transport.ReceiveTaskStackSize = 8192;
    transport.ReceiveQueueLength = 12;
    assert(transport.ReceiveTaskStackSize == 8192);
    assert(transport.ReceiveQueueLength == 12);

    ESPNow::ESPNowClockSynchronizationConfig clock;
    assert(clock.Mode == ESPNow::ESPNowClockSynchronizationMode::Disabled);
    assert(clock.SynchronizationIntervalMilliseconds == 1000);
    assert(clock.AdjustmentMode == Timing::ClockSynchronizationAdjustmentMode::SlewOnly);

    return 0;
}
