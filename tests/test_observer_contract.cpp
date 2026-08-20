#include <cassert>

#include <ESPressio_IESPNowTransportObserver.hpp>

using namespace ESPressio::ESPNow;

class Observer final : public IESPNowTransportObserver {
public:
    int Initialized = 0;
    int PeerAdded = 0;
    int SendFailed = 0;

    void OnESPNowTransportInitialized() override { ++Initialized; }
    void OnESPNowPeerAdded(const MacAddress&) override { ++PeerAdded; }
    void OnESPNowSendFailed(const MacAddress&, uint8_t, std::size_t) override { ++SendFailed; }
};

int main() {
    Observer observer;
    MacAddress address;
    observer.OnESPNowTransportInitialized();
    observer.OnESPNowPeerAdded(address);
    observer.OnESPNowSendFailed(address, 7, 12);
    assert(observer.Initialized == 1);
    assert(observer.PeerAdded == 1);
    assert(observer.SendFailed == 1);
    return 0;
}
