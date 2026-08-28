#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "ESPressio_ESPNowTypes.hpp"

namespace ESPressio::ESPNow {

/// <summary>Fragments and reassembles Security envelopes for carriage within bounded ESP-NOW protocol payloads.</summary>
class ESPNowSecurityProtocol final {
public:
    /// <summary>Wire magic identifying ESP-NOW Security fragments.</summary>
    static constexpr uint32_t Magic = 0x53454345u; // "ECES" little-endian on wire
    /// <summary>Current fragment protocol version.</summary>
    static constexpr uint8_t Version = 1;
    /// <summary>Fixed fragment header length in bytes.</summary>
    static constexpr std::size_t HeaderSize = 15;
    /// <summary>Maximum application bytes carried by one Security fragment.</summary>
    static constexpr std::size_t MaximumFragmentPayload = MaximumFrameSize - 8 - HeaderSize;
    /// <summary>Maximum number of fragments accepted for one Security envelope.</summary>
    static constexpr std::size_t MaximumFragments = 8;
    /// <summary>Maximum complete Security envelope size representable by this protocol.</summary>
    static constexpr std::size_t MaximumEnvelopeBytes = MaximumFragmentPayload * MaximumFragments;

    /// <summary>Decoded Security protocol fragment and its application-protocol routing metadata.</summary>
    struct Fragment {
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Index = 0;
        uint16_t Count = 0;
        std::vector<uint8_t> Data;
    };

    /// <summary>Reusable bounded reassembly state for one fragmented Security envelope.</summary>
    struct ReassemblyState {
        MacAddress Source;
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Count = 0;
        std::array<std::vector<uint8_t>, MaximumFragments> Fragments;
        std::array<bool, MaximumFragments> Present{};
        std::size_t Received = 0;

        /// <summary>Clears logical reassembly state while retaining bounded fragment capacity for steady-state reuse.</summary>
        void Reset() {
            Source = MacAddress(); ApplicationProtocol = 0; MessageID = 0; Count = 0; Received = 0;
            Present.fill(false);
            for (auto& f : Fragments) f.clear();
        }

        /// <summary>Clears reassembly state and explicitly returns retained fragment capacity to the heap.</summary>
        void ReleaseStorage() {
            Reset();
            for (auto& f : Fragments) std::vector<uint8_t>().swap(f);
        }
    };

    /// <summary>Encodes one indexed Security fragment into its wire payload.</summary>
    /// <returns>False when fragment bounds, count/index, or data arguments are invalid.</returns>
    static bool EncodeFragmentPayload(
        uint8_t applicationProtocol,
        uint32_t messageID,
        uint16_t index,
        uint16_t count,
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& output
    ) {
        output.clear();
        if (count == 0 || count > MaximumFragments || index >= count ||
            size > MaximumFragmentPayload || (data == nullptr && size != 0)) return false;
        output.reserve(HeaderSize + size);
        Append32(output, Magic); output.push_back(Version); output.push_back(0); output.push_back(applicationProtocol);
        Append32(output, messageID); Append16(output, index); Append16(output, count);
        if (size != 0) output.insert(output.end(), data, data + size);
        return true;
    }

    /// <summary>Encodes a Fragment value into its wire payload.</summary>
    static bool EncodeFragment(const Fragment& fragment, std::vector<uint8_t>& output) {
        return EncodeFragmentPayload(
            fragment.ApplicationProtocol,
            fragment.MessageID,
            fragment.Index,
            fragment.Count,
            fragment.Data.data(),
            fragment.Data.size(),
            output
        );
    }

    /// <summary>Validates and decodes one Security fragment wire payload.</summary>
    /// <returns>True when magic/version and fragment bounds are valid.</returns>
    static bool DecodeFragment(const uint8_t* data, std::size_t size, Fragment& fragment) {
        fragment = {};
        if (data == nullptr || size < HeaderSize) return false;
        std::size_t o = 0; uint32_t magic = 0; uint8_t version = 0, flags = 0;
        if (!Read32(data,size,o,magic) || !Read8(data,size,o,version) || !Read8(data,size,o,flags) || !Read8(data,size,o,fragment.ApplicationProtocol) ||
            !Read32(data,size,o,fragment.MessageID) || !Read16(data,size,o,fragment.Index) || !Read16(data,size,o,fragment.Count)) return false;
        (void)flags;
        if (magic != Magic || version != Version || fragment.Count == 0 || fragment.Count > MaximumFragments || fragment.Index >= fragment.Count || size - o > MaximumFragmentPayload) return false;
        fragment.Data.assign(data + o, data + size);
        return true;
    }

    /// <summary>Splits a complete Security envelope into ordered ESP-NOW Security fragment payloads.</summary>
    /// <returns>False when the envelope is empty, oversized, or otherwise cannot be represented.</returns>
    static bool FragmentEnvelope(uint8_t applicationProtocol, uint32_t messageID, const uint8_t* data, std::size_t size, std::vector<std::vector<uint8_t>>& frames) {
        frames.clear();
        if ((data == nullptr && size != 0) || size == 0 || size > MaximumEnvelopeBytes) return false;
        const std::size_t count = (size + MaximumFragmentPayload - 1) / MaximumFragmentPayload;
        if (count == 0 || count > MaximumFragments) return false;
        frames.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t offset = i * MaximumFragmentPayload;
            const std::size_t bytes = std::min(MaximumFragmentPayload, size - offset);
            std::vector<uint8_t> encoded;
            if (!EncodeFragmentPayload(
                    applicationProtocol,
                    messageID,
                    static_cast<uint16_t>(i),
                    static_cast<uint16_t>(count),
                    data + offset,
                    bytes,
                    encoded)) return false;
            frames.push_back(std::move(encoded));
        }
        return true;
    }

    /// <summary>Accepts one decoded fragment into reusable state and emits the complete ordered envelope once all fragments are present.</summary>
    /// <param name="completed">Receives the complete envelope only when reassembly finishes.</param>
    /// <returns>True for a valid partial fragment or successful completion; false for invalid bounds or an oversized assembled envelope.</returns>
    static bool AcceptFragment(ReassemblyState& state, const MacAddress& source, const Fragment& fragment, std::vector<uint8_t>& completed) {
        completed.clear();
        if (fragment.Count == 0 || fragment.Count > MaximumFragments || fragment.Index >= fragment.Count) return false;
        if (state.MessageID == 0 || state.MessageID != fragment.MessageID || state.Source != source || state.Count != fragment.Count || state.ApplicationProtocol != fragment.ApplicationProtocol) {
            state.Reset(); state.Source = source; state.ApplicationProtocol = fragment.ApplicationProtocol; state.MessageID = fragment.MessageID; state.Count = fragment.Count;
        }
        if (!state.Present[fragment.Index]) {
            state.Fragments[fragment.Index] = fragment.Data; state.Present[fragment.Index] = true; ++state.Received;
        }
        if (state.Received != state.Count) return true;
        std::size_t total = 0; for (std::size_t i=0;i<state.Count;++i) total += state.Fragments[i].size();
        if (total > MaximumEnvelopeBytes) { state.Reset(); return false; }
        completed.reserve(total); for (std::size_t i=0;i<state.Count;++i) completed.insert(completed.end(), state.Fragments[i].begin(), state.Fragments[i].end());
        state.Reset(); return true;
    }

private:
    static void Append16(std::vector<uint8_t>& o,uint16_t v){o.push_back(static_cast<uint8_t>(v));o.push_back(static_cast<uint8_t>(v>>8));}
    static void Append32(std::vector<uint8_t>& o,uint32_t v){for(int i=0;i<4;++i)o.push_back(static_cast<uint8_t>(v>>(i*8)));}
    static bool Read8(const uint8_t*i,std::size_t s,std::size_t&o,uint8_t&v){if(o+1>s)return false;v=i[o++];return true;}
    static bool Read16(const uint8_t*i,std::size_t s,std::size_t&o,uint16_t&v){if(o+2>s)return false;v=static_cast<uint16_t>(i[o])|(static_cast<uint16_t>(i[o+1])<<8);o+=2;return true;}
    static bool Read32(const uint8_t*i,std::size_t s,std::size_t&o,uint32_t&v){if(o+4>s)return false;v=0;for(int n=0;n<4;++n)v|=static_cast<uint32_t>(i[o+n])<<(n*8);o+=4;return true;}
};

}