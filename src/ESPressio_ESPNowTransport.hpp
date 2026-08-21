#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <WiFi.h>

#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_ESPNowTypes.hpp"
#include "ESPressio_IESPNowTransportObserver.hpp"

#ifndef ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS
    #define ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS 8
#endif

namespace ESPressio {
namespace ESPNow {

class ESPNowTransport {
public:
    using ProtocolHandler = std::function<void(const ESPNowReceivedFrame&)>;

private:
    #pragma pack(push, 1)
    struct WireHeader {
        uint32_t Magic = FrameMagic;
        uint8_t Version = FrameVersion;
        uint8_t Protocol = 0;
        uint16_t PayloadLength = 0;
    };
    #pragma pack(pop)

    struct HandlerRecord {
        uint8_t Protocol = 0;
        ProtocolHandler Handler = nullptr;
    };

    struct CallbackFrame {
        MacAddress Source;
        uint64_t ReceiveMonotonicNanoseconds = 0;
        uint16_t Length = 0;
        uint8_t Data[MaximumFrameSize] = {0};
    };

    class TransportObservable final : public Observable::ThreadSafeObservable {
    private:
        template <typename Callback>
        void Notify(Callback&& callback) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IESPNowTransportObserver>([&](IESPNowTransportObserver* observer) {
                    try { callback(observer); } catch (...) {}
                });
            });
        }
    public:
        void Initialized() { Notify([](IESPNowTransportObserver* observer){ observer->OnESPNowTransportInitialized(); }); }
        void InitializationFailed() { Notify([](IESPNowTransportObserver* observer){ observer->OnESPNowTransportInitializationFailed(); }); }
        void Shutdown() { Notify([](IESPNowTransportObserver* observer){ observer->OnESPNowTransportShutdown(); }); }
        void PeerAdded(const MacAddress& address) { Notify([&](IESPNowTransportObserver* observer){ observer->OnESPNowPeerAdded(address); }); }
        void PeerRemoved(const MacAddress& address) { Notify([&](IESPNowTransportObserver* observer){ observer->OnESPNowPeerRemoved(address); }); }
        void FrameReceived(const MacAddress& address, uint8_t protocol, std::size_t size, uint64_t timestamp) { Notify([&](IESPNowTransportObserver* observer){ observer->OnESPNowFrameReceived(address, protocol, size, timestamp); }); }
        void SendAccepted(const MacAddress& address, uint8_t protocol, std::size_t size) { Notify([&](IESPNowTransportObserver* observer){ observer->OnESPNowSendAccepted(address, protocol, size); }); }
        void SendFailed(const MacAddress& address, uint8_t protocol, std::size_t size) { Notify([&](IESPNowTransportObserver* observer){ observer->OnESPNowSendFailed(address, protocol, size); }); }
    };

    ESPNowTransportConfig _config;
    QueueHandle_t _receiveQueue = nullptr;
    TaskHandle_t _receiveTask = nullptr;
    std::array<HandlerRecord, ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS> _handlers;
    mutable std::mutex _handlerMutex;
    bool _initialized = false;
    std::shared_ptr<TransportObservable> _observable = std::make_shared<TransportObservable>();

    static ESPNowTransport*& CallbackInstance() {
        static ESPNowTransport* instance = nullptr;
        return instance;
    }

    ESPNowTransport() = default;

    static uint64_t GetRawMonotonicNanoseconds() {
        return static_cast<uint64_t>(esp_timer_get_time()) * 1000ULL;
    }

    static void ReceiveTaskEntry(void* parameter) {
        auto* self = static_cast<ESPNowTransport*>(parameter);
        if (self != nullptr) self->ReceiveTaskLoop();
        vTaskDelete(nullptr);
    }

    void ReceiveTaskLoop() {
        CallbackFrame frame;
        while (true) {
            if (xQueueReceive(_receiveQueue, &frame, portMAX_DELAY) != pdTRUE) continue;
            ProcessCallbackFrame(frame);
        }
    }

    void ProcessCallbackFrame(const CallbackFrame& frame) {
        if (frame.Length < sizeof(WireHeader)) return;

        WireHeader header;
        std::memcpy(&header, frame.Data, sizeof(header));
        if (header.Magic != FrameMagic || header.Version != FrameVersion) return;
        if (header.PayloadLength > MaximumFrameSize - sizeof(WireHeader) ||
            sizeof(WireHeader) + header.PayloadLength > frame.Length) return;

        ESPNowReceivedFrame received;
        received.Source = frame.Source;
        received.ReceiveMonotonicNanoseconds = frame.ReceiveMonotonicNanoseconds;
        received.Protocol = header.Protocol;
        received.PayloadLength = header.PayloadLength;
        if (header.PayloadLength > 0) {
            std::memcpy(received.Payload, frame.Data + sizeof(WireHeader), header.PayloadLength);
        }

        // A frame that reaches this point has a valid ESPressio wire header and
        // bounded payload. Count it as peer-liveness evidence regardless of
        // protocol before handing it to the protocol-specific consumer.
        _observable->FrameReceived(
            received.Source,
            received.Protocol,
            received.PayloadLength,
            received.ReceiveMonotonicNanoseconds
        );

        ProtocolHandler handler;
        {
            std::lock_guard<std::mutex> lock(_handlerMutex);
            for (const auto& record : _handlers) {
                if (record.Handler && record.Protocol == header.Protocol) {
                    handler = record.Handler;
                    break;
                }
            }
        }
        if (handler) handler(received);
    }

    static void QueueReceivedData(const uint8_t* source, const uint8_t* data, int length) {
        ESPNowTransport* self = CallbackInstance();
        if (self == nullptr || !self->_initialized || self->_receiveQueue == nullptr ||
            source == nullptr || data == nullptr || length <= 0) return;

        CallbackFrame frame;
        frame.Source = MacAddress(source);
        frame.ReceiveMonotonicNanoseconds = GetRawMonotonicNanoseconds();
        frame.Length = static_cast<uint16_t>(length > static_cast<int>(MaximumFrameSize) ? MaximumFrameSize : length);
        std::memcpy(frame.Data, data, frame.Length);
        xQueueSend(self->_receiveQueue, &frame, 0);
    }

    #if ESP_IDF_VERSION_MAJOR >= 5
    static void ReceiveCallback(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
        QueueReceivedData(info == nullptr ? nullptr : info->src_addr, data, length);
    }
    #else
    static void ReceiveCallback(const uint8_t* source, const uint8_t* data, int length) {
        QueueReceivedData(source, data, length);
    }
    #endif

public:
    ESPNowTransport(const ESPNowTransport&) = delete;
    ESPNowTransport& operator=(const ESPNowTransport&) = delete;

    static ESPNowTransport& GetInstance() {
        static ESPNowTransport instance;
        return instance;
    }

    Observable::ObserverHandlePtr RegisterObserver(IESPNowTransportObserver* observer) {
        return _observable->RegisterObserver(observer);
    }

    void UnregisterObserver(IESPNowTransportObserver* observer) {
        _observable->UnregisterObserver(observer);
    }

    bool Initialize(const ESPNowTransportConfig& config = ESPNowTransportConfig()) {
        if (_initialized) return true;
        _config = config;

        if (_config.InitializeWiFi) WiFi.mode(WIFI_STA);

        if (_config.Channel != 0 && esp_wifi_set_channel(_config.Channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            _observable->InitializationFailed();
            return false;
        }

        _receiveQueue = xQueueCreate(_config.ReceiveQueueLength, sizeof(CallbackFrame));
        if (_receiveQueue == nullptr) {
            _observable->InitializationFailed();
            return false;
        }

        if (xTaskCreatePinnedToCore(
                ReceiveTaskEntry,
                "espressio_espnow_rx",
                _config.ReceiveTaskStackSize,
                this,
                _config.ReceiveTaskPriority,
                &_receiveTask,
                _config.ReceiveTaskCore
            ) != pdPASS) {
            vQueueDelete(_receiveQueue);
            _receiveQueue = nullptr;
            _observable->InitializationFailed();
            return false;
        }

        if (esp_now_init() != ESP_OK) {
            vTaskDelete(_receiveTask);
            _receiveTask = nullptr;
            vQueueDelete(_receiveQueue);
            _receiveQueue = nullptr;
            _observable->InitializationFailed();
            return false;
        }

        CallbackInstance() = this;
        if (esp_now_register_recv_cb(ReceiveCallback) != ESP_OK) {
            CallbackInstance() = nullptr;
            esp_now_deinit();
            vTaskDelete(_receiveTask);
            _receiveTask = nullptr;
            vQueueDelete(_receiveQueue);
            _receiveQueue = nullptr;
            _observable->InitializationFailed();
            return false;
        }

        _initialized = true;
        _observable->Initialized();
        return true;
    }

    void Shutdown() {
        if (!_initialized) return;
        _initialized = false;

        esp_now_unregister_recv_cb();
        esp_now_deinit();
        CallbackInstance() = nullptr;

        if (_receiveTask != nullptr) {
            vTaskDelete(_receiveTask);
            _receiveTask = nullptr;
        }
        if (_receiveQueue != nullptr) {
            vQueueDelete(_receiveQueue);
            _receiveQueue = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(_handlerMutex);
            for (auto& record : _handlers) record = HandlerRecord();
        }

        _observable->Shutdown();
    }

    bool GetIsInitialized() const { return _initialized; }

    bool RegisterProtocolHandler(uint8_t protocol, ProtocolHandler handler) {
        if (!handler) return false;
        std::lock_guard<std::mutex> lock(_handlerMutex);

        for (auto& record : _handlers) {
            if (record.Handler && record.Protocol == protocol) {
                record.Handler = std::move(handler);
                return true;
            }
        }
        for (auto& record : _handlers) {
            if (!record.Handler) {
                record.Protocol = protocol;
                record.Handler = std::move(handler);
                return true;
            }
        }
        return false;
    }

    void UnregisterProtocolHandler(uint8_t protocol) {
        std::lock_guard<std::mutex> lock(_handlerMutex);
        for (auto& record : _handlers) {
            if (record.Handler && record.Protocol == protocol) {
                record = HandlerRecord();
                return;
            }
        }
    }

    bool AddPeer(const ESPNowPeerConfig& config) {
        if (!_initialized || config.Address.IsZero()) return false;
        if (esp_now_is_peer_exist(config.Address.Bytes)) return true;

        esp_now_peer_info_t peer = {};
        std::memcpy(peer.peer_addr, config.Address.Bytes, MacAddressLength);
        peer.channel = config.Channel;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = config.Encrypt;
        if (config.Encrypt) std::memcpy(peer.lmk, config.LocalMasterKey, sizeof(peer.lmk));

        const bool added = esp_now_add_peer(&peer) == ESP_OK;
        if (added) _observable->PeerAdded(config.Address);
        return added;
    }

    bool RemovePeer(const MacAddress& address) {
        if (!_initialized || address.IsZero()) return false;
        const bool existed = esp_now_is_peer_exist(address.Bytes);
        const esp_err_t result = esp_now_del_peer(address.Bytes);
        const bool success = result == ESP_OK || result == ESP_ERR_ESPNOW_NOT_FOUND;
        if (success && existed) _observable->PeerRemoved(address);
        return success;
    }

    bool Send(const MacAddress& destination, uint8_t protocol, const void* payload, std::size_t payloadLength) {
        if (!_initialized || destination.IsZero() || payloadLength > MaximumFrameSize - sizeof(WireHeader)) {
            _observable->SendFailed(destination, protocol, payloadLength);
            return false;
        }

        uint8_t frame[MaximumFrameSize] = {0};
        WireHeader header;
        header.Protocol = protocol;
        header.PayloadLength = static_cast<uint16_t>(payloadLength);
        std::memcpy(frame, &header, sizeof(header));
        if (payload != nullptr && payloadLength > 0) {
            std::memcpy(frame + sizeof(header), payload, payloadLength);
        }

        const bool accepted = esp_now_send(destination.Bytes, frame, sizeof(header) + payloadLength) == ESP_OK;
        if (accepted) _observable->SendAccepted(destination, protocol, payloadLength);
        else _observable->SendFailed(destination, protocol, payloadLength);
        return accepted;
    }

    uint64_t GetMonotonicTimestampNanoseconds() const {
        return GetRawMonotonicNanoseconds();
    }
};

} // namespace ESPNow
} // namespace ESPressio
