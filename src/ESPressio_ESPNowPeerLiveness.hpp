#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include <ESPressio_Memory.hpp>

#include "ESPressio_ESPNowTypes.hpp"

#ifndef ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS
#define ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS 20
#endif

namespace ESPressio::ESPNow {

/// <summary>Represents the age-derived liveness classification of a known ESP-NOW peer.</summary>
enum class ESPNowPeerLivenessState : uint8_t {
    Unknown = 0,
    Alive = 1,
    Suspect = 2,
    Expired = 3
};

/// <summary>Configures the age thresholds used when classifying peer liveness.</summary>
struct ESPNowPeerLivenessConfig {
    /// <summary>Age after which a previously observed peer is considered suspect.</summary>
    uint64_t SuspectAfterNanoseconds = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    /// <summary>Age after which a previously observed peer is considered expired.</summary>
    uint64_t ExpireAfterNanoseconds = 20ULL * 1000ULL * 1000ULL * 1000ULL;
};

/// <summary>Point-in-time liveness information for one tracked peer.</summary>
struct ESPNowPeerLivenessSnapshot {
    /// <summary>Peer MAC address.</summary>
    MacAddress Address;
    /// <summary>Peer state evaluated at the snapshot time.</summary>
    ESPNowPeerLivenessState State = ESPNowPeerLivenessState::Unknown;
    /// <summary>Monotonic nanosecond timestamp of the most recent observation.</summary>
    uint64_t LastSeenNanoseconds = 0;
};

/// <summary>Tracks last-seen timestamps for a fixed-capacity collection of ESP-NOW peers.</summary>
/// <remarks>Liveness is derived lazily from caller-supplied monotonic time and creates no background task. The bounded peer table is materialized lazily in ESPressio System ExternalPreferred storage so unused trackers remain allocation-free and active trackers do not reserve internal DRAM.</remarks>
class ESPNowPeerLivenessTracker {
private:
    struct PeerRecord {
        bool Used = false;
        MacAddress Address;
        uint64_t LastSeenNanoseconds = 0;
    };

    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using PeerStorage = System::Memory::Vector<
        PeerRecord,
        ExternalPreferred
    >;

    ESPNowPeerLivenessConfig _config;
    mutable PeerStorage _peers;
    mutable std::mutex _mutex;

    bool EnsureStorageLocked() const {
        if (_peers.size() == ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS) return true;
        try {
            _peers.assign(
                ESPRESSIO_ESPNOW_MAX_LIVENESS_PEERS,
                PeerRecord{}
            );
            return true;
        } catch (...) {
            return false;
        }
    }

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
    /// <summary>Creates an allocation-free tracker with the supplied liveness thresholds.</summary>
    /// <param name="config">Thresholds used to classify observed peers.</param>
    explicit ESPNowPeerLivenessTracker(
        const ESPNowPeerLivenessConfig& config = ESPNowPeerLivenessConfig()
    ) : _config(config) {
        if (_config.ExpireAfterNanoseconds < _config.SuspectAfterNanoseconds) {
            _config.ExpireAfterNanoseconds = _config.SuspectAfterNanoseconds;
        }
    }

    /// <summary>Records a peer as observed at the supplied monotonic timestamp.</summary>
    /// <param name="address">Peer MAC address.</param>
    /// <param name="nowNanoseconds">Current monotonic time in nanoseconds.</param>
    /// <returns>True when an existing record was updated or a free record was available.</returns>
    bool Observe(const MacAddress& address, uint64_t nowNanoseconds) {
        if (address.IsZero()) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        if (!EnsureStorageLocked()) return false;
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

    /// <summary>Removes a peer from liveness tracking.</summary>
    /// <returns>True when a matching tracked peer was removed.</returns>
    bool Forget(const MacAddress& address) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!EnsureStorageLocked()) return false;
        for (auto& peer : _peers) {
            if (peer.Used && peer.Address == address) {
                peer = PeerRecord();
                return true;
            }
        }
        return false;
    }

    /// <summary>Evaluates the current liveness state for a peer.</summary>
    /// <param name="address">Peer to inspect.</param>
    /// <param name="nowNanoseconds">Current monotonic time in nanoseconds.</param>
    /// <returns>The derived liveness state, or Unknown when the peer has not been tracked or storage is unavailable.</returns>
    ESPNowPeerLivenessState GetState(const MacAddress& address, uint64_t nowNanoseconds) const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!EnsureStorageLocked()) return ESPNowPeerLivenessState::Unknown;
        for (const auto& peer : _peers) {
            if (peer.Used && peer.Address == address) return StateFor(peer, nowNanoseconds);
        }
        return ESPNowPeerLivenessState::Unknown;
    }

    /// <summary>Copies current liveness information for tracked peers into caller-owned storage.</summary>
    /// <param name="output">Destination array.</param>
    /// <param name="capacity">Number of snapshot elements available in the destination.</param>
    /// <param name="nowNanoseconds">Current monotonic time used to classify each peer.</param>
    /// <returns>The number of snapshot elements written.</returns>
    std::size_t Snapshot(
        ESPNowPeerLivenessSnapshot* output,
        std::size_t capacity,
        uint64_t nowNanoseconds
    ) const {
        if (output == nullptr || capacity == 0) return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        if (!EnsureStorageLocked()) return 0;
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
