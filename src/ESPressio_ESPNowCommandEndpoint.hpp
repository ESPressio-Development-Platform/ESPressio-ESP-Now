#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ESPressio_ESPNowCommandProtocol.hpp"

namespace ESPressio::ESPNow {

struct ESPNowCommandPeerAddress {
    std::array<uint8_t, 6> Bytes{};

    bool operator==(const ESPNowCommandPeerAddress& other) const noexcept {
        return Bytes == other.Bytes;
    }

    bool operator<(const ESPNowCommandPeerAddress& other) const noexcept {
        return Bytes < other.Bytes;
    }
};

struct ESPNowCommandMetadata {
    const char* Transport = "esp-now";
    ESPNowCommandPeerAddress RemotePeer;
    uint64_t RequestID = 0;
    bool Duplicate = false;
};

struct ESPNowCommandInvocationContext {
    ESPNowCommandMetadata Metadata;
    Command::CommandInvocation Invocation;
};

struct ESPNowCommandEndpointConfig {
    std::size_t MaximumProtocolPayloadBytes = 242;
    std::size_t MaximumMessageBytes = 4096;
    std::size_t MaximumOutstandingRequests = 8;
    std::size_t MaximumReassemblies = 8;
    std::size_t MaximumDuplicateResults = 8;
    uint64_t RequestTimeoutMilliseconds = 2000;
    uint64_t ReassemblyTimeoutMilliseconds = 2000;
    uint64_t DuplicateResultLifetimeMilliseconds = 5000;
};

class ESPNowCommandEndpoint final {
public:
    using SendHandler = std::function<bool(
        const ESPNowCommandPeerAddress&,
        const uint8_t*,
        std::size_t
    )>;

    using CompletionHandler = std::function<void(const Command::CommandResult&)>;
    using PolicyHandler = std::function<Command::CommandResult(const ESPNowCommandInvocationContext&)>;
    using ResultObserver = std::function<void(
        const ESPNowCommandInvocationContext&,
        const Command::CommandResult&
    )>;

    bool Initialize(
        Command::CommandRegistry& registry,
        ESPNowCommandEndpointConfig config,
        SendHandler sender
    ) {
        Shutdown();
        if (!sender || config.MaximumProtocolPayloadBytes <= ESPNowCommandProtocol::FragmentHeaderSize ||
            config.MaximumMessageBytes == 0 || config.MaximumOutstandingRequests == 0 ||
            config.MaximumReassemblies == 0 || config.MaximumDuplicateResults == 0) {
            return false;
        }
        _registry = &registry;
        _config = config;
        _sender = std::move(sender);
        _initialized = true;
        return true;
    }

    void Shutdown() {
        if (!_initialized) {
            _registry = nullptr;
            _sender = {};
            return;
        }

        for (auto& item : _pending) {
            if (item.Completion) {
                item.Completion(Command::CommandResult::Error("ESP-NOW Command endpoint shut down", 3));
            }
        }

        _pending.clear();
        _reassemblies.clear();
        _duplicates.clear();
        _policy = {};
        _observer = {};
        _sender = {};
        _registry = nullptr;
        _initialized = false;
    }

    bool GetIsInitialized() const noexcept { return _initialized; }
    const ESPNowCommandEndpointConfig& GetConfig() const noexcept { return _config; }
    std::size_t GetOutstandingRequestCount() const noexcept { return _pending.size(); }
    std::size_t GetReassemblyCount() const noexcept { return _reassemblies.size(); }

    void SetPolicy(PolicyHandler policy) { _policy = std::move(policy); }
    void SetResultObserver(ResultObserver observer) { _observer = std::move(observer); }

    bool Invoke(
        const ESPNowCommandPeerAddress& peer,
        const Command::CommandInvocation& invocation,
        CompletionHandler completion,
        uint64_t nowMilliseconds,
        uint64_t* requestID = nullptr
    ) {
        if (!_initialized || _registry == nullptr || !_sender || invocation.path.empty()) return false;
        if (_pending.size() >= _config.MaximumOutstandingRequests) return false;

        uint64_t id = _nextRequestID++;
        if (id == 0) id = _nextRequestID++;

        ESPNowCommandProtocol::Request request;
        request.RequestID = id;
        request.Invocation = invocation;

        std::vector<uint8_t> payload;
        if (!ESPNowCommandProtocol::EncodeRequest(request, payload) ||
            payload.empty() || payload.size() > _config.MaximumMessageBytes) {
            return false;
        }

        PendingRequest pending;
        pending.Peer = peer;
        pending.RequestID = id;
        pending.StartedMilliseconds = nowMilliseconds;
        pending.Completion = std::move(completion);
        _pending.push_back(std::move(pending));

        if (!SendPayload(peer, ESPNowCommandMessageType::Request, id, payload)) {
            auto handler = std::move(_pending.back().Completion);
            _pending.pop_back();
            if (handler) handler(Command::CommandResult::Error("Failed to send ESP-NOW Command request", 2));
            return false;
        }

        if (requestID) *requestID = id;
        return true;
    }

    bool Receive(
        const ESPNowCommandPeerAddress& peer,
        const uint8_t* data,
        std::size_t size,
        uint64_t nowMilliseconds
    ) {
        if (!_initialized || data == nullptr) return false;

        ESPNowCommandProtocol::FragmentHeader header;
        const uint8_t* fragmentData = nullptr;
        if (!ESPNowCommandProtocol::ParseFragmentHeader(data, size, header, fragmentData)) return false;
        if (header.TotalLength > _config.MaximumMessageBytes) return false;

        auto* reassembly = FindReassembly(peer, header.RequestID, header.Type);
        if (reassembly == nullptr) {
            if (_reassemblies.size() >= _config.MaximumReassemblies) return false;

            Reassembly state;
            state.Peer = peer;
            state.RequestID = header.RequestID;
            state.Type = header.Type;
            state.TotalLength = header.TotalLength;
            state.FragmentCount = header.FragmentCount;
            state.StartedMilliseconds = nowMilliseconds;
            state.LastUpdatedMilliseconds = nowMilliseconds;
            state.Fragments.resize(header.FragmentCount);
            state.Received.assign(header.FragmentCount, false);
            _reassemblies.push_back(std::move(state));
            reassembly = &_reassemblies.back();
        }

        if (reassembly->TotalLength != header.TotalLength ||
            reassembly->FragmentCount != header.FragmentCount) {
            return false;
        }

        reassembly->LastUpdatedMilliseconds = nowMilliseconds;
        const std::size_t index = header.FragmentIndex;

        if (!reassembly->Received[index]) {
            reassembly->Fragments[index].assign(fragmentData, fragmentData + header.FragmentLength);
            reassembly->Received[index] = true;
            reassembly->ReceivedBytes += header.FragmentLength;
        } else {
            const auto& existing = reassembly->Fragments[index];
            if (existing.size() != header.FragmentLength ||
                !std::equal(existing.begin(), existing.end(), fragmentData)) {
                return false;
            }
        }

        if (reassembly->ReceivedBytes > reassembly->TotalLength) return false;
        if (!std::all_of(reassembly->Received.begin(), reassembly->Received.end(), [](bool value) { return value; })) {
            return true;
        }

        std::vector<uint8_t> complete;
        complete.reserve(reassembly->TotalLength);
        for (const auto& fragment : reassembly->Fragments) {
            complete.insert(complete.end(), fragment.begin(), fragment.end());
        }
        if (complete.size() != reassembly->TotalLength) return false;

        const uint8_t type = reassembly->Type;
        const uint64_t id = reassembly->RequestID;
        RemoveReassembly(reassembly);

        if (type == static_cast<uint8_t>(ESPNowCommandMessageType::Request)) {
            return HandleRequest(peer, id, complete, nowMilliseconds);
        }

        return HandleResponse(peer, id, complete);
    }

    void Update(uint64_t nowMilliseconds) {
        if (!_initialized) return;

        for (std::size_t i = 0; i < _pending.size();) {
            if (Elapsed(nowMilliseconds, _pending[i].StartedMilliseconds) >= _config.RequestTimeoutMilliseconds) {
                auto handler = std::move(_pending[i].Completion);
                _pending.erase(_pending.begin() + static_cast<std::ptrdiff_t>(i));
                if (handler) handler(Command::CommandResult::Error("ESP-NOW Command request timed out", 4));
            } else {
                ++i;
            }
        }

        _reassemblies.erase(
            std::remove_if(_reassemblies.begin(), _reassemblies.end(), [&](const Reassembly& item) {
                return Elapsed(nowMilliseconds, item.LastUpdatedMilliseconds) >= _config.ReassemblyTimeoutMilliseconds;
            }),
            _reassemblies.end()
        );

        _duplicates.erase(
            std::remove_if(_duplicates.begin(), _duplicates.end(), [&](const DuplicateResult& item) {
                return Elapsed(nowMilliseconds, item.CreatedMilliseconds) >= _config.DuplicateResultLifetimeMilliseconds;
            }),
            _duplicates.end()
        );
    }

private:
    struct PendingRequest {
        ESPNowCommandPeerAddress Peer;
        uint64_t RequestID = 0;
        uint64_t StartedMilliseconds = 0;
        CompletionHandler Completion;
    };

    struct Reassembly {
        ESPNowCommandPeerAddress Peer;
        uint64_t RequestID = 0;
        uint8_t Type = 0;
        uint32_t TotalLength = 0;
        uint16_t FragmentCount = 0;
        std::size_t ReceivedBytes = 0;
        uint64_t StartedMilliseconds = 0;
        uint64_t LastUpdatedMilliseconds = 0;
        std::vector<std::vector<uint8_t>> Fragments;
        std::vector<bool> Received;
    };

    struct DuplicateResult {
        ESPNowCommandPeerAddress Peer;
        uint64_t RequestID = 0;
        uint64_t CreatedMilliseconds = 0;
        std::vector<uint8_t> EncodedResponse;
    };

    Command::CommandRegistry* _registry = nullptr;
    ESPNowCommandEndpointConfig _config;
    SendHandler _sender;
    PolicyHandler _policy;
    ResultObserver _observer;
    std::vector<PendingRequest> _pending;
    std::vector<Reassembly> _reassemblies;
    std::vector<DuplicateResult> _duplicates;
    uint64_t _nextRequestID = 1;
    bool _initialized = false;

    static uint64_t Elapsed(uint64_t now, uint64_t then) {
        return now >= then ? now - then : 0;
    }

    bool SendPayload(
        const ESPNowCommandPeerAddress& peer,
        ESPNowCommandMessageType type,
        uint64_t requestID,
        const std::vector<uint8_t>& payload
    ) {
        const auto frames = ESPNowCommandProtocol::BuildFragments(
            type,
            requestID,
            payload,
            _config.MaximumProtocolPayloadBytes
        );
        if (frames.empty()) return false;
        for (const auto& frame : frames) {
            if (!_sender(peer, frame.data(), frame.size())) return false;
        }
        return true;
    }

    Reassembly* FindReassembly(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        uint8_t type
    ) {
        for (auto& item : _reassemblies) {
            if (item.Peer == peer && item.RequestID == requestID && item.Type == type) return &item;
        }
        return nullptr;
    }

    void RemoveReassembly(Reassembly* target) {
        if (target == nullptr) return;
        const auto index = static_cast<std::size_t>(target - _reassemblies.data());
        if (index < _reassemblies.size()) {
            _reassemblies.erase(_reassemblies.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    DuplicateResult* FindDuplicate(const ESPNowCommandPeerAddress& peer, uint64_t requestID) {
        for (auto& item : _duplicates) {
            if (item.Peer == peer && item.RequestID == requestID) return &item;
        }
        return nullptr;
    }

    void StoreDuplicate(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        uint64_t nowMilliseconds,
        std::vector<uint8_t> encodedResponse
    ) {
        if (_duplicates.size() >= _config.MaximumDuplicateResults) {
            _duplicates.erase(_duplicates.begin());
        }
        DuplicateResult item;
        item.Peer = peer;
        item.RequestID = requestID;
        item.CreatedMilliseconds = nowMilliseconds;
        item.EncodedResponse = std::move(encodedResponse);
        _duplicates.push_back(std::move(item));
    }

    bool HandleRequest(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        const std::vector<uint8_t>& payload,
        uint64_t nowMilliseconds
    ) {
        if (auto* duplicate = FindDuplicate(peer, requestID)) {
            return SendPayload(peer, ESPNowCommandMessageType::Response, requestID, duplicate->EncodedResponse);
        }

        ESPNowCommandProtocol::Request request;
        if (!ESPNowCommandProtocol::DecodeRequest(requestID, payload.data(), payload.size(), request)) {
            return SendErrorResponse(peer, requestID, "Malformed ESP-NOW Command request", 5, nowMilliseconds);
        }

        ESPNowCommandInvocationContext context;
        context.Metadata.RemotePeer = peer;
        context.Metadata.RequestID = requestID;
        context.Invocation = request.Invocation;

        Command::CommandResult result;
        if (_policy) {
            result = _policy(context);
            if (!result.success) {
                if (_observer) _observer(context, result);
                return SendResponseAndCache(peer, requestID, result, nowMilliseconds);
            }
        }

        result = _registry->Invoke(request.Invocation);
        if (_observer) _observer(context, result);
        return SendResponseAndCache(peer, requestID, result, nowMilliseconds);
    }

    bool HandleResponse(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        const std::vector<uint8_t>& payload
    ) {
        ESPNowCommandProtocol::Response response;
        if (!ESPNowCommandProtocol::DecodeResponse(requestID, payload.data(), payload.size(), response)) return false;

        for (std::size_t i = 0; i < _pending.size(); ++i) {
            if (_pending[i].Peer == peer && _pending[i].RequestID == requestID) {
                auto handler = std::move(_pending[i].Completion);
                _pending.erase(_pending.begin() + static_cast<std::ptrdiff_t>(i));
                if (handler) handler(response.Result);
                return true;
            }
        }

        return false;
    }

    bool SendErrorResponse(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        std::string message,
        int code,
        uint64_t nowMilliseconds
    ) {
        return SendResponseAndCache(
            peer,
            requestID,
            Command::CommandResult::Error(std::move(message), code),
            nowMilliseconds
        );
    }

    bool SendResponseAndCache(
        const ESPNowCommandPeerAddress& peer,
        uint64_t requestID,
        const Command::CommandResult& result,
        uint64_t nowMilliseconds
    ) {
        ESPNowCommandProtocol::Response response;
        response.RequestID = requestID;
        response.Result = result;
        std::vector<uint8_t> encoded;
        if (!ESPNowCommandProtocol::EncodeResponse(response, encoded) || encoded.size() > _config.MaximumMessageBytes) {
            return false;
        }
        StoreDuplicate(peer, requestID, nowMilliseconds, encoded);
        return SendPayload(peer, ESPNowCommandMessageType::Response, requestID, encoded);
    }
};

}
