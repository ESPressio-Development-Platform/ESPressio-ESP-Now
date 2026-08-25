#pragma once

/*
 * Optional ESPressio Event integration.
 *
 * This header is intentionally NOT included by ESPressio_ESPNow.hpp so that
 * ordinary ESP-NOW / Timing users do not acquire an ESPressio Event or
 * ESPressio Serializable dependency.
 */

#if !__has_include(<ESPressio_EventTransport.hpp>)
#error "ESPressio ESP-NOW Event Transport requires ESPressio Event in the consuming project."
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <ESPressio_EventTransport.hpp>

#include "ESPressio_ESPNowAsyncProtocolHandler.hpp"
#include "ESPressio_ESPNowTransport.hpp"
#include "ESPressio_ESPNowTypes.hpp"

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS
    #define ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS 16
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES
    #define ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES 8
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE
    #define ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE 16384
#endif

#ifndef ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS
    #define ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS 5000
#endif

namespace ESPressio::ESPNow {

class ESPNowEventTransport final :
    public Event::IEventTransport {

private:
#pragma pack(push, 1)
    struct FragmentHeader {
        static constexpr uint32_t MagicValue = 0x45564E57u; // EVNW
        static constexpr uint8_t VersionValue = 1;

        uint32_t Magic = MagicValue;
        uint8_t Version = VersionValue;
        uint8_t Reserved = 0;
        uint16_t FragmentIndex = 0;
        uint16_t FragmentCount = 0;
        uint16_t FragmentLength = 0;
        uint32_t TotalLength = 0;
        uint64_t MessageID = 0;
    };
#pragma pack(pop)

    static constexpr std::size_t ESPNowWireHeaderSize = 8;
    static constexpr std::size_t MaximumProtocolPayload =
        MaximumFrameSize - ESPNowWireHeaderSize;
    static constexpr std::size_t MaximumFragmentPayload =
        MaximumProtocolPayload - sizeof(FragmentHeader);

    static_assert(
        MaximumFragmentPayload > 0,
        "ESP-NOW Event fragment header exceeds ESP-NOW protocol payload."
    );

    struct Reassembly {
        MacAddress Source;
        uint64_t MessageID = 0;
        uint32_t TotalLength = 0;
        uint16_t FragmentCount = 0;
        uint16_t ReceivedCount = 0;
        uint64_t LastReceiveMonotonicNanoseconds = 0;
        std::vector<uint8_t> Data;
        std::vector<bool> Received;
    };

    ESPNowTransport* _transport = nullptr;
    Event::IEventTransportReceiver* _receiver = nullptr;
    std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS> _destinations{};
    std::size_t _destinationCount = 0;
    std::vector<Reassembly> _reassemblies;
    ESPNowAsyncProtocolHandler _asyncHandler;
    mutable std::mutex _mutex;
    bool _initialized = false;

    void HandleFrame(const ESPNowReceivedFrame& frame) {
        if (frame.PayloadLength < sizeof(FragmentHeader)) {
            return;
        }

        FragmentHeader header;
        std::memcpy(&header, frame.Payload, sizeof(header));

        if (
            header.Magic != FragmentHeader::MagicValue ||
            header.Version != FragmentHeader::VersionValue ||
            header.FragmentCount == 0 ||
            header.FragmentIndex >= header.FragmentCount ||
            header.FragmentLength > MaximumFragmentPayload ||
            sizeof(FragmentHeader) + header.FragmentLength > frame.PayloadLength ||
            header.TotalLength == 0 ||
            header.TotalLength > ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE
        ) {
            return;
        }

        const std::size_t offset =
            static_cast<std::size_t>(header.FragmentIndex) * MaximumFragmentPayload;
        if (offset + header.FragmentLength > header.TotalLength) {
            return;
        }

        std::vector<uint8_t> completed;
        Event::IEventTransportReceiver* receiver = nullptr;

        {
            std::lock_guard<std::mutex> lock(_mutex);

            const uint64_t timeoutNanoseconds =
                static_cast<uint64_t>(ESPRESSIO_ESPNOW_EVENT_REASSEMBLY_TIMEOUT_MS) *
                1000000ULL;

            _reassemblies.erase(
                std::remove_if(
                    _reassemblies.begin(),
                    _reassemblies.end(),
                    [&](const Reassembly& current) {
                        return
                            frame.ReceiveMonotonicNanoseconds >
                                current.LastReceiveMonotonicNanoseconds &&
                            frame.ReceiveMonotonicNanoseconds -
                                current.LastReceiveMonotonicNanoseconds >
                                timeoutNanoseconds;
                    }
                ),
                _reassemblies.end()
            );

            auto found = std::find_if(
                _reassemblies.begin(),
                _reassemblies.end(),
                [&](const Reassembly& current) {
                    return
                        current.MessageID == header.MessageID &&
                        current.Source == frame.Source;
                }
            );

            if (found == _reassemblies.end()) {
                if (_reassemblies.size() >= ESPRESSIO_ESPNOW_EVENT_MAX_REASSEMBLIES) {
                    const auto oldest = std::min_element(
                        _reassemblies.begin(),
                        _reassemblies.end(),
                        [](const Reassembly& left, const Reassembly& right) {
                            return left.LastReceiveMonotonicNanoseconds <
                                right.LastReceiveMonotonicNanoseconds;
                        }
                    );
                    if (oldest != _reassemblies.end()) {
                        _reassemblies.erase(oldest);
                    }
                }

                Reassembly reassembly;
                reassembly.Source = frame.Source;
                reassembly.MessageID = header.MessageID;
                reassembly.TotalLength = header.TotalLength;
                reassembly.FragmentCount = header.FragmentCount;
                reassembly.LastReceiveMonotonicNanoseconds =
                    frame.ReceiveMonotonicNanoseconds;
                reassembly.Data.resize(header.TotalLength);
                reassembly.Received.resize(header.FragmentCount, false);
                _reassemblies.push_back(std::move(reassembly));
                found = std::prev(_reassemblies.end());
            }

            if (
                found->TotalLength != header.TotalLength ||
                found->FragmentCount != header.FragmentCount
            ) {
                _reassemblies.erase(found);
                return;
            }

            found->LastReceiveMonotonicNanoseconds =
                frame.ReceiveMonotonicNanoseconds;

            if (!found->Received[header.FragmentIndex]) {
                std::memcpy(
                    found->Data.data() + offset,
                    frame.Payload + sizeof(FragmentHeader),
                    header.FragmentLength
                );
                found->Received[header.FragmentIndex] = true;
                ++found->ReceivedCount;
            }

            if (found->ReceivedCount == found->FragmentCount) {
                completed = std::move(found->Data);
                _reassemblies.erase(found);
                receiver = _receiver;
            }
        }

        if (receiver != nullptr && !completed.empty()) {
            /*
             * This runs on the Event protocol TaskExecutor, not the ESP-NOW
             * TransportWorker. Deserialization, Event queueing and downstream
             * listener work can therefore never consume the transport stack.
             */
            receiver->ReceiveEventTransportPacket(
                this,
                completed.data(),
                completed.size()
            );
        }
    }

    bool SendToDestination(
        const MacAddress& destination,
        const Event::EventTransportPacket& packet
    ) {
        if (
            _transport == nullptr ||
            packet.Data == nullptr ||
            packet.Size == 0 ||
            packet.Size > ESPRESSIO_ESPNOW_EVENT_MAX_PACKET_SIZE
        ) {
            return false;
        }

        const std::size_t fragmentCountSize =
            (packet.Size + MaximumFragmentPayload - 1) / MaximumFragmentPayload;

        if (
            fragmentCountSize == 0 ||
            fragmentCountSize > UINT16_MAX ||
            packet.Size > UINT32_MAX
        ) {
            return false;
        }

        const uint16_t fragmentCount =
            static_cast<uint16_t>(fragmentCountSize);

        std::array<uint8_t, MaximumProtocolPayload> payload{};

        for (uint16_t index = 0; index < fragmentCount; ++index) {
            const std::size_t offset =
                static_cast<std::size_t>(index) * MaximumFragmentPayload;
            const std::size_t remaining = packet.Size - offset;
            const std::size_t fragmentLength =
                std::min(remaining, MaximumFragmentPayload);

            FragmentHeader header;
            header.FragmentIndex = index;
            header.FragmentCount = fragmentCount;
            header.FragmentLength = static_cast<uint16_t>(fragmentLength);
            header.TotalLength = static_cast<uint32_t>(packet.Size);
            header.MessageID = packet.MessageID;

            std::memcpy(payload.data(), &header, sizeof(header));
            std::memcpy(
                payload.data() + sizeof(header),
                packet.Data + offset,
                fragmentLength
            );

            if (!_transport->Send(
                    destination,
                    static_cast<uint8_t>(ESPNowProtocol::EventTransport),
                    payload.data(),
                    sizeof(header) + fragmentLength)) {
                return false;
            }
        }

        return true;
    }

public:
    ESPNowEventTransport() = default;

    explicit ESPNowEventTransport(ESPNowTransport& transport)
        : _transport(&transport) {
    }

    ESPNowEventTransport(const ESPNowEventTransport&) = delete;
    ESPNowEventTransport& operator=(const ESPNowEventTransport&) = delete;

    ~ESPNowEventTransport() override {
        Shutdown();
    }

    bool Initialize(
        ESPNowTransport& transport = ESPNowTransport::GetInstance(),
        ESPNowAsyncProtocolHandler::Configuration asyncConfiguration = {}
    ) {
        if (_initialized) {
            return true;
        }
        if (!transport.GetIsInitialized()) {
            return false;
        }

        _transport = &transport;
        asyncConfiguration.Name = "espnowEvent";

        if (!_asyncHandler.Initialize(
                [this](const ESPNowReceivedFrame& frame) {
                    HandleFrame(frame);
                },
                asyncConfiguration)) {
            _transport = nullptr;
            return false;
        }

        if (!_transport->RegisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::EventTransport),
                [this](const ESPNowReceivedFrame& frame) {
                    // The ESP-NOW TransportWorker only hands ownership to the
                    // bounded Event protocol executor, then returns.
                    (void)_asyncHandler.Submit(frame);
                })) {
            _asyncHandler.Shutdown();
            _transport = nullptr;
            return false;
        }

        _initialized = true;
        return true;
    }

    void Shutdown() {
        if (!_initialized && _transport == nullptr) {
            _asyncHandler.Shutdown();
            return;
        }

        if (_transport != nullptr) {
            _transport->UnregisterProtocolHandler(
                static_cast<uint8_t>(ESPNowProtocol::EventTransport)
            );
        }

        _asyncHandler.Shutdown();

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _receiver = nullptr;
            _destinationCount = 0;
            _reassemblies.clear();
        }

        _transport = nullptr;
        _initialized = false;
    }

    bool GetIsInitialized() const {
        return _initialized;
    }

    Task::TaskExecutionStatistics GetAsyncHandlerStatistics() const {
        return _asyncHandler.GetStatistics();
    }

    uint64_t GetRejectedAsyncHandoffCount() const noexcept {
        return _asyncHandler.GetRejectedHandoffCount();
    }

    bool AddDestination(const MacAddress& destination) {
        if (destination.IsZero()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for (std::size_t index = 0; index < _destinationCount; ++index) {
            if (_destinations[index] == destination) {
                return true;
            }
        }
        if (_destinationCount >= _destinations.size()) {
            return false;
        }
        _destinations[_destinationCount++] = destination;
        return true;
    }

    bool RemoveDestination(const MacAddress& destination) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (std::size_t index = 0; index < _destinationCount; ++index) {
            if (_destinations[index] != destination) {
                continue;
            }
            for (std::size_t move = index + 1; move < _destinationCount; ++move) {
                _destinations[move - 1] = _destinations[move];
            }
            --_destinationCount;
            _destinations[_destinationCount] = MacAddress{};
            return true;
        }
        return false;
    }

    void ClearDestinations() {
        std::lock_guard<std::mutex> lock(_mutex);
        _destinationCount = 0;
        for (auto& destination : _destinations) {
            destination = MacAddress{};
        }
    }

    std::size_t GetDestinationCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _destinationCount;
    }

    bool Send(const Event::EventTransportPacket& packet) override {
        if (
            !_initialized ||
            _transport == nullptr ||
            packet.Data == nullptr ||
            packet.Size == 0
        ) {
            return false;
        }

        std::array<MacAddress, ESPRESSIO_ESPNOW_EVENT_MAX_DESTINATIONS> destinations{};
        std::size_t destinationCount = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            destinationCount = _destinationCount;
            for (std::size_t index = 0; index < destinationCount; ++index) {
                destinations[index] = _destinations[index];
            }
        }

        if (destinationCount == 0) {
            return false;
        }

        bool allAccepted = true;
        for (std::size_t index = 0; index < destinationCount; ++index) {
            if (!SendToDestination(destinations[index], packet)) {
                allAccepted = false;
            }
        }
        return allAccepted;
    }

    void SetReceiver(Event::IEventTransportReceiver* receiver) override {
        std::lock_guard<std::mutex> lock(_mutex);
        _receiver = receiver;
    }
};

}
