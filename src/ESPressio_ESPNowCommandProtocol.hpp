#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !__has_include(<ESPressio_Command.hpp>)
#error "ESP-NOW Command integration requires ESPressio Command >= 1.0.0 < 2.0.0."
#endif

#include <ESPressio_Command.hpp>

namespace ESPressio::ESPNow {

/// <summary>Identifies request and response payloads carried by the ESP-NOW Command protocol.</summary>
enum class ESPNowCommandMessageType : uint8_t {
    Request = 1,
    Response = 2
};

/// <summary>Encodes, decodes, fragments, and validates transport-neutral Command request/response payloads for ESP-NOW.</summary>
class ESPNowCommandProtocol final {
public:
    /// <summary>Wire magic identifying ESP-NOW Command fragments.</summary>
    static constexpr uint32_t Magic = 0x45434D44u; // ECMD
    /// <summary>Current ESP-NOW Command fragment protocol version.</summary>
    static constexpr uint8_t Version = 1;

#pragma pack(push, 1)
    /// <summary>Fixed wire header prepended to each fragmented Command request or response payload.</summary>
    struct FragmentHeader {
        uint32_t MagicValue = Magic;
        uint8_t VersionValue = Version;
        uint8_t Type = 0;
        uint16_t Reserved = 0;
        uint64_t RequestID = 0;
        uint32_t TotalLength = 0;
        uint16_t FragmentIndex = 0;
        uint16_t FragmentCount = 0;
        uint16_t FragmentLength = 0;
    };
#pragma pack(pop)

    /// <summary>Size in bytes of the fixed Command fragment header.</summary>
    static constexpr std::size_t FragmentHeaderSize = sizeof(FragmentHeader);

    /// <summary>Decoded Command request paired with its transport request identifier.</summary>
    struct Request {
        uint64_t RequestID = 0;
        Command::CommandInvocation Invocation;
    };

    /// <summary>Decoded Command response paired with its originating request identifier.</summary>
    struct Response {
        uint64_t RequestID = 0;
        Command::CommandResult Result;
    };

    /// <summary>Encodes a Command invocation into the protocol request payload representation while preserving the caller's byte-buffer allocator.</summary>
    /// <returns>True when every path/argument/raw string can be represented by the wire format.</returns>
    template<typename TAllocator>
    static bool EncodeRequest(
        const Command::CommandInvocation& invocation,
        std::vector<uint8_t, TAllocator>& output
    ) {
        output.clear();
        if (invocation.path.empty()) return false;
        AppendU16(output, invocation.path.size());
        for (const auto& part : invocation.path) if (!AppendString(output, part)) return false;
        AppendU16(output, invocation.positional.size());
        for (const auto& value : invocation.positional) if (!AppendCommandValue(output, value)) return false;
        AppendU16(output, invocation.named.size());
        for (const auto& item : invocation.named) {
            if (!AppendString(output, item.first)) return false;
            if (!AppendCommandValue(output, item.second)) return false;
        }
        return AppendString(output, invocation.raw);
    }

    /// <summary>Encodes the invocation contained by a decoded/request wrapper while preserving the caller's byte-buffer allocator.</summary>
    template<typename TAllocator>
    static bool EncodeRequest(
        const Request& request,
        std::vector<uint8_t, TAllocator>& output
    ) {
        return EncodeRequest(request.Invocation, output);
    }

    /// <summary>Decodes a request payload and associates it with the supplied transport request identifier.</summary>
    /// <returns>True only when the complete payload is structurally valid and consumed exactly.</returns>
    static bool DecodeRequest(uint64_t requestID, const uint8_t* data, std::size_t size, Request& output) {
        if (data == nullptr && size != 0) return false;
        Reader reader(data, size);
        uint16_t count = 0;
        if (!reader.ReadU16(count) || count == 0) return false;
        Request result;
        result.RequestID = requestID;
        result.Invocation.path.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            Command::CommandString value;
            if (!reader.ReadString(value)) return false;
            result.Invocation.path.push_back(std::move(value));
        }
        if (!reader.ReadU16(count)) return false;
        result.Invocation.positional.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            Command::CommandString value;
            if (!reader.ReadString(value)) return false;
            result.Invocation.positional.emplace_back(std::move(value));
        }
        if (!reader.ReadU16(count)) return false;
        for (uint16_t i = 0; i < count; ++i) {
            Command::CommandString key;
            Command::CommandString value;
            if (!reader.ReadString(key) || !reader.ReadString(value)) return false;
            result.Invocation.named.emplace(std::move(key), Command::CommandValue(std::move(value)));
        }
        if (!reader.ReadString(result.Invocation.raw) || !reader.AtEnd()) return false;
        output = std::move(result);
        return true;
    }

    /// <summary>Encodes a Command result into the protocol response payload representation while preserving the caller's byte-buffer allocator.</summary>
    template<typename TAllocator>
    static bool EncodeResponse(
        const Command::CommandResult& result,
        std::vector<uint8_t, TAllocator>& output
    ) {
        output.clear();
        output.push_back(result.success ? 1u : 0u);
        AppendI32(output, result.code);
        return AppendString(output, result.message);
    }

    /// <summary>Encodes the Command result contained by a response wrapper while preserving the caller's byte-buffer allocator.</summary>
    template<typename TAllocator>
    static bool EncodeResponse(
        const Response& response,
        std::vector<uint8_t, TAllocator>& output
    ) {
        return EncodeResponse(response.Result, output);
    }

    /// <summary>Decodes a response payload and associates it with the supplied request identifier.</summary>
    /// <returns>True only when the complete response payload is valid and consumed exactly.</returns>
    static bool DecodeResponse(uint64_t requestID, const uint8_t* data, std::size_t size, Response& output) {
        if (data == nullptr || size < 5) return false;
        Reader reader(data, size);
        uint8_t success = 0;
        int32_t code = 0;
        std::string message;
        if (!reader.ReadU8(success) || success > 1 || !reader.ReadI32(code) || !reader.ReadString(message) || !reader.AtEnd()) return false;
        output.RequestID = requestID;
        output.Result.success = success != 0;
        output.Result.code = code;
        output.Result.message = std::move(message);
        return true;
    }

    /// <summary>Validates and parses a Command fragment header, returning a borrowed view of the fragment payload.</summary>
    static bool ParseFragmentHeader(const uint8_t* data, std::size_t size, FragmentHeader& header, const uint8_t*& fragmentData) {
        fragmentData = nullptr;
        if (data == nullptr || size < sizeof(FragmentHeader)) return false;
        std::memcpy(&header, data, sizeof(header));
        if (header.MagicValue != Magic || header.VersionValue != Version) return false;
        if (header.Type != static_cast<uint8_t>(ESPNowCommandMessageType::Request) && header.Type != static_cast<uint8_t>(ESPNowCommandMessageType::Response)) return false;
        if (header.FragmentCount == 0 || header.FragmentIndex >= header.FragmentCount || header.TotalLength == 0) return false;
        if (header.FragmentLength > size - sizeof(FragmentHeader) || sizeof(FragmentHeader) + header.FragmentLength != size) return false;
        fragmentData = data + sizeof(FragmentHeader);
        return true;
    }

    /// <summary>Calculates the number of wire fragments required for a payload and transport payload limit.</summary>
    /// <returns>Zero when the payload cannot be represented by the fragment format or the transport payload is too small.</returns>
    static std::size_t GetFragmentCount(std::size_t payloadSize, std::size_t maximumProtocolPayload) {
        if (payloadSize == 0 || maximumProtocolPayload <= sizeof(FragmentHeader) || payloadSize > 0xFFFFFFFFu) return 0;
        const std::size_t fragmentCapacity = maximumProtocolPayload - sizeof(FragmentHeader);
        const std::size_t count = (payloadSize + fragmentCapacity - 1) / fragmentCapacity;
        return count == 0 || count > 0xFFFFu ? 0 : count;
    }

    /// <summary>Builds one indexed fragment for a Command request or response payload without changing either buffer's allocator.</summary>
    /// <returns>True when the requested fragment index and payload can be represented.</returns>
    template<typename TPayloadAllocator, typename TFrameAllocator>
    static bool BuildFragment(
        ESPNowCommandMessageType type,
        uint64_t requestID,
        const std::vector<uint8_t, TPayloadAllocator>& payload,
        std::size_t maximumProtocolPayload,
        std::size_t fragmentIndex,
        std::vector<uint8_t, TFrameAllocator>& frame
    ) {
        const std::size_t fragmentCount = GetFragmentCount(payload.size(), maximumProtocolPayload);
        if (fragmentCount == 0 || fragmentIndex >= fragmentCount) return false;
        const std::size_t fragmentCapacity = maximumProtocolPayload - sizeof(FragmentHeader);
        const std::size_t offset = fragmentIndex * fragmentCapacity;
        const std::size_t remaining = payload.size() - offset;
        const std::size_t count = remaining < fragmentCapacity ? remaining : fragmentCapacity;
        FragmentHeader header;
        header.Type = static_cast<uint8_t>(type);
        header.RequestID = requestID;
        header.TotalLength = static_cast<uint32_t>(payload.size());
        header.FragmentIndex = static_cast<uint16_t>(fragmentIndex);
        header.FragmentCount = static_cast<uint16_t>(fragmentCount);
        header.FragmentLength = static_cast<uint16_t>(count);
        frame.resize(sizeof(header) + count);
        std::memcpy(frame.data(), &header, sizeof(header));
        std::memcpy(frame.data() + sizeof(header), payload.data() + offset, count);
        return true;
    }

    /// <summary>Builds every wire fragment required for a Command request or response payload.</summary>
    /// <returns>Fragments in ascending fragment-index order, or an empty collection when fragmentation is not possible.</returns>
    static std::vector<std::vector<uint8_t>> BuildFragments(
        ESPNowCommandMessageType type,
        uint64_t requestID,
        const std::vector<uint8_t>& payload,
        std::size_t maximumProtocolPayload
    ) {
        std::vector<std::vector<uint8_t>> result;
        const std::size_t fragmentCount = GetFragmentCount(payload.size(), maximumProtocolPayload);
        if (fragmentCount == 0) return result;
        result.reserve(fragmentCount);
        for (std::size_t index = 0; index < fragmentCount; ++index) {
            std::vector<uint8_t> frame;
            if (!BuildFragment(type, requestID, payload, maximumProtocolPayload, index, frame)) return {};
            result.push_back(std::move(frame));
        }
        return result;
    }

private:
    template<typename TAllocator, typename TString>
    static bool AppendString(
        std::vector<uint8_t, TAllocator>& out,
        const TString& value
    ) {
        if (value.size() > 0xFFFFu) return false;
        AppendU16(out, value.size());
        out.insert(out.end(), value.begin(), value.end());
        return true;
    }

    template<typename TAllocator>
    static bool AppendCommandValue(
        std::vector<uint8_t, TAllocator>& out,
        const Command::CommandValue& value
    ) {
        if (value.IsNull()) return false;
        return AppendString(out, value.ToString());
    }

    template<typename TAllocator>
    static void AppendU16(
        std::vector<uint8_t, TAllocator>& out,
        std::size_t value
    ) {
        const uint16_t v = static_cast<uint16_t>(value);
        out.push_back(static_cast<uint8_t>(v & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }

    template<typename TAllocator>
    static void AppendI32(
        std::vector<uint8_t, TAllocator>& out,
        int value
    ) {
        const uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(value));
        out.push_back(static_cast<uint8_t>(v & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    class Reader {
    public:
        Reader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

        bool ReadU8(uint8_t& value) {
            if (offset_ + 1 > size_) return false;
            value = data_[offset_++];
            return true;
        }

        bool ReadU16(uint16_t& value) {
            if (offset_ + 2 > size_) return false;
            value = static_cast<uint16_t>(data_[offset_]) |
                static_cast<uint16_t>(data_[offset_ + 1] << 8);
            offset_ += 2;
            return true;
        }

        bool ReadI32(int32_t& value) {
            if (offset_ + 4 > size_) return false;
            const uint32_t v = static_cast<uint32_t>(data_[offset_]) |
                (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
            value = static_cast<int32_t>(v);
            offset_ += 4;
            return true;
        }

        template<typename TString>
        bool ReadString(TString& value) {
            uint16_t length = 0;
            if (!ReadU16(length) || offset_ + length > size_) return false;
            value.assign(reinterpret_cast<const char*>(data_ + offset_), length);
            offset_ += length;
            return true;
        }

        bool AtEnd() const { return offset_ == size_; }

    private:
        const uint8_t* data_ = nullptr;
        std::size_t size_ = 0;
        std::size_t offset_ = 0;
    };
};

} // namespace ESPressio::ESPNow
