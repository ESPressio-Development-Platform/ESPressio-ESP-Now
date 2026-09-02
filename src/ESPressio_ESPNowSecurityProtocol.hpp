#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

#include <ESPressio_Memory.hpp>

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

    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using ByteBuffer = System::Memory::ByteVector<ExternalPreferred>;

    /// <summary>Decoded Security protocol fragment and its application-protocol routing metadata.</summary>
    /// <remarks><c>Data</c> is a borrowed view into the encoded frame supplied to <c>DecodeFragment</c> and remains valid only while that source storage remains alive.</remarks>
    struct Fragment {
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Index = 0;
        uint16_t Count = 0;
        const uint8_t* Data = nullptr;
        std::size_t DataSize = 0;
    };

    /// <summary>Reusable bounded reassembly state for one fragmented Security envelope.</summary>
    struct ReassemblyState {
        MacAddress Source;
        uint8_t ApplicationProtocol = 0;
        uint32_t MessageID = 0;
        uint16_t Count = 0;
        std::array<ByteBuffer, MaximumFragments> Fragments;
        std::array<bool, MaximumFragments> Present{};
        std::size_t Received = 0;

        /// <summary>Clears logical reassembly state while retaining externally preferred fragment capacity for steady-state reuse.</summary>
        void Reset() {
            Source = MacAddress();
            ApplicationProtocol = 0;
            MessageID = 0;
            Count = 0;
            Received = 0;
            Present.fill(false);
            for (auto& fragment : Fragments) fragment.clear();
        }

        /// <summary>Clears reassembly state and explicitly returns retained fragment capacity to the active System memory provider.</summary>
        void ReleaseStorage() {
            Reset();
            for (auto& fragment : Fragments) {
                ByteBuffer empty;
                fragment.swap(empty);
            }
        }
    };

    /// <summary>Encodes one indexed Security fragment into caller-selected contiguous byte storage.</summary>
    /// <typeparam name="TBuffer">Vector-compatible byte buffer whose allocator controls encoded-frame placement.</typeparam>
    /// <returns>False when fragment bounds, count/index, or data arguments are invalid.</returns>
    template<typename TBuffer>
    static bool EncodeFragmentPayload(
        uint8_t applicationProtocol,
        uint32_t messageID,
        uint16_t index,
        uint16_t count,
        const uint8_t* data,
        std::size_t size,
        TBuffer& output
    ) {
        output.clear();
        if (count == 0 || count > MaximumFragments || index >= count ||
            size > MaximumFragmentPayload || (data == nullptr && size != 0)) return false;
        output.reserve(HeaderSize + size);
        Append32(output, Magic);
        output.push_back(Version);
        output.push_back(0);
        output.push_back(applicationProtocol);
        Append32(output, messageID);
        Append16(output, index);
        Append16(output, count);
        if (size != 0) output.insert(output.end(), data, data + size);
        return true;
    }

    /// <summary>Encodes a borrowed Fragment value into caller-selected contiguous byte storage.</summary>
    template<typename TBuffer>
    static bool EncodeFragment(const Fragment& fragment, TBuffer& output) {
        return EncodeFragmentPayload(
            fragment.ApplicationProtocol,
            fragment.MessageID,
            fragment.Index,
            fragment.Count,
            fragment.Data,
            fragment.DataSize,
            output
        );
    }

    /// <summary>Validates and decodes one Security fragment wire payload without copying its fragment bytes.</summary>
    /// <remarks>The returned fragment borrows its <c>Data</c> from the supplied input buffer. Consumers that need longer ownership must copy into their own storage.</remarks>
    /// <returns>True when magic/version and fragment bounds are valid.</returns>
    static bool DecodeFragment(const uint8_t* data, std::size_t size, Fragment& fragment) {
        fragment = {};
        if (data == nullptr || size < HeaderSize) return false;
        std::size_t offset = 0;
        uint32_t magic = 0;
        uint8_t version = 0;
        uint8_t flags = 0;
        if (!Read32(data,size,offset,magic) ||
            !Read8(data,size,offset,version) ||
            !Read8(data,size,offset,flags) ||
            !Read8(data,size,offset,fragment.ApplicationProtocol) ||
            !Read32(data,size,offset,fragment.MessageID) ||
            !Read16(data,size,offset,fragment.Index) ||
            !Read16(data,size,offset,fragment.Count)) return false;
        (void)flags;
        if (magic != Magic || version != Version ||
            fragment.Count == 0 || fragment.Count > MaximumFragments ||
            fragment.Index >= fragment.Count ||
            size - offset > MaximumFragmentPayload) return false;
        fragment.Data = data + offset;
        fragment.DataSize = size - offset;
        return true;
    }

    /// <summary>Splits a complete Security envelope into ordered fragment payloads using the caller's frame-container allocator choices.</summary>
    /// <typeparam name="TFrames">Vector-compatible outer container whose value type is a vector-compatible byte buffer.</typeparam>
    /// <returns>False when the envelope is empty, oversized, or otherwise cannot be represented.</returns>
    template<typename TFrames>
    static bool FragmentEnvelope(
        uint8_t applicationProtocol,
        uint32_t messageID,
        const uint8_t* data,
        std::size_t size,
        TFrames& frames
    ) {
        frames.clear();
        if ((data == nullptr && size != 0) || size == 0 || size > MaximumEnvelopeBytes) return false;
        const std::size_t count = (size + MaximumFragmentPayload - 1) / MaximumFragmentPayload;
        if (count == 0 || count > MaximumFragments) return false;
        frames.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t offset = index * MaximumFragmentPayload;
            const std::size_t bytes = std::min(MaximumFragmentPayload, size - offset);
            typename TFrames::value_type encoded;
            if (!EncodeFragmentPayload(
                    applicationProtocol,
                    messageID,
                    static_cast<uint16_t>(index),
                    static_cast<uint16_t>(count),
                    data + offset,
                    bytes,
                    encoded)) return false;
            frames.push_back(std::move(encoded));
        }
        return true;
    }

    /// <summary>Accepts one decoded fragment into reusable external-preferred state and emits the complete ordered envelope once all fragments are present.</summary>
    /// <typeparam name="TBuffer">Vector-compatible completion buffer whose allocator controls final envelope placement.</typeparam>
    /// <param name="completed">Receives the complete envelope only when reassembly finishes.</param>
    /// <returns>True for a valid partial fragment or successful completion; false for invalid bounds or an oversized assembled envelope.</returns>
    template<typename TBuffer>
    static bool AcceptFragment(
        ReassemblyState& state,
        const MacAddress& source,
        const Fragment& fragment,
        TBuffer& completed
    ) {
        completed.clear();
        if (fragment.Count == 0 || fragment.Count > MaximumFragments ||
            fragment.Index >= fragment.Count ||
            (fragment.Data == nullptr && fragment.DataSize != 0) ||
            fragment.DataSize > MaximumFragmentPayload) return false;

        if (state.MessageID == 0 || state.MessageID != fragment.MessageID ||
            state.Source != source || state.Count != fragment.Count ||
            state.ApplicationProtocol != fragment.ApplicationProtocol) {
            state.Reset();
            state.Source = source;
            state.ApplicationProtocol = fragment.ApplicationProtocol;
            state.MessageID = fragment.MessageID;
            state.Count = fragment.Count;
        }

        if (!state.Present[fragment.Index]) {
            auto& destination = state.Fragments[fragment.Index];
            destination.assign(fragment.Data, fragment.Data + fragment.DataSize);
            state.Present[fragment.Index] = true;
            ++state.Received;
        }
        if (state.Received != state.Count) return true;

        std::size_t total = 0;
        for (std::size_t index = 0; index < state.Count; ++index) {
            total += state.Fragments[index].size();
        }
        if (total > MaximumEnvelopeBytes) {
            state.Reset();
            return false;
        }

        completed.reserve(total);
        for (std::size_t index = 0; index < state.Count; ++index) {
            completed.insert(
                completed.end(),
                state.Fragments[index].begin(),
                state.Fragments[index].end()
            );
        }
        state.Reset();
        return true;
    }

private:
    template<typename TBuffer>
    static void Append16(TBuffer& output, uint16_t value) {
        output.push_back(static_cast<uint8_t>(value));
        output.push_back(static_cast<uint8_t>(value >> 8));
    }

    template<typename TBuffer>
    static void Append32(TBuffer& output, uint32_t value) {
        for (int index = 0; index < 4; ++index) {
            output.push_back(static_cast<uint8_t>(value >> (index * 8)));
        }
    }

    static bool Read8(const uint8_t* input, std::size_t size, std::size_t& offset, uint8_t& value) {
        if (offset + 1 > size) return false;
        value = input[offset++];
        return true;
    }

    static bool Read16(const uint8_t* input, std::size_t size, std::size_t& offset, uint16_t& value) {
        if (offset + 2 > size) return false;
        value = static_cast<uint16_t>(input[offset]) |
            (static_cast<uint16_t>(input[offset + 1]) << 8);
        offset += 2;
        return true;
    }

    static bool Read32(const uint8_t* input, std::size_t size, std::size_t& offset, uint32_t& value) {
        if (offset + 4 > size) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            value |= static_cast<uint32_t>(input[offset + index]) << (index * 8);
        }
        offset += 4;
        return true;
    }
};

}