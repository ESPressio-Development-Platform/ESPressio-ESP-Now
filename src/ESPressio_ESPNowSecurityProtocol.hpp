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

class ESPNowSecurityProtocol final {
public:
    static constexpr uint32_t Magic = 0x53454345u; // "ECES" little-endian on wire
    static constexpr uint8_t Version = 1;
    static constexpr std::size_t HeaderSize = 15;
    static constexpr std::size_t MaximumFragmentPayload = MaximumFrameSize - 8 - HeaderSize;
    static constexpr std::size_t MaximumFragments = 8;
    static constexpr std::size_t MaximumEnvelopeBytes = MaximumFragmentPayload * MaximumFragments;

    struct Fragment {
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Index = 0;
        uint16_t Count = 0;
        std::vector<uint8_t> Data;
    };

    struct ReassemblyState {
        MacAddress Source;
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Count = 0;
        std::array<std::vector<uint8_t>, MaximumFragments> Fragments;
        std::array<bool, MaximumFragments> Present{};
        std::size_t Received = 0;

        void Reset() {
            Source = MacAddress(); ApplicationProtocol = 0; MessageID = 0; Count = 0; Received = 0;
            Present.fill(false);
            for (auto& f : Fragments) f.clear();
        }
    };

    static bool EncodeFragment(const Fragment& fragment, std::vector<uint8_t>& output) {
        output.clear();
        if (fragment.Count == 0 || fragment.Count > MaximumFragments || fragment.Index >= fragment.Count || fragment.Data.size() > MaximumFragmentPayload) return false;
        output.reserve(HeaderSize + fragment.Data.size());
        Append32(output, Magic); output.push_back(Version); output.push_back(0); output.push_back(fragment.ApplicationProtocol);
        Append32(output, fragment.MessageID); Append16(output, fragment.Index); Append16(output, fragment.Count);
        output.insert(output.end(), fragment.Data.begin(), fragment.Data.end());
        return true;
    }

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

    static bool FragmentEnvelope(uint8_t applicationProtocol, uint32_t messageID, const uint8_t* data, std::size_t size, std::vector<std::vector<uint8_t>>& frames) {
        frames.clear();
        if ((data == nullptr && size != 0) || size == 0 || size > MaximumEnvelopeBytes) return false;
        const std::size_t count = (size + MaximumFragmentPayload - 1) / MaximumFragmentPayload;
        if (count == 0 || count > MaximumFragments) return false;
        frames.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t offset = i * MaximumFragmentPayload;
            const std::size_t bytes = std::min(MaximumFragmentPayload, size - offset);
            Fragment f; f.ApplicationProtocol = applicationProtocol; f.MessageID = messageID; f.Index = static_cast<uint16_t>(i); f.Count = static_cast<uint16_t>(count); f.Data.assign(data + offset, data + offset + bytes);
            std::vector<uint8_t> encoded; if (!EncodeFragment(f, encoded)) return false; frames.push_back(std::move(encoded));
        }
        return true;
    }

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
