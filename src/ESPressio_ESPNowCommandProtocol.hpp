#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#if !__has_include(<ESPressio_Command.hpp>)
#error "ESP-NOW Command integration requires ESPressio Command >= 1.0.0 < 2.0.0."
#endif

#include <ESPressio_Command.hpp>

namespace ESPressio::ESPNow {

enum class ESPNowCommandMessageType : uint8_t {
    Request = 1,
    Response = 2
};

class ESPNowCommandProtocol final {
public:
    static constexpr uint32_t Magic = 0x45434D44u; // ECMD
    static constexpr uint8_t Version = 1;

#pragma pack(push, 1)
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

    static constexpr std::size_t FragmentHeaderSize = sizeof(FragmentHeader);

    struct Request {
        uint64_t RequestID = 0;
        Command::CommandInvocation Invocation;
    };

    struct Response {
        uint64_t RequestID = 0;
        Command::CommandResult Result;
    };

    static bool EncodeRequest(const Request& request, std::vector<uint8_t>& output) {
        output.clear();
        if (request.Invocation.path.empty()) return false;
        AppendU16(output, request.Invocation.path.size());
        for (const auto& part : request.Invocation.path) if (!AppendString(output, part)) return false;
        AppendU16(output, request.Invocation.positional.size());
        for (const auto& value : request.Invocation.positional) if (!AppendCommandValue(output, value)) return false;
        AppendU16(output, request.Invocation.named.size());
        for (const auto& item : request.Invocation.named) {
            if (!AppendString(output, item.first)) return false;
            if (!AppendCommandValue(output, item.second)) return false;
        }
        return AppendString(output, request.Invocation.raw);
    }

    static bool DecodeRequest(uint64_t requestID, const uint8_t* data, std::size_t size, Request& output) {
        if (data == nullptr && size != 0) return false;
        Reader reader(data, size);
        uint16_t count = 0;
        if (!reader.ReadU16(count) || count == 0) return false;
        Request result;
        result.RequestID = requestID;
        result.Invocation.path.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            std::string value; if (!reader.ReadString(value)) return false;
            result.Invocation.path.push_back(std::move(value));
        }
        if (!reader.ReadU16(count)) return false;
        result.Invocation.positional.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            std::string value; if (!reader.ReadString(value)) return false;
            result.Invocation.positional.emplace_back(std::move(value));
        }
        if (!reader.ReadU16(count)) return false;
        for (uint16_t i = 0; i < count; ++i) {
            std::string key, value;
            if (!reader.ReadString(key) || !reader.ReadString(value)) return false;
            result.Invocation.named.emplace(std::move(key), Command::CommandValue(std::move(value)));
        }
        if (!reader.ReadString(result.Invocation.raw) || !reader.AtEnd()) return false;
        output = std::move(result);
        return true;
    }

    static bool EncodeResponse(const Response& response, std::vector<uint8_t>& output) {
        output.clear();
        output.push_back(response.Result.success ? 1u : 0u);
        AppendI32(output, response.Result.code);
        return AppendString(output, response.Result.message);
    }

    static bool DecodeResponse(uint64_t requestID, const uint8_t* data, std::size_t size, Response& output) {
        if (data == nullptr || size < 5) return false;
        Reader reader(data, size);
        uint8_t success = 0; int32_t code = 0; std::string message;
        if (!reader.ReadU8(success) || success > 1 || !reader.ReadI32(code) || !reader.ReadString(message) || !reader.AtEnd()) return false;
        output.RequestID = requestID;
        output.Result.success = success != 0;
        output.Result.code = code;
        output.Result.message = std::move(message);
        return true;
    }

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

    static std::size_t GetFragmentCount(std::size_t payloadSize, std::size_t maximumProtocolPayload) {
        if (payloadSize == 0 || maximumProtocolPayload <= sizeof(FragmentHeader) || payloadSize > 0xFFFFFFFFu) return 0;
        const std::size_t fragmentCapacity = maximumProtocolPayload - sizeof(FragmentHeader);
        const std::size_t count = (payloadSize + fragmentCapacity - 1) / fragmentCapacity;
        return count == 0 || count > 0xFFFFu ? 0 : count;
    }

    static bool BuildFragment(
        ESPNowCommandMessageType type,
        uint64_t requestID,
        const std::vector<uint8_t>& payload,
        std::size_t maximumProtocolPayload,
        std::size_t fragmentIndex,
        std::vector<uint8_t>& frame
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
    static bool AppendString(std::vector<uint8_t>& out, const std::string& value) {
        if (value.size() > 0xFFFFu) return false;
        AppendU16(out, value.size());
        out.insert(out.end(), value.begin(), value.end());
        return true;
    }
    static bool AppendCommandValue(std::vector<uint8_t>& out, const Command::CommandValue& value) {
        if (value.IsNull()) return false;
        return AppendString(out, value.ToString());
    }
    static void AppendU16(std::vector<uint8_t>& out, std::size_t value) {
        const uint16_t v = static_cast<uint16_t>(value);
        out.push_back(static_cast<uint8_t>(v & 0xFFu)); out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    static void AppendI32(std::vector<uint8_t>& out, int value) {
        const uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(value));
        out.push_back(static_cast<uint8_t>(v & 0xFFu)); out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu)); out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    class Reader {
    public:
        Reader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
        bool ReadU8(uint8_t& value) { if (offset_ + 1 > size_) return false; value = data_[offset_++]; return true; }
        bool ReadU16(uint16_t& value) { if (offset_ + 2 > size_) return false; value = static_cast<uint16_t>(data_[offset_]) | static_cast<uint16_t>(data_[offset_ + 1] << 8); offset_ += 2; return true; }
        bool ReadI32(int32_t& value) {
            if (offset_ + 4 > size_) return false;
            const uint32_t v = static_cast<uint32_t>(data_[offset_]) | (static_cast<uint32_t>(data_[offset_ + 1]) << 8) | (static_cast<uint32_t>(data_[offset_ + 2]) << 16) | (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
            value = static_cast<int32_t>(v); offset_ += 4; return true;
        }
        bool ReadString(std::string& value) { uint16_t length = 0; if (!ReadU16(length) || offset_ + length > size_) return false; value.assign(reinterpret_cast<const char*>(data_ + offset_), length); offset_ += length; return true; }
        bool AtEnd() const { return offset_ == size_; }
    private:
        const uint8_t* data_ = nullptr; std::size_t size_ = 0; std::size_t offset_ = 0;
    };
};

}
