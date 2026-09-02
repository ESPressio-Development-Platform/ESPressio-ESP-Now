#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <ESPressio_ESPNowCommandEndpoint.hpp>

using namespace ESPressio;

namespace {

using Peer = ESPNow::ESPNowCommandPeerAddress;
using FragmentCollection = ESPNow::ESPNowCommandProtocol::FragmentCollection;

Peer MakePeer(uint8_t value) {
    Peer peer;
    peer.Bytes = {value, value, value, value, value, value};
    return peer;
}

struct Frame {
    Peer Destination;
    std::vector<uint8_t> Data;
};

FragmentCollection BuildRequestFrames(
    uint64_t requestId,
    const Command::CommandInvocation& invocation,
    std::size_t maximumPayload
) {
    ESPNow::ESPNowCommandProtocol::Request request;
    request.RequestID = requestId;
    request.Invocation = invocation;

    std::vector<uint8_t> payload;
    assert(ESPNow::ESPNowCommandProtocol::EncodeRequest(request, payload));
    return ESPNow::ESPNowCommandProtocol::BuildFragments(
        ESPNow::ESPNowCommandMessageType::Request,
        requestId,
        payload,
        maximumPayload
    );
}

Command::CommandInvocation PingInvocation() {
    Command::CommandInvocation invocation;
    invocation.path = {"ping"};
    invocation.raw = "ping";
    return invocation;
}

Command::CommandResult DecodeResponse(
    uint64_t requestId,
    const std::vector<Frame>& frames
) {
    assert(!frames.empty());

    struct Part {
        uint16_t Index = 0;
        std::vector<uint8_t> Data;
    };

    std::vector<Part> parts;
    uint16_t expectedCount = 0;
    uint32_t expectedLength = 0;

    for (const auto& frame : frames) {
        ESPNow::ESPNowCommandProtocol::FragmentHeader header;
        const uint8_t* fragmentData = nullptr;
        assert(ESPNow::ESPNowCommandProtocol::ParseFragmentHeader(
            frame.Data.data(),
            frame.Data.size(),
            header,
            fragmentData
        ));
        assert(header.RequestID == requestId);
        assert(header.Type == static_cast<uint8_t>(ESPNow::ESPNowCommandMessageType::Response));

        if (expectedCount == 0) {
            expectedCount = header.FragmentCount;
            expectedLength = header.TotalLength;
        }
        assert(header.FragmentCount == expectedCount);
        assert(header.TotalLength == expectedLength);

        parts.push_back({
            header.FragmentIndex,
            std::vector<uint8_t>(fragmentData, fragmentData + header.FragmentLength)
        });
    }

    assert(parts.size() == expectedCount);
    std::sort(parts.begin(), parts.end(), [](const Part& left, const Part& right) {
        return left.Index < right.Index;
    });

    std::vector<uint8_t> payload;
    payload.reserve(expectedLength);
    for (const auto& part : parts) {
        payload.insert(payload.end(), part.Data.begin(), part.Data.end());
    }
    assert(payload.size() == expectedLength);

    ESPNow::ESPNowCommandProtocol::Response response;
    assert(ESPNow::ESPNowCommandProtocol::DecodeResponse(
        requestId,
        payload.data(),
        payload.size(),
        response
    ));
    return response.Result;
}

void DeliverRequest(
    ESPNow::ESPNowCommandEndpoint& endpoint,
    const Peer& source,
    const FragmentCollection& frames,
    uint64_t now
) {
    for (const auto& frame : frames) {
        assert(endpoint.Receive(source, frame.data(), frame.size(), now));
    }
}

} // namespace

int main() {
    Command::CommandRegistry registry;
    int executions = 0;
    registry.Command("ping").OnExecute([&](const Command::CommandContext&) {
        ++executions;
        return Command::CommandResult::Ok("pong");
    });

    ESPNow::ESPNowCommandEndpointConfig config;
    config.MaximumProtocolPayloadBytes = 48;
    config.MaximumMessageBytes = 1024;
    config.MaximumOutstandingRequests = 2;
    config.MaximumReassemblies = 4;
    config.MaximumDuplicateResults = 4;
    config.MaximumInboundRequests = 1;
    config.InboundRequestTimeoutMilliseconds = 50;

    const Peer remote = MakePeer(0x44);
    std::vector<Frame> outbound;

    ESPNow::ESPNowCommandEndpoint endpoint;
    assert(endpoint.Initialize(
        registry,
        config,
        [&](const Peer& destination, const uint8_t* data, std::size_t size) {
            outbound.push_back({destination, std::vector<uint8_t>(data, data + size)});
            return true;
        }
    ));

    int handedOff = 0;
    ESPNow::ESPNowCommandInvocationContext captured;
    endpoint.SetInboundRequestHandler(
        [&](const ESPNow::ESPNowCommandInvocationContext& context) {
            ++handedOff;
            captured = context;
            return true;
        }
    );

    const auto requestFrames = BuildRequestFrames(
        1001,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );

    DeliverRequest(endpoint, remote, requestFrames, 10);

    // The receive/reassembly path must not execute the application Command.
    assert(executions == 0);
    assert(handedOff == 1);
    assert(captured.Metadata.RequestID == 1001);
    assert(captured.Metadata.RemotePeer == remote);
    assert(captured.Invocation.raw == "ping");
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(outbound.empty());

    // Retransmission of an in-flight logical request must preserve identity and
    // must not enqueue or execute it a second time.
    DeliverRequest(endpoint, remote, requestFrames, 11);
    assert(executions == 0);
    assert(handedOff == 1);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(outbound.empty());

    Command::CommandResponseEnvelope completion;
    completion.RequestId = 1001;
    completion.Success = true;
    completion.Code = 0;
    assert(completion.SetMessage("pong"));
    assert(endpoint.CompleteInbound(remote, completion, 12));
    assert(endpoint.GetInboundRequestCount() == 0);
    assert(!outbound.empty());

    auto decoded = DecodeResponse(1001, outbound);
    assert(decoded.success);
    assert(decoded.code == 0);
    assert(decoded.message == "pong");
    outbound.clear();

    // Once complete, the duplicate cache answers retransmission without
    // handing the Command to the application executor again.
    DeliverRequest(endpoint, remote, requestFrames, 13);
    assert(handedOff == 1);
    assert(executions == 0);
    assert(endpoint.GetInboundRequestCount() == 0);
    decoded = DecodeResponse(1001, outbound);
    assert(decoded.success && decoded.message == "pong");
    outbound.clear();

    // Queue saturation is bounded: while one request is accepted, a distinct
    // request receives a deterministic capacity error rather than executing.
    const auto firstBounded = BuildRequestFrames(
        2001,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );
    const auto secondBounded = BuildRequestFrames(
        2002,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );
    DeliverRequest(endpoint, remote, firstBounded, 20);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(handedOff == 2);

    DeliverRequest(endpoint, remote, secondBounded, 21);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(handedOff == 2);
    assert(executions == 0);
    decoded = DecodeResponse(2002, outbound);
    assert(!decoded.success);
    assert(decoded.code == 6);
    outbound.clear();

    completion.RequestId = 2001;
    completion.Success = true;
    completion.Code = 0;
    completion.SetMessage("done");
    assert(endpoint.CompleteInbound(remote, completion, 22));
    outbound.clear();

    // A rejected handoff also remains asynchronous and returns a stable error.
    endpoint.SetInboundRequestHandler(
        [&](const ESPNow::ESPNowCommandInvocationContext&) {
            ++handedOff;
            return false;
        }
    );
    const auto rejected = BuildRequestFrames(
        3001,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );
    DeliverRequest(endpoint, remote, rejected, 30);
    assert(executions == 0);
    assert(endpoint.GetInboundRequestCount() == 0);
    assert(handedOff == 3);
    decoded = DecodeResponse(3001, outbound);
    assert(!decoded.success);
    assert(decoded.code == 7);
    outbound.clear();

    // Accepted asynchronous requests have a bounded lifetime. Before the exact
    // deadline the slot remains occupied and no response is emitted.
    endpoint.SetInboundRequestHandler(
        [&](const ESPNow::ESPNowCommandInvocationContext& context) {
            ++handedOff;
            captured = context;
            return true;
        }
    );
    const auto expiring = BuildRequestFrames(
        4001,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );
    DeliverRequest(endpoint, remote, expiring, 100);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(handedOff == 4);
    endpoint.Update(149);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(outbound.empty());

    // At the deadline the abandoned slot is reclaimed and a deterministic
    // timeout response is produced and cached.
    endpoint.Update(150);
    assert(endpoint.GetInboundRequestCount() == 0);
    decoded = DecodeResponse(4001, outbound);
    assert(!decoded.success);
    assert(decoded.code == 8);
    assert(decoded.message == "Inbound ESP-NOW Command request timed out");
    outbound.clear();

    // Completion arriving after expiry is stale and must not resurrect the
    // request or replace the cached timeout result.
    completion.RequestId = 4001;
    completion.Success = true;
    completion.Code = 0;
    completion.SetMessage("late");
    assert(!endpoint.CompleteInbound(remote, completion, 151));
    assert(endpoint.GetInboundRequestCount() == 0);

    // Retransmission after expiry is answered from the duplicate cache and is
    // never handed to application execution a second time.
    DeliverRequest(endpoint, remote, expiring, 152);
    assert(handedOff == 4);
    assert(endpoint.GetInboundRequestCount() == 0);
    decoded = DecodeResponse(4001, outbound);
    assert(!decoded.success);
    assert(decoded.code == 8);
    outbound.clear();

    // Expiry frees bounded inbound capacity for a distinct request.
    const auto afterExpiry = BuildRequestFrames(
        4002,
        PingInvocation(),
        config.MaximumProtocolPayloadBytes
    );
    DeliverRequest(endpoint, remote, afterExpiry, 153);
    assert(endpoint.GetInboundRequestCount() == 1);
    assert(handedOff == 5);

    // A backwards caller timestamp does not spuriously expire the request
    // because the endpoint elapsed helper clamps backwards time.
    endpoint.Update(10);
    assert(endpoint.GetInboundRequestCount() == 1);
    endpoint.Update(202);
    assert(endpoint.GetInboundRequestCount() == 1);
    endpoint.Update(203);
    assert(endpoint.GetInboundRequestCount() == 0);
    decoded = DecodeResponse(4002, outbound);
    assert(!decoded.success);
    assert(decoded.code == 8);
    outbound.clear();

    endpoint.Shutdown();

    // A zero inbound timeout is invalid: async entries must always have a
    // bounded lifetime when the endpoint is initialized.
    ESPNow::ESPNowCommandEndpoint invalidEndpoint;
    auto invalidConfig = config;
    invalidConfig.InboundRequestTimeoutMilliseconds = 0;
    assert(!invalidEndpoint.Initialize(
        registry,
        invalidConfig,
        [&](const Peer&, const uint8_t*, std::size_t) { return true; }
    ));
    return 0;
}
