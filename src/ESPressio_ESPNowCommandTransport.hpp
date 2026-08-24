#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

#include "ESPressio_ESPNowCommandEndpoint.hpp"
#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio::ESPNow {

struct ESPNowCommandTransportConfig {
    ESPNowCommandEndpointConfig Endpoint;
    uint8_t Protocol = static_cast<uint8_t>(ESPNowProtocol::CommandTransport);
};

class ESPNowCommandTransport final {
public:
    using CompletionHandler = ESPNowCommandEndpoint::CompletionHandler;
    using PolicyHandler = ESPNowCommandEndpoint::PolicyHandler;
    using ResultObserver = ESPNowCommandEndpoint::ResultObserver;

    ~ESPNowCommandTransport() { Shutdown(); }

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

        const bool registered = transport.RegisterProtocolHandler(
            config.Protocol,
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
            }
        );

        if (!registered) {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.Shutdown();
            _transport = nullptr;
            return false;
        }

        const bool maintenanceRegistered = transport.RegisterMaintenanceHandler(
            this,
            [this](uint64_t nowMilliseconds) {
                std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
                _endpoint.Update(nowMilliseconds);
            }
        );

        if (!maintenanceRegistered) {
            transport.UnregisterProtocolHandler(config.Protocol);
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.Shutdown();
            _transport = nullptr;
            return false;
        }

        _initialized = true;
        return true;
    }

    void Shutdown() {
        if (_transport != nullptr && _initialized) {
            _transport->UnregisterMaintenanceHandler(this);
            _transport->UnregisterProtocolHandler(_config.Protocol);
        }
        {
            std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
            _endpoint.Shutdown();
        }
        _transport = nullptr;
        _initialized = false;
    }

    bool GetIsInitialized() const noexcept { return _initialized; }

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

    // Compatibility hook. Periodic maintenance is now automatic on the
    // ESPNowTransport PrecisionThread worker; callers no longer need to invoke
    // Update() from Arduino loop(). Manual calls remain safe and idempotent.
    void Update() {
        if (!_initialized || _transport == nullptr) return;
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.Update(
            _transport->GetMonotonicTimestampNanoseconds() / 1000000ULL
        );
    }

    void SetPolicy(PolicyHandler policy) {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.SetPolicy(std::move(policy));
    }

    void SetResultObserver(ResultObserver observer) {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        _endpoint.SetResultObserver(std::move(observer));
    }

    std::size_t GetOutstandingRequestCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(_endpointMutex);
        return _endpoint.GetOutstandingRequestCount();
    }

    const ESPNowCommandEndpointConfig& GetEndpointConfig() const noexcept {
        return _endpoint.GetConfig();
    }

private:
    ESPNowTransport* _transport = nullptr;
    ESPNowCommandTransportConfig _config;
    ESPNowCommandEndpoint _endpoint;
    mutable std::recursive_mutex _endpointMutex;
    bool _initialized = false;

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
