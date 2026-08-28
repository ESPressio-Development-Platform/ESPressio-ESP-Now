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

#include <ESPressio_CommandEnvelope.hpp>

#include "ESPressio_ESPNowCommandProtocol.hpp"

namespace ESPressio::ESPNow {

/// <summary>Value-semantic six-byte address used to identify peers within the ESP-NOW Command endpoint.</summary>
struct ESPNowCommandPeerAddress {
    std::array<uint8_t, 6> Bytes{};
    bool operator==(const ESPNowCommandPeerAddress& other) const noexcept { return Bytes == other.Bytes; }
    bool operator<(const ESPNowCommandPeerAddress& other) const noexcept { return Bytes < other.Bytes; }
};

/// <summary>Transport metadata associated with one decoded inbound Command invocation.</summary>
struct ESPNowCommandMetadata {
    const char* Transport = "esp-now";
    ESPNowCommandPeerAddress RemotePeer;
    uint64_t RequestID = 0;
    bool Duplicate = false;
};

/// <summary>Inbound Command invocation paired with its ESP-NOW transport metadata.</summary>
struct ESPNowCommandInvocationContext {
    ESPNowCommandMetadata Metadata;
    Command::CommandInvocation Invocation;
};

/// <summary>Bounds message size, outstanding work, reassembly state, duplicate caching, and timeouts for ESPNowCommandEndpoint.</summary>
struct ESPNowCommandEndpointConfig {
    std::size_t MaximumProtocolPayloadBytes = 242;
    std::size_t MaximumMessageBytes = 4096;
    std::size_t MaximumOutstandingRequests = 8;
    std::size_t MaximumReassemblies = 8;
    std::size_t MaximumDuplicateResults = 8;
    std::size_t MaximumInboundRequests = 8;
    uint64_t RequestTimeoutMilliseconds = 100;
    uint64_t ReassemblyTimeoutMilliseconds = 500;
    uint64_t DuplicateResultLifetimeMilliseconds = 1000;
};

/// <summary>Transport-independent ESP-NOW Command endpoint implementing request correlation, fragmentation/reassembly, duplicate suppression, policy, and timeout handling.</summary>
class ESPNowCommandEndpoint final {
public:
    /// <summary>Callback that sends one fully built protocol fragment to a peer.</summary>
    using SendHandler = std::function<bool(const ESPNowCommandPeerAddress&, const uint8_t*, std::size_t)>;
    /// <summary>Callback invoked when an outbound request completes or times out.</summary>
    using CompletionHandler = std::function<void(const Command::CommandResult&)>;
    /// <summary>Optional policy callback evaluated before an inbound Command is handed to execution.</summary>
    using PolicyHandler = std::function<Command::CommandResult(const ESPNowCommandInvocationContext&)>;
    /// <summary>Observer callback for results produced for inbound Command invocations.</summary>
    using ResultObserver = std::function<void(const ESPNowCommandInvocationContext&, const Command::CommandResult&)>;
    /// <summary>Optional asynchronous handoff callback for accepted inbound Command requests.</summary>
    using InboundRequestHandler = std::function<bool(const ESPNowCommandInvocationContext&)>;

    /// <summary>Initializes the endpoint with bounded resource policy and a fragment-send callback.</summary>
    /// <returns>False when required capacities are zero, protocol payload capacity is too small, or the sender is empty.</returns>
    bool Initialize(Command::CommandRegistry& registry, ESPNowCommandEndpointConfig config, SendHandler sender) {
        Shutdown();
        if (!sender || config.MaximumProtocolPayloadBytes <= ESPNowCommandProtocol::FragmentHeaderSize ||
            config.MaximumMessageBytes == 0 || config.MaximumOutstandingRequests == 0 ||
            config.MaximumReassemblies == 0 || config.MaximumDuplicateResults == 0 ||
            config.MaximumInboundRequests == 0) return false;
        _registry = &registry;
        _config = config;
        _sender = std::move(sender);
        _initialized = true;
        return true;
    }

    /// <summary>Fails pending outbound requests, clears reassembly/duplicate/inbound state, and releases callbacks/registry references.</summary>
    void Shutdown() {
        if (_initialized) {
            for (auto& item : _pending) {
                if (item.Completion) {
                    item.Completion(Command::CommandResult::Error("ESP-NOW Command endpoint shut down", 3));
                }
            }
        }
        _pending.clear();
        _inbound.clear();
        _reassemblies.clear();
        _duplicates.clear();
        _policy = {};
        _observer = {};
        _inboundHandler = {};
        _sender = {};
        _registry = nullptr;
        _initialized = false;
    }

    /// <summary>Reports whether the endpoint is initialized.</summary>
    bool GetIsInitialized() const noexcept { return _initialized; }
    /// <summary>Returns the effective endpoint resource/time-out configuration.</summary>
    const ESPNowCommandEndpointConfig& GetConfig() const noexcept { return _config; }
    /// <summary>Returns the number of outbound requests awaiting responses.</summary>
    std::size_t GetOutstandingRequestCount() const noexcept { return _pending.size(); }
    /// <summary>Returns the number of handed-off inbound requests awaiting completion.</summary>
    std::size_t GetInboundRequestCount() const noexcept { return _inbound.size(); }
    /// <summary>Returns the number of incomplete fragmented messages currently being reassembled.</summary>
    std::size_t GetReassemblyCount() const noexcept { return _reassemblies.size(); }
    /// <summary>Sets the inbound Command policy callback.</summary>
    void SetPolicy(PolicyHandler policy) { _policy = std::move(policy); }
    /// <summary>Sets the observer called for completed inbound Command results.</summary>
    void SetResultObserver(ResultObserver observer) { _observer = std::move(observer); }
    /// <summary>Sets an asynchronous inbound-request handoff callback; when unset, Commands execute synchronously through the registry.</summary>
    void SetInboundRequestHandler(InboundRequestHandler handler) { _inboundHandler = std::move(handler); }

    /// <summary>Allocates a request identifier, encodes/fragments an invocation, sends it, and tracks its completion timeout.</summary>
    /// <returns>True when the request is accepted and all initial fragments are sent.</returns>
    bool Invoke(const ESPNowCommandPeerAddress& peer, const Command::CommandInvocation& invocation, CompletionHandler completion,
                uint64_t nowMilliseconds, uint64_t* requestID = nullptr) {
        if (!_initialized || _registry == nullptr || !_sender || invocation.path.empty() ||
            _pending.size() >= _config.MaximumOutstandingRequests) return false;
        uint64_t id = _nextRequestID++;
        if (id == 0) id = _nextRequestID++;
        std::vector<uint8_t> payload;
        if (!ESPNowCommandProtocol::EncodeRequest(invocation, payload) || payload.empty() ||
            payload.size() > _config.MaximumMessageBytes) return false;
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

    /// <summary>Accepts one Command protocol fragment, validates/reassembles it, and dispatches a completed request or response.</summary>
    /// <returns>True for an accepted partial fragment or successfully handled complete message.</returns>
    bool Receive(const ESPNowCommandPeerAddress& peer, const uint8_t* data, std::size_t size, uint64_t nowMilliseconds) {
        if (!_initialized || data == nullptr) return false;
        ESPNowCommandProtocol::FragmentHeader header;
        const uint8_t* fragmentData = nullptr;
        if (!ESPNowCommandProtocol::ParseFragmentHeader(data, size, header, fragmentData) ||
            header.TotalLength > _config.MaximumMessageBytes) return false;
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
        if (reassembly->TotalLength != header.TotalLength || reassembly->FragmentCount != header.FragmentCount) return false;
        reassembly->LastUpdatedMilliseconds = nowMilliseconds;
        const std::size_t index = header.FragmentIndex;
        if (!reassembly->Received[index]) {
            reassembly->Fragments[index].assign(fragmentData, fragmentData + header.FragmentLength);
            reassembly->Received[index] = true;
            reassembly->ReceivedBytes += header.FragmentLength;
        } else {
            const auto& existing = reassembly->Fragments[index];
            if (existing.size() != header.FragmentLength ||
                !std::equal(existing.begin(), existing.end(), fragmentData)) return false;
        }
        if (reassembly->ReceivedBytes > reassembly->TotalLength) return false;
        if (!std::all_of(reassembly->Received.begin(), reassembly->Received.end(), [](bool value){ return value; })) return true;
        std::vector<uint8_t> complete;
        complete.reserve(reassembly->TotalLength);
        for (const auto& fragment : reassembly->Fragments) {
            complete.insert(complete.end(), fragment.begin(), fragment.end());
        }
        if (complete.size() != reassembly->TotalLength) return false;
        const uint8_t type = reassembly->Type;
        const uint64_t id = reassembly->RequestID;
        RemoveReassembly(reassembly);
        switch (static_cast<ESPNowCommandMessageType>(type)) {
            case ESPNowCommandMessageType::Request:
                return HandleRequest(peer, id, complete, nowMilliseconds);
            case ESPNowCommandMessageType::Response:
                return HandleResponse(peer, id, complete);
        }
        return false;
    }

    /// <summary>Completes a previously handed-off inbound request and sends/caches its response.</summary>
    /// <returns>False when no matching inbound request remains.</returns>
    bool CompleteInbound(
        const ESPNowCommandPeerAddress& peer,
        const Command::CommandResponseEnvelope& response,
        uint64_t nowMilliseconds
    ) {
        if (!_initialized) return false;
        InboundRequest* inbound = FindInbound(peer, response.RequestId);
        if (inbound == nullptr) return false;

        Command::CommandResult result;
        result.success = response.Success;
        result.code = response.Code;
        result.message = response.MessageString();

        ESPNowCommandInvocationContext context = std::move(inbound->Context);
        RemoveInbound(inbound);
        if (_observer) _observer(context, result);
        return SendResponseAndCache(peer, response.RequestId, result, nowMilliseconds);
    }

    /// <summary>Expires timed-out outbound requests, stale fragment reassemblies, and duplicate-response cache entries.</summary>
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
            std::remove_if(
                _reassemblies.begin(),
                _reassemblies.end(),
                [&](const Reassembly& item){
                    return Elapsed(nowMilliseconds, item.LastUpdatedMilliseconds) >=
                        _config.ReassemblyTimeoutMilliseconds;
                }
            ),
            _reassemblies.end()
        );
        _duplicates.erase(
            std::remove_if(
                _duplicates.begin(),
                _duplicates.end(),
                [&](const DuplicateResult& item){
                    return Elapsed(nowMilliseconds, item.CreatedMilliseconds) >=
                        _config.DuplicateResultLifetimeMilliseconds;
                }
            ),
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
    struct InboundRequest {
        ESPNowCommandPeerAddress Peer;
        uint64_t RequestID = 0;
        ESPNowCommandInvocationContext Context;
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
    InboundRequestHandler _inboundHandler;
    std::vector<PendingRequest> _pending;
    std::vector<InboundRequest> _inbound;
    std::vector<Reassembly> _reassemblies;
    std::vector<DuplicateResult> _duplicates;
    uint64_t _nextRequestID = 1;
    bool _initialized = false;

    static uint64_t Elapsed(uint64_t now, uint64_t then) { return now >= then ? now - then : 0; }

    bool SendPayload(const ESPNowCommandPeerAddress& peer, ESPNowCommandMessageType type, uint64_t requestID,
                     const std::vector<uint8_t>& payload) {
        const std::size_t count = ESPNowCommandProtocol::GetFragmentCount(
            payload.size(), _config.MaximumProtocolPayloadBytes
        );
        if (count == 0) return false;
        std::vector<uint8_t> frame;
        frame.reserve(_config.MaximumProtocolPayloadBytes);
        for (std::size_t index = 0; index < count; ++index) {
            if (!ESPNowCommandProtocol::BuildFragment(
                    type, requestID, payload, _config.MaximumProtocolPayloadBytes, index, frame)) return false;
            if (!_sender(peer, frame.data(), frame.size())) return false;
        }
        return true;
    }

    Reassembly* FindReassembly(const ESPNowCommandPeerAddress& peer, uint64_t requestID, uint8_t type) {
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

    InboundRequest* FindInbound(const ESPNowCommandPeerAddress& peer, uint64_t requestID) {
        for (auto& item : _inbound) {
            if (item.Peer == peer && item.RequestID == requestID) return &item;
        }
        return nullptr;
    }

    void RemoveInbound(InboundRequest* target) {
        if (target == nullptr) return;
        const auto index = static_cast<std::size_t>(target - _inbound.data());
        if (index < _inbound.size()) {
            _inbound.erase(_inbound.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    DuplicateResult* StoreDuplicate(const ESPNowCommandPeerAddress& peer, uint64_t requestID,
                                    uint64_t nowMilliseconds, std::vector<uint8_t> encodedResponse) {
        if (_duplicates.size() >= _config.MaximumDuplicateResults) {
            _duplicates.erase(_duplicates.begin());
        }
        DuplicateResult item;
        item.Peer = peer;
        item.RequestID = requestID;
        item.CreatedMilliseconds = nowMilliseconds;
        item.EncodedResponse = std::move(encodedResponse);
        _duplicates.push_back(std::move(item));
        return &_duplicates.back();
    }

    bool HandleRequest(const ESPNowCommandPeerAddress& peer, uint64_t requestID,
                       const std::vector<uint8_t>& payload, uint64_t nowMilliseconds) {
        if (auto* duplicate = FindDuplicate(peer, requestID)) {
            return SendPayload(peer, ESPNowCommandMessageType::Response, requestID, duplicate->EncodedResponse);
        }

        if (FindInbound(peer, requestID) != nullptr) {
            // The same logical request is already queued/executing. Do not
            // execute it twice; completion will be correlated by RequestID.
            return true;
        }

        ESPNowCommandProtocol::Request request;
        if (!ESPNowCommandProtocol::DecodeRequest(requestID, payload.data(), payload.size(), request)) {
            return SendErrorResponse(peer, requestID, "Malformed ESP-NOW Command request", 5, nowMilliseconds);
        }

        ESPNowCommandInvocationContext context;
        context.Metadata.RemotePeer = peer;
        context.Metadata.RequestID = requestID;
        context.Invocation = std::move(request.Invocation);

        if (_policy) {
            const Command::CommandResult policyResult = _policy(context);
            if (!policyResult.success) {
                if (_observer) _observer(context, policyResult);
                return SendResponseAndCache(peer, requestID, policyResult, nowMilliseconds);
            }
        }

        if (_inboundHandler) {
            if (_inbound.size() >= _config.MaximumInboundRequests) {
                return SendErrorResponse(peer, requestID, "Inbound Command capacity exhausted", 6, nowMilliseconds);
            }

            InboundRequest inbound;
            inbound.Peer = peer;
            inbound.RequestID = requestID;
            inbound.Context = context;
            _inbound.push_back(std::move(inbound));

            // The context copy above is intentional: the stored request must
            // exist before invoking a potentially re-entrant handler, while the
            // handler itself observes the stable local context for this call.
            if (_inboundHandler(context)) {
                return true;
            }

            _inbound.pop_back();
            return SendErrorResponse(peer, requestID, "Inbound Command handoff rejected", 7, nowMilliseconds);
        }

        Command::CommandResult result = _registry->Invoke(context.Invocation);
        if (_observer) _observer(context, result);
        return SendResponseAndCache(peer, requestID, result, nowMilliseconds);
    }

    bool SendErrorResponse(const ESPNowCommandPeerAddress& peer, uint64_t requestID, const char* message,
                           int code, uint64_t nowMilliseconds) {
        return SendResponseAndCache(peer, requestID, Command::CommandResult::Error(message, code), nowMilliseconds);
    }

    bool SendResponseAndCache(const ESPNowCommandPeerAddress& peer, uint64_t requestID,
                              const Command::CommandResult& result, uint64_t nowMilliseconds) {
        std::vector<uint8_t> payload;
        if (!ESPNowCommandProtocol::EncodeResponse(result, payload) || payload.empty() ||
            payload.size() > _config.MaximumMessageBytes) return false;
        auto* cached = StoreDuplicate(peer, requestID, nowMilliseconds, std::move(payload));
        return cached != nullptr &&
            SendPayload(peer, ESPNowCommandMessageType::Response, requestID, cached->EncodedResponse);
    }

    bool HandleResponse(const ESPNowCommandPeerAddress& peer, uint64_t requestID, const std::vector<uint8_t>& payload) {
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
};

} // namespace ESPressio::ESPNow