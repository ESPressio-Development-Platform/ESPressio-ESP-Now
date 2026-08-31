from pathlib import Path

endpoint = Path('src/ESPressio_ESPNowCommandEndpoint.hpp')
text = endpoint.read_text()

old = '''    uint64_t RequestTimeoutMilliseconds = 100;\n    uint64_t ReassemblyTimeoutMilliseconds = 500;\n    uint64_t DuplicateResultLifetimeMilliseconds = 1000;\n'''
new = '''    uint64_t RequestTimeoutMilliseconds = 100;\n    uint64_t InboundRequestTimeoutMilliseconds = 5000;\n    uint64_t ReassemblyTimeoutMilliseconds = 500;\n    uint64_t DuplicateResultLifetimeMilliseconds = 1000;\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''            config.MaximumReassemblies == 0 || config.MaximumDuplicateResults == 0 ||\n            config.MaximumInboundRequests == 0) return false;\n'''
new = '''            config.MaximumReassemblies == 0 || config.MaximumDuplicateResults == 0 ||\n            config.MaximumInboundRequests == 0 || config.InboundRequestTimeoutMilliseconds == 0) return false;\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''    /// <summary>Expires timed-out outbound requests, stale fragment reassemblies, and duplicate-response cache entries.</summary>\n    void Update(uint64_t nowMilliseconds) {\n'''
new = '''    /// <summary>Expires timed-out outbound/inbound requests, stale fragment reassemblies, and duplicate-response cache entries.</summary>\n    void Update(uint64_t nowMilliseconds) {\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''        _reassemblies.erase(\n'''
new = '''        for (std::size_t i = 0; i < _inbound.size();) {\n            if (Elapsed(nowMilliseconds, _inbound[i].AcceptedMilliseconds) >=\n                _config.InboundRequestTimeoutMilliseconds) {\n                const ESPNowCommandPeerAddress peer = _inbound[i].Peer;\n                const uint64_t requestID = _inbound[i].RequestID;\n                ESPNowCommandInvocationContext context = std::move(_inbound[i].Context);\n                _inbound.erase(_inbound.begin() + static_cast<std::ptrdiff_t>(i));\n\n                const Command::CommandResult result = Command::CommandResult::Error(\n                    "Inbound ESP-NOW Command request timed out",\n                    8\n                );\n                if (_observer) _observer(context, result);\n\n                // The slot is already released even when response encoding or\n                // physical transmission fails. A successfully encoded response\n                // is cached before send, so retransmission of the same logical\n                // request cannot re-enter application execution after expiry.\n                (void)SendResponseAndCache(peer, requestID, result, nowMilliseconds);\n            } else {\n                ++i;\n            }\n        }\n\n        _reassemblies.erase(\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''    struct InboundRequest {\n        ESPNowCommandPeerAddress Peer;\n        uint64_t RequestID = 0;\n        ESPNowCommandInvocationContext Context;\n    };\n'''
new = '''    struct InboundRequest {\n        ESPNowCommandPeerAddress Peer;\n        uint64_t RequestID = 0;\n        uint64_t AcceptedMilliseconds = 0;\n        ESPNowCommandInvocationContext Context;\n    };\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''            InboundRequest inbound;\n            inbound.Peer = peer;\n            inbound.RequestID = requestID;\n            inbound.Context = context;\n'''
new = '''            InboundRequest inbound;\n            inbound.Peer = peer;\n            inbound.RequestID = requestID;\n            inbound.AcceptedMilliseconds = nowMilliseconds;\n            inbound.Context = context;\n'''
assert old in text
text = text.replace(old, new, 1)
endpoint.write_text(text)

test = Path('tests/test_async_command_handoff.cpp')
t = test.read_text()
old = '''    config.MaximumInboundRequests = 1;\n'''
new = '''    config.MaximumInboundRequests = 1;\n    config.InboundRequestTimeoutMilliseconds = 50;\n'''
assert old in t
t = t.replace(old, new, 1)

needle = '''    decoded = DecodeResponse(3001, outbound);\n    assert(!decoded.success);\n    assert(decoded.code == 7);\n\n    endpoint.Shutdown();\n'''
replacement = '''    decoded = DecodeResponse(3001, outbound);\n    assert(!decoded.success);\n    assert(decoded.code == 7);\n    outbound.clear();\n\n    // Accepted asynchronous requests have a bounded lifetime. Before the exact\n    // deadline the slot remains occupied and no response is emitted.\n    endpoint.SetInboundRequestHandler(\n        [&](const ESPNow::ESPNowCommandInvocationContext& context) {\n            ++handedOff;\n            captured = context;\n            return true;\n        }\n    );\n    const auto expiring = BuildRequestFrames(\n        4001,\n        PingInvocation(),\n        config.MaximumProtocolPayloadBytes\n    );\n    DeliverRequest(endpoint, remote, expiring, 100);\n    assert(endpoint.GetInboundRequestCount() == 1);\n    assert(handedOff == 4);\n    endpoint.Update(149);\n    assert(endpoint.GetInboundRequestCount() == 1);\n    assert(outbound.empty());\n\n    // At the deadline the abandoned slot is reclaimed and a deterministic\n    // timeout response is produced and cached.\n    endpoint.Update(150);\n    assert(endpoint.GetInboundRequestCount() == 0);\n    decoded = DecodeResponse(4001, outbound);\n    assert(!decoded.success);\n    assert(decoded.code == 8);\n    assert(decoded.message == "Inbound ESP-NOW Command request timed out");\n    outbound.clear();\n\n    // Completion arriving after expiry is stale and must not resurrect the\n    // request or replace the cached timeout result.\n    completion.RequestId = 4001;\n    completion.Success = true;\n    completion.Code = 0;\n    completion.SetMessage("late");\n    assert(!endpoint.CompleteInbound(remote, completion, 151));\n    assert(endpoint.GetInboundRequestCount() == 0);\n\n    // Retransmission after expiry is answered from the duplicate cache and is\n    // never handed to application execution a second time.\n    DeliverRequest(endpoint, remote, expiring, 152);\n    assert(handedOff == 4);\n    assert(endpoint.GetInboundRequestCount() == 0);\n    decoded = DecodeResponse(4001, outbound);\n    assert(!decoded.success);\n    assert(decoded.code == 8);\n    outbound.clear();\n\n    // Expiry frees bounded inbound capacity for a distinct request.\n    const auto afterExpiry = BuildRequestFrames(\n        4002,\n        PingInvocation(),\n        config.MaximumProtocolPayloadBytes\n    );\n    DeliverRequest(endpoint, remote, afterExpiry, 153);\n    assert(endpoint.GetInboundRequestCount() == 1);\n    assert(handedOff == 5);\n\n    // A backwards/rolled-over caller timestamp does not spuriously expire the\n    // request because the endpoint's elapsed helper clamps backwards time.\n    endpoint.Update(10);\n    assert(endpoint.GetInboundRequestCount() == 1);\n    endpoint.Update(202);\n    assert(endpoint.GetInboundRequestCount() == 1);\n    endpoint.Update(203);\n    assert(endpoint.GetInboundRequestCount() == 0);\n    decoded = DecodeResponse(4002, outbound);\n    assert(!decoded.success);\n    assert(decoded.code == 8);\n    outbound.clear();\n\n    endpoint.Shutdown();\n\n    // A zero inbound timeout is invalid: async entries must always have a\n    // bounded lifetime when the endpoint is initialized.\n    ESPNow::ESPNowCommandEndpoint invalidEndpoint;\n    auto invalidConfig = config;\n    invalidConfig.InboundRequestTimeoutMilliseconds = 0;\n    assert(!invalidEndpoint.Initialize(\n        registry,\n        invalidConfig,\n        [&](const Peer&, const uint8_t*, std::size_t) { return true; }\n    ));\n'''
assert needle in t
t = t.replace(needle, replacement, 1)
test.write_text(t)

doc = Path('COMMAND_INTEGRATION.md')
d = doc.read_text()
anchor = 'MaximumInboundRequests'
assert anchor in d
# Add one concise note beside the first configuration discussion without rewriting release/version material.
pos = d.index(anchor)
line_end = d.find('\n', pos)
insert = '\n- `InboundRequestTimeoutMilliseconds` bounds accepted asynchronous inbound Command lifetime; expiry frees the bounded slot, returns/caches timeout error code `8`, and causes late `CompleteInbound(...)` calls to fail.\n'
d = d[:line_end+1] + insert + d[line_end+1:]
doc.write_text(d)
