#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <ESPressio_ESPNowCommandEndpoint.hpp>

using namespace ESPressio;

namespace {

using Peer = ESPNow::ESPNowCommandPeerAddress;

Peer MakePeer(uint8_t value) {
    Peer peer;
    peer.Bytes = {value, value, value, value, value, value};
    return peer;
}

struct Frame {
    Peer Destination;
    std::vector<uint8_t> Data;
};

void Deliver(
    std::vector<Frame>& frames,
    ESPNow::ESPNowCommandEndpoint& target,
    const Peer& source,
    uint64_t now,
    bool reverse = false
) {
    if (reverse) std::reverse(frames.begin(), frames.end());
    for (const auto& frame : frames) {
        assert(target.Receive(source, frame.Data.data(), frame.Data.size(), now));
    }
    frames.clear();
}

Command::CommandInvocation AddInvocation(int a, int b) {
    Command::CommandInvocation invocation;
    invocation.path = {"math", "add"};
    invocation.positional = {std::to_string(a), std::to_string(b)};
    return invocation;
}

} // namespace

int main() {
    // Protocol encode/decode round trip.
    {
        ESPNow::ESPNowCommandProtocol::Request request;
        request.RequestID = 42;
        request.Invocation.path = {"gpio", "write"};
        request.Invocation.positional = {"2", "high"};
        request.Invocation.named["reason"] = "test";
        request.Invocation.raw = "gpio write 2 high --reason test";

        std::vector<uint8_t> encoded;
        assert(ESPNow::ESPNowCommandProtocol::EncodeRequest(request, encoded));

        ESPNow::ESPNowCommandProtocol::Request decoded;
        assert(ESPNow::ESPNowCommandProtocol::DecodeRequest(42, encoded.data(), encoded.size(), decoded));
        assert(decoded.RequestID == 42);
        assert(decoded.Invocation.path == request.Invocation.path);
        assert(decoded.Invocation.positional == request.Invocation.positional);
        assert(decoded.Invocation.named == request.Invocation.named);
        assert(decoded.Invocation.raw == request.Invocation.raw);

        ESPNow::ESPNowCommandProtocol::Response response;
        response.RequestID = 42;
        response.Result = Command::CommandResult::Error("denied", 17);
        assert(ESPNow::ESPNowCommandProtocol::EncodeResponse(response, encoded));

        ESPNow::ESPNowCommandProtocol::Response decodedResponse;
        assert(ESPNow::ESPNowCommandProtocol::DecodeResponse(42, encoded.data(), encoded.size(), decodedResponse));
        assert(decodedResponse.RequestID == 42);
        assert(!decodedResponse.Result.success);
        assert(decodedResponse.Result.code == 17);
        assert(decodedResponse.Result.message == "denied");
    }

    Command::CommandRegistry registryA;
    Command::CommandRegistry registryB;

    int executions = 0;
    auto& add = registryB.Command("math").Command("add");
    add.Parameter<int>("a");
    add.Parameter<int>("b");
    add.OnExecute([&](const Command::CommandContext& context) {
        ++executions;
        return Command::CommandResult::Ok(
            std::to_string(context.Get<int>("a") + context.Get<int>("b"))
        );
    });

    auto& echo = registryB.Command("echo");
    echo.Parameter<std::string>("value");
    echo.OnExecute([](const Command::CommandContext& context) {
        return Command::CommandResult::Ok(context.Get<std::string>("value"));
    });

    const Peer peerA = MakePeer(0xA1);
    const Peer peerB = MakePeer(0xB2);
    const Peer peerC = MakePeer(0xC3);

    std::vector<Frame> aToB;
    std::vector<Frame> bToA;

    ESPNow::ESPNowCommandEndpointConfig config;
    config.MaximumProtocolPayloadBytes = 40; // force fragmentation in tests
    config.MaximumMessageBytes = 2048;
    config.MaximumOutstandingRequests = 2;
    config.MaximumReassemblies = 4;
    config.MaximumDuplicateResults = 4;
    config.RequestTimeoutMilliseconds = 100;
    config.ReassemblyTimeoutMilliseconds = 100;
    config.DuplicateResultLifetimeMilliseconds = 500;

    ESPNow::ESPNowCommandEndpoint endpointA;
    ESPNow::ESPNowCommandEndpoint endpointB;

    assert(endpointA.Initialize(
        registryA,
        config,
        [&](const Peer& destination, const uint8_t* data, std::size_t size) {
            aToB.push_back({destination, std::vector<uint8_t>(data, data + size)});
            return true;
        }
    ));

    assert(endpointB.Initialize(
        registryB,
        config,
        [&](const Peer& destination, const uint8_t* data, std::size_t size) {
            bToA.push_back({destination, std::vector<uint8_t>(data, data + size)});
            return true;
        }
    ));

    // Fragmented request, deliberately delivered out of order.
    bool completed = false;
    Command::CommandResult completionResult;
    uint64_t firstRequestID = 0;

    assert(endpointA.Invoke(
        peerB,
        AddInvocation(7, 9),
        [&](const Command::CommandResult& result) {
            completed = true;
            completionResult = result;
        },
        10,
        &firstRequestID
    ));
    assert(firstRequestID != 0);
    assert(aToB.size() > 1);
    const auto originalRequestFrames = aToB;

    Deliver(aToB, endpointB, peerA, 11, true);
    assert(executions == 1);
    assert(!bToA.empty());
    Deliver(bToA, endpointA, peerB, 12, true);
    assert(completed);
    assert(completionResult.success);
    assert(completionResult.message == "16");
    assert(endpointA.GetOutstandingRequestCount() == 0);

    // Duplicate request replay must not execute a side effect twice.
    aToB = originalRequestFrames;
    Deliver(aToB, endpointB, peerA, 13);
    assert(executions == 1);
    assert(!bToA.empty());
    bToA.clear();

    // Metadata, policy rejection and result observation.
    bool policySeen = false;
    bool observerSeen = false;
    endpointB.SetPolicy([&](const ESPNow::ESPNowCommandInvocationContext& context) {
        policySeen = true;
        assert(context.Metadata.Transport == std::string("esp-now"));
        assert(context.Metadata.RemotePeer == peerA || context.Metadata.RemotePeer == peerC);
        assert(context.Metadata.RequestID != 0);
        if (!context.Invocation.path.empty() && context.Invocation.path.front() == "blocked") {
            return Command::CommandResult::Error("remote policy denied", 21);
        }
        return Command::CommandResult::Ok();
    });
    endpointB.SetResultObserver([&](const ESPNow::ESPNowCommandInvocationContext&, const Command::CommandResult&) {
        observerSeen = true;
    });

    Command::CommandInvocation blocked;
    blocked.path = {"blocked"};
    bool denied = false;
    assert(endpointA.Invoke(peerB, blocked, [&](const Command::CommandResult& result) {
        denied = !result.success && result.code == 21 && result.message == "remote policy denied";
    }, 20));
    Deliver(aToB, endpointB, peerA, 21);
    Deliver(bToA, endpointA, peerB, 22);
    assert(policySeen);
    assert(observerSeen);
    assert(denied);

    // Timeout behavior.
    bool timedOut = false;
    assert(endpointA.Invoke(peerB, AddInvocation(1, 2), [&](const Command::CommandResult& result) {
        timedOut = !result.success && result.code == 4;
    }, 100));
    aToB.clear(); // simulate lost radio packets
    endpointA.Update(201);
    assert(timedOut);
    assert(endpointA.GetOutstandingRequestCount() == 0);

    // Maximum outstanding requests is bounded.
    config.MaximumOutstandingRequests = 1;
    ESPNow::ESPNowCommandEndpoint bounded;
    std::vector<Frame> boundedFrames;
    assert(bounded.Initialize(registryA, config, [&](const Peer& destination, const uint8_t* data, std::size_t size) {
        boundedFrames.push_back({destination, std::vector<uint8_t>(data, data + size)});
        return true;
    }));
    assert(bounded.Invoke(peerB, AddInvocation(1, 1), {}, 0));
    assert(!bounded.Invoke(peerB, AddInvocation(2, 2), {}, 0));
    bounded.Update(101);
    assert(bounded.GetOutstandingRequestCount() == 0);

    // Long structured value spans many fragments and preserves content.
    std::string longValue(600, 'x');
    Command::CommandInvocation longEcho;
    longEcho.path = {"echo"};
    longEcho.positional = {longValue};
    bool echoCompleted = false;
    assert(endpointA.Invoke(peerB, longEcho, [&](const Command::CommandResult& result) {
        echoCompleted = result.success && result.message == longValue;
    }, 300));
    assert(aToB.size() > 2);
    Deliver(aToB, endpointB, peerA, 301, true);
    Deliver(bToA, endpointA, peerB, 302, true);
    assert(echoCompleted);

    // Reassembly is isolated by peer even when request IDs match.
    ESPNow::ESPNowCommandProtocol::Request manual;
    manual.RequestID = 777;
    manual.Invocation = AddInvocation(3, 4);
    std::vector<uint8_t> manualPayload;
    assert(ESPNow::ESPNowCommandProtocol::EncodeRequest(manual, manualPayload));
    auto manualFrames = ESPNow::ESPNowCommandProtocol::BuildFragments(
        ESPNow::ESPNowCommandMessageType::Request,
        777,
        manualPayload,
        config.MaximumProtocolPayloadBytes
    );
    assert(manualFrames.size() > 1);

    const int beforePeerIsolation = executions;
    for (std::size_t i = 0; i < manualFrames.size(); ++i) {
        assert(endpointB.Receive(peerA, manualFrames[i].data(), manualFrames[i].size(), 400 + i));
        assert(endpointB.Receive(peerC, manualFrames[i].data(), manualFrames[i].size(), 400 + i));
    }
    assert(executions == beforePeerIsolation + 2);
    bToA.clear();

    // Missing fragments expire without execution.
    auto incomplete = manualFrames;
    assert(endpointB.Receive(peerA, incomplete.front().data(), incomplete.front().size(), 500));
    assert(endpointB.GetReassemblyCount() == 1);
    endpointB.Update(601);
    assert(endpointB.GetReassemblyCount() == 0);

    // Duplicate fragments with identical payload are accepted.
    assert(endpointB.Receive(peerA, manualFrames.front().data(), manualFrames.front().size(), 700));
    assert(endpointB.Receive(peerA, manualFrames.front().data(), manualFrames.front().size(), 701));
    endpointB.Update(802);

    // Malformed magic/version/header are rejected.
    auto malformed = manualFrames.front();
    malformed[0] ^= 0xFFu;
    assert(!endpointB.Receive(peerA, malformed.data(), malformed.size(), 900));

    malformed = manualFrames.front();
    malformed[4] = 99;
    assert(!endpointB.Receive(peerA, malformed.data(), malformed.size(), 900));

    // Oversized declared total length is rejected.
    malformed = manualFrames.front();
    ESPNow::ESPNowCommandProtocol::FragmentHeader header;
    const uint8_t* ignored = nullptr;
    assert(ESPNow::ESPNowCommandProtocol::ParseFragmentHeader(malformed.data(), malformed.size(), header, ignored));
    header.TotalLength = static_cast<uint32_t>(config.MaximumMessageBytes + 1);
    std::memcpy(malformed.data(), &header, sizeof(header));
    assert(!endpointB.Receive(peerA, malformed.data(), malformed.size(), 900));

    // Shutdown deterministically completes pending work with an error.
    bool shutdownCompletion = false;
    assert(endpointA.Invoke(peerB, AddInvocation(5, 6), [&](const Command::CommandResult& result) {
        shutdownCompletion = !result.success && result.code == 3;
    }, 1000));
    endpointA.Shutdown();
    assert(shutdownCompletion);

    endpointB.Shutdown();
    bounded.Shutdown();
    return 0;
}
