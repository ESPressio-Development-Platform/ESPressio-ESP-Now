#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <ESPressio_CommandEvents.hpp>
#include <ESPressio_CommandResponseRoute.hpp>

#include "ESPressio_ESPNowAsyncProtocolHandler.hpp"
#include "ESPressio_ESPNowCommandEndpoint.hpp"
#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio::ESPNow {

/// <summary>Configures the Command endpoint, asynchronous receive worker, and ESP-NOW protocol number used by ESPNowCommandTransport.</summary>
struct ESPNowCommandTransportConfig {
    ESPNowCommandEndpointConfig Endpoint;
    ESPNowAsyncProtocolHandler::Configuration AsyncHandler;
    uint8_t Protocol = static_cast<uint8_t>(ESPNowProtocol::CommandTransport);

    ESPNowCommandTransportConfig() {
        AsyncHandler.Name = "espnowCommand";
    }
};

/// <summary>Bridges ESPressio Command request/response routing onto an ESPNowTransport.</summary>
/// <remarks>Inbound radio callbacks are handed to a bounded asynchronous worker before Command parsing/execution, while response routing is registered with the Command response-route registry.</remarks>
class ESPNowCommandTransport final {
public:
    using CompletionHandler = ESPNowCommandEndpoint::CompletionHandler;
    using PolicyHandler = ESPNowCommandEndpoint::PolicyHandler;
    using ResultObserver = ESPNowCommandEndpoint::ResultObserver;

private:
    class ResponseRoute final : public Command::ICommandResponseRoute {
        ESPNowCommandTransport* _owner = nullptr;

    public:
        explicit ResponseRoute(ESPNowCommandTransport& owner)
            : _owner(&owner) {
        }

        void Detach() {
            _owner = nullptr;
        }

        bool SendCommandResponse(
            const Command::CommandOriginAddress& destination,
            const Command::CommandResponseEnvelope& response
        ) override {
            if (_owner == nullptr || destination.Length != 6) {
                return false;
            }

            ESPNowCommandPeerAddress peer;
            for (size_t index = 0; index < peer.Bytes.size(); ++index) {
                peer.Bytes[index] = destination.Bytes[index];
            }

            return _owner->CompleteInbound(peer, response);
        }
    };

public:
    ~ESPNowCommandTransport() { Shutdown(); }

    /// <summary>Initializes the endpoint, Command response route, asynchronous receive handler, protocol handler, and maintenance callback.</summary>
    /// <param name="transport">Initialized ESP-NOW transport used for Command frames.</param>
    /// <param name="registry">Command registry used to resolve inbound invocations.</param>
    /// <param name="config">Command transport configuration.</param>
    /// <returns>True when every required integration point is registered successfully.</returns>
    bool Initialize(
        ESPNowTransport& transport = ESPNowTransport::GetInstance(),
        Command::CommandRegistry& registry = Command::CommandRegistry::GetInstance(),
        ESPNowCommandTransportConfig config = {}
    ) {
        Shutdown();
        if (!transport.GetIsInitialized()) return false;

        constexpr std::size_t maximumOuterPayload = 242;
        if (config.Endpoint.MaximumProtocolPayloadBytes > maximumOuterPayload) {
            config.Endpoint.MaximumProtocolPayloadBytes = maximumOuterPayload;
        }

        _transport = &transport;
        _config = config;

        {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            const bool endpointReady = _endpoint.Initialize(
                registry,
                config.Endpoint,
                [this](const ESPNowCommandPeerAddress& peer, const uint8_t* data, std::size_t size) {
                    if (_transport == nullptr) return false;
                    return _transport->Send(ToMacAddress(peer), _config.Protocol, data, size);
                }
            );
            if (!endpointReady) {
                _transport = nullptr;
                return false;
            }
        }

        _responseRoute = std::make_shared<ResponseRoute>(*this);
        _responseRouteId =
            Command::CommandResponseRouteRegistry::GetInstance().Register(
                _responseRoute
            );
        if (_responseRouteId == 0) {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.Shutdown();
            _responseRoute.reset();
            _transport = nullptr;
            return false;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.SetInboundRequestHandler(
                [this](const ESPNowCommandInvocationContext& context) {
                    Command::CommandRequestEnvelope envelope;
                    envelope.RequestId = context.Metadata.RequestID;
                    envelope.Origin.TransportRoute = _responseRouteId;
                    if (!envelope.Origin.Address.Assign(
                            context.Metadata.RemotePeer.Bytes.data(),
                            context.Metadata.RemotePeer.Bytes.size())) {
                        return false;
                    }
                    envelope.ResponseExpectation =
                        Command::CommandResponseExpectation::Completion;
                    envelope.ResponseMode = Command::CommandResponseMode::Single;
                    envelope.ResponseTimeoutMilliseconds =
                        static_cast<uint32_t>(_config.Endpoint.RequestTimeoutMilliseconds);
                    if (!envelope.SetRaw(context.Invocation.raw)) {
                        return false;
                    }

                    (new Event::InboundCommandEvent(envelope))->Queue();
                    return true;
                }
            );
        }

        if (!_asyncHandler.Initialize(
                [this](const ESPNowReceivedFrame& frame) {
                    if (_transport == nullptr) return;
                    const uint64_t nowMilliseconds =
                        _transport->GetMonotonicTimestampNanoseconds() / 1000000ULL;
                    std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
                    _endpoint.Receive(
                        ToPeerAddress(frame.Source),
                        frame.Payload,
                        frame.PayloadLength,
                        nowMilliseconds
                    );
                },
                config.AsyncHandler)) {
            Shutdown();
            return false;
        }

        const bool registered = transport.RegisterProtocolHandler(
            config.Protocol,
            [this](const ESPNowReceivedFrame& frame) {
                // The ESP-NOW TransportWorker performs no Command parsing,
                // policy execution, handler invocation, or response work.
                // Ownership is handed to the bounded application executor and
                // the transport stack can unwind immediately.
                (void)_asyncHandler.Submit(frame);
            }
        );

        if (!registered) {
            Shutdown();
            return false;
        }

        const bool maintenanceRegistered = transport.RegisterMaintenanceHandler(
            this,
            [this](uint64_t nowMilliseconds) {
                // Maintenance remains lightweight and bounded. Application
                // Command execution itself never occurs here.
                std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
                _endpoint.Update(nowMilliseconds);
            }
        );

        if (!maintenanceRegistered) {
            transport.UnregisterProtocolHandler(config.Protocol);
            Shutdown();
            return false;
        }

        _initialized = true;
        return true;
    }

    /// <summary>Unregisters protocol/maintenance hooks, stops the async worker, removes the Command response route, and shuts down the endpoint.</summary>
    void Shutdown() {
        if (_transport != nullptr && _initialized) {
            _transport->UnregisterMaintenanceHandler(this);
            _transport->UnregisterProtocolHandler(_config.Protocol);
        }

        _asyncHandler.Shutdown();

        if (_responseRouteId != 0) {
            Command::CommandResponseRouteRegistry::GetInstance().Unregister(
                _responseRouteId
            );
            _responseRouteId = 0;
        }
        if (_responseRoute) {
            _responseRoute->Detach();
            _responseRoute.reset();
        }

        {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.Shutdown();
        }
        _transport = nullptr;
        _initialized = false;
    }

    /// <summary>Reports whether all Command transport integrations are currently initialized.</summary>
    bool GetIsInitialized() const noexcept { return _initialized; }

    /// <summary>Sends a Command invocation to a remote ESP-NOW peer.</summary>
    /// <param name="peer">Destination peer MAC address.</param>
    /// <param name="invocation">Command invocation to encode and transmit.</param>
    /// <param name="completion">Optional completion callback for the response.</param>
    /// <param name="requestID">Optional destination for the allocated request identifier.</param>
    /// <returns>True when the endpoint accepts the outbound request.</returns>
    bool Invoke(
        const MacAddress& peer,
        const Command::CommandInvocation& invocation,
        CompletionHandler completion = {},
        uint64_t* requestID = nullptr
    ) {
        if (!_initialized || _transport == nullptr) return false;
        const uint64_t nowMilliseconds =
            _transport->GetMonotonicTimestampNanoseconds() / 1000000ULL;
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        return _endpoint.Invoke(
            ToPeerAddress(peer),
            invocation,
            std::move(completion),
            nowMilliseconds,
            requestID
        );
    }

    /// <summary>Runs endpoint timeout/reassembly maintenance using the transport monotonic clock.</summary>
    void Update() {
        if (!_initialized || _transport == nullptr) return;
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.Update(
            _transport->GetMonotonicTimestampNanoseconds() / 1000000ULL
        );
    }

    /// <summary>Sets the policy callback applied to inbound Command invocations.</summary>
    void SetPolicy(PolicyHandler policy) {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.SetPolicy(std::move(policy));
    }

    /// <summary>Sets an observer for Command results produced or received by the endpoint.</summary>
    void SetResultObserver(ResultObserver observer) {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.SetResultObserver(std::move(observer));
    }

    /// <summary>Returns the number of outbound requests awaiting completion.</summary>
    std::size_t GetOutstandingRequestCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        return _endpoint.GetOutstandingRequestCount();
    }

    /// <summary>Returns the number of inbound requests currently awaiting completion/response.</summary>
    std::size_t GetInboundRequestCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        return _endpoint.GetInboundRequestCount();
    }

    /// <summary>Returns the effective endpoint configuration.</summary>
    const ESPNowCommandEndpointConfig& GetEndpointConfig() const noexcept {
        return _endpoint.GetConfig();
    }

    /// <summary>Returns queue/execution statistics for the asynchronous receive handler.</summary>
    Task::TaskExecutionStatistics GetAsyncHandlerStatistics() const {
        return _asyncHandler.GetStatistics();
    }

    /// <summary>Returns the number of inbound frames rejected while handing them to the asynchronous receive worker.</summary>
    uint64_t GetRejectedAsyncHandoffCount() const noexcept {
        return _asyncHandler.GetRejectedHandoffCount();
    }

private:
    ESPNowTransport* _transport = nullptr;
    ESPNowCommandTransportConfig _config;
    ESPNowCommandEndpoint _endpoint;
    ESPNowAsyncProtocolHandler _asyncHandler;
    std::shared_ptr<ResponseRoute> _responseRoute;
    Command::CommandTransportRouteId _responseRouteId = 0;
    mutable std::recursive_mutex _endpointMutex;
    bool _initialized = false;

    bool CompleteInbound(
        const ESPNowCommandPeerAddress& peer,
        const Command::CommandResponseEnvelope& response
    ) {
        if (!_initialized || _transport == nullptr) return false;
        const uint64_t nowMilliseconds =
            _transport->GetMonotonicTimestampNanoseconds() / 1000000ULL;
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        return _endpoint.CompleteInbound(peer, response, nowMilliseconds);
    }

    static ESPNowCommandPeerAddress ToPeerAddress(const MacAddress& address) {
        ESPNowCommandPeerAddress result;
        for (std::size_t i = 0; i < result.Bytes.size(); ++i) result.Bytes[i] = address.Bytes[i];
        return result;
    }

    static MacAddress ToMacAddress(const ESPNowCommandPeerAddress& address) {
        return MacAddress(address.Bytes.data());
    }
};

}
