#include <cassert>
#include <cstdint>

#include "ESPressio_ESPNowPeerLiveness.hpp"

using namespace ESPressio::ESPNow;

static MacAddress Address(uint8_t suffix) {
    const uint8_t bytes[MacAddressLength] = {0x02, 0x00, 0x00, 0x00, 0x00, suffix};
    return MacAddress(bytes);
}

int main() {
    ESPNowPeerLivenessConfig config;
    config.SuspectAfterNanoseconds = 5'000;
    config.ExpireAfterNanoseconds = 20'000;
    ESPNowPeerLivenessTracker tracker(config);

    const MacAddress peer = Address(1);
    assert(tracker.GetState(peer, 0) == ESPNowPeerLivenessState::Unknown);
    assert(tracker.Observe(peer, 1'000));
    assert(tracker.GetState(peer, 4'999) == ESPNowPeerLivenessState::Alive);
    assert(tracker.GetState(peer, 6'000) == ESPNowPeerLivenessState::Suspect);

    // Any valid peer traffic refreshes liveness; it need not be discovery traffic.
    assert(tracker.Observe(peer, 6'500));
    assert(tracker.GetState(peer, 10'000) == ESPNowPeerLivenessState::Alive);
    assert(tracker.GetState(peer, 12'000) == ESPNowPeerLivenessState::Suspect);
    assert(tracker.GetState(peer, 26'499) == ESPNowPeerLivenessState::Suspect);
    assert(tracker.GetState(peer, 26'500) == ESPNowPeerLivenessState::Expired);

    ESPNowPeerLivenessSnapshot snapshot[2];
    assert(tracker.Snapshot(snapshot, 2, 26'500) == 1);
    assert(snapshot[0].Address == peer);
    assert(snapshot[0].State == ESPNowPeerLivenessState::Expired);

    assert(tracker.Forget(peer));
    assert(tracker.GetState(peer, 30'000) == ESPNowPeerLivenessState::Unknown);

    // Rediscovery/traffic after removal establishes a fresh liveness epoch.
    assert(tracker.Observe(peer, 31'000));
    assert(tracker.GetState(peer, 31'001) == ESPNowPeerLivenessState::Alive);

    return 0;
}
