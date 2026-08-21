#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "ESPressio_ESPNowTypes.hpp"

#ifndef ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS
#define ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS 20
#endif

namespace ESPressio::ESPNow {

enum class ESPNowPeerLivenessState : uint8_t {
    Unknown = 0,
    Alive = 1,
    Suspect = 2,
    Expired = 3
};

struct ESPNowPeerLivenessConfig {
    uint64_t SuspectAfterNanoseconds = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    uint64_t ExpireAfterNanoseconds = 20ULL * 1000ULL * 1000ULL * 1000ULL;
};

struct ESPNowPeerLivenessSnapshot {
    MacAddress Address;
    ESPNowPeerLivenessState State = ESPNowPeerLivenessState::Unknown;
    uint64_t LastSeenNanoseconds = 0;
};

class ESPNowPeerLivenessTracker {
private:
    struct PeerRecord {
        bool Used = false;
        MacAddress Address;
        uint64_t LastSeenNanoseconds = 0;
    };

    ESPNowPeerLivenessConfig _config;
    std::array<PeerRecord, ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS> _peers{};
    mutable std::mutex _mutex;

    static uint64_t Elapsed(uint64_t now, uint64_t then) noexcept {
        return now >= then ? now - then : 0;
    }

    ESPNowPeerLivenessState StateFor(const PeerRecord& peer, uint64_t now) const noexcept {
        if (!peer.Used) return ESPNowPeerLivenessState::Unknown;
        const uint64_t age = Elapsed(now, peer.LastSeenNanoseconds);
        if (age >= _config.ExpireAfterNanoseconds) return ESPNowPeerLivenessState::Expired;
        if (age >= _config.SuspectAfterNanoseconds) return ESPNowPeerLivenessState::Suspect;
        return ESPNowPeerLivenessState::Alive;
    }

public:
    explicit ESPNowPeerLivenessTracker(
        const ESPNowPeerLivenessConfig& config = ESPNowPeerLivenessConfig()
    ) : _config(config) {
        if (_config.ExpireAfterNanoseconds < _config.SuspectAfterNanoseconds) {
            _config.ExpireAfterNanoseconds = _config.SuspectAfterNanoseconds;
        }
    }

    bool Observe(const MacAddress& address, uint64_t nowNanoseconds) {
        if (address.IsZero()) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        PeerRecord* freeRecord = nullptr;
        for (auto& peer : _peers) {
            if (peer.Used && peer.Address == address) {
                peer.LastSeenNanoseconds = nowNanoseconds;
                return true;
            }
            if (!peer.Used && freeRecord == nullptr) freeRecord = &peer;
        }
        if (freeRecord == nullptr) return false;
        freeRecord->Used = true;
        freeRecord->Address = address;
        freeRecord->LastSeenNanoseconds = nowNanoseconds;
        return true;
    }

    bool Forget(const MacAddress& address) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& peer : _peers) {
            if (peer.Used && peer.Address == address) {
                peer = PeerRecord();
                return true;
            }
        }
        return false;
    }

    ESPNowPeerLivenessState GetState(const MacAddress& address, uint64_t nowNanoseconds) const {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& peer : _peers) {
            if (peer.Used && peer.Address == address) return StateFor(peer, nowNanoseconds);
        }
        return ESPNowPeerLivenessState::Unknown;
    }

    std::size_t Snapshot(
        ESPNowPeerLivenessSnapshot* output,
        std::size_t capacity,
        uint64_t nowNanoseconds
    ) const {
        if (output == nullptr || capacity == 0) return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count = 0;
        for (const auto& peer : _peers) {
            if (!peer.Used || count >= capacity) continue;
            output[count].Address = peer.Address;
            output[count].LastSeenNanoseconds = peer.LastSeenNanoseconds;
            output[count].State = StateFor(peer, nowNanoseconds);
            ++count;
        }
        return count;
    }
};

} // namespace ESPressio::ESPNow
