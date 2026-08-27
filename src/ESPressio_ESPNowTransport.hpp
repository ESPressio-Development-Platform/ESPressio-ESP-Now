#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <WiFi.h>

#include <esp_err.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <ESPressio_Memory.hpp>
#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>
#include <ESPressio_Time.hpp>

#include "ESPressio_ESPNowTypes.hpp"
#include "ESPressio_IESPNowTransportObserver.hpp"

#ifndef ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS
#define ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS 8
#endif
#ifndef ESPRESSIO_ESPNOW_MAX_MAINTENANCE_HANDLERS
#define ESPRESSIO_ESPNOW_MAX_MAINTENANCE_HANDLERS 8
#endif
#ifndef ESPRESSIO_ESPNOW_MAX_INTERFACE_HINTS
#define ESPRESSIO_ESPNOW_MAX_INTERFACE_HINTS 20
#endif
#ifndef ESPRESSIO_ESPNOW_MAX_MANAGED_PEERS
#define ESPRESSIO_ESPNOW_MAX_MANAGED_PEERS 20
#endif

namespace ESPressio {
namespace ESPNow {

class ESPNowTransport {
public:
    using ProtocolHandler = std::function<void(const ESPNowReceivedFrame&)>;
    using MaintenanceHandler = std::function<void(uint64_t)>;

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
        std::shared_ptr<ProtocolHandler> Handler;
    };

    struct MaintenanceRecord {
        const void* Owner = nullptr;
        std::shared_ptr<MaintenanceHandler> Handler;
    };

    struct CallbackFrame {
        MacAddress Source;
        uint64_t ReceiveMonotonicNanoseconds = 0;
        uint16_t Length = 0;
        wifi_interface_t LocalInterface = WIFI_IF_STA;
        bool HasLocalInterface = false;
        uint8_t Data[MaximumFrameSize] = {0};
    };

    struct PeerInterfaceHint {
        bool Used = false;
        MacAddress Address;
        wifi_interface_t Interface = WIFI_IF_STA;
    };

    struct ManagedPeerRecord {
        bool Used = false;
        ESPNowPeerConfig Config;
    };

    using HandlerStorage = System::Memory::Vector<HandlerRecord, System::Memory::MemoryPolicy::ExternalPreferred>;
    using MaintenanceStorage = System::Memory::Vector<MaintenanceRecord, System::Memory::MemoryPolicy::ExternalPreferred>;
    using PeerHintStorage = System::Memory::Vector<PeerInterfaceHint, System::Memory::MemoryPolicy::ExternalPreferred>;
    using ManagedPeerStorage = System::Memory::Vector<ManagedPeerRecord, System::Memory::MemoryPolicy::ExternalPreferred>;
    using PeerConfigSnapshot = System::Memory::Vector<ESPNowPeerConfig, System::Memory::MemoryPolicy::ExternalPreferred>;

    class TransportObservable final : public Observable::ThreadSafeObservable {
        template<typename Callback>
        void Notify(Callback&& callback) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IESPNowTransportObserver>([&](IESPNowTransportObserver* observer) {
                    try { callback(observer); } catch (...) {}
                });
            });
        }
    public:
        void Initialized() { Notify([](IESPNowTransportObserver* o){ o->OnESPNowTransportInitialized(); }); }
        void InitializationFailed() { Notify([](IESPNowTransportObserver* o){ o->OnESPNowTransportInitializationFailed(); }); }
        void Shutdown() { Notify([](IESPNowTransportObserver* o){ o->OnESPNowTransportShutdown(); }); }
        void PeerAdded(const MacAddress& a) { Notify([&](IESPNowTransportObserver* o){ o->OnESPNowPeerAdded(a); }); }
        void PeerRemoved(const MacAddress& a) { Notify([&](IESPNowTransportObserver* o){ o->OnESPNowPeerRemoved(a); }); }
        void FrameReceived(const MacAddress& a, uint8_t p, std::size_t s, uint64_t t) { Notify([&](IESPNowTransportObserver* o){ o->OnESPNowFrameReceived(a,p,s,t); }); }
        void SendAccepted(const MacAddress& a, uint8_t p, std::size_t s) { Notify([&](IESPNowTransportObserver* o){ o->OnESPNowSendAccepted(a,p,s); }); }
        void SendFailed(const MacAddress& a, uint8_t p, std::size_t s, ESPNowSendFailure f, int32_t e) {
            Notify([&](IESPNowTransportObserver* o){ o->OnESPNowSendFailedDetailed(a,p,s,f,e); });
        }
    };

    class TransportWorker final : public Threads::PrecisionThread<
        Units::NanoSeconds<uint64_t>, Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>
    > {
    public:
        using Time = Units::NanoSeconds<uint64_t>;
        explicit TransportWorker(ESPNowTransport& owner) : _owner(owner) { SetStartOnInitialize(false); }
        void Configure(const ESPNowTransportConfig& config) {
            SetStackSize(config.ReceiveTaskStackSize);
            SetPriority(static_cast<unsigned int>(config.ReceiveTaskPriority));
            SetCoreID(static_cast<int>(config.ReceiveTaskCore));
            SetIterationPeriod(Units::MilliSeconds<uint32_t>(config.WorkerIterationIntervalMilliseconds));
        }
        uint32_t MinimumFreeStackBytes() const noexcept { return _minimumFreeStackBytes.load(std::memory_order_acquire); }
    protected:
        void Iterate(Time, Time, Threads::SkippedIterationCount) override {
            const uint32_t freeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
            uint32_t previous = _minimumFreeStackBytes.load(std::memory_order_relaxed);
            while ((previous == 0 || freeBytes < previous) &&
                   !_minimumFreeStackBytes.compare_exchange_weak(previous, freeBytes,
                       std::memory_order_release, std::memory_order_relaxed)) {}
            _owner.ProcessWorkerIteration();
        }
    private:
        ESPNowTransport& _owner;
        std::atomic<uint32_t> _minimumFreeStackBytes{0};
    };

    ESPNowTransportConfig _config;
    QueueHandle_t _receiveQueue = nullptr; // Native callback queue: deliberately internal.
    std::unique_ptr<TransportWorker> _worker;
    HandlerStorage _handlers;
    mutable std::mutex _handlerMutex;
    MaintenanceStorage _maintenanceHandlers;
    mutable std::mutex _maintenanceMutex;
    PeerHintStorage _peerInterfaceHints;
    mutable std::mutex _peerInterfaceMutex;
    ManagedPeerStorage _managedPeers;
    mutable std::mutex _managedPeerMutex;
    mutable std::recursive_mutex _nativeMutex;
    mutable std::mutex _radioBindingMutex;
    ESPNowRadioBinding _radioBinding{};
    std::atomic<bool> _initialized{false};
    std::atomic<bool> _nativeSuspended{false};
    std::shared_ptr<TransportObservable> _observable;
    std::atomic<ESPNowSendFailure> _lastSendFailure{ESPNowSendFailure::None};
    std::atomic<int32_t> _lastSendNativeError{0};

    ESPNowTransport()
        : _handlers(ESPRESSIO_ESPNOW_MAX_PROTOCOL_HANDLERS),
          _maintenanceHandlers(ESPRESSIO_ESPNOW_MAX_MAINTENANCE_HANDLERS),
          _peerInterfaceHints(ESPRESSIO_ESPNOW_MAX_INTERFACE_HINTS),
          _managedPeers(ESPRESSIO_ESPNOW_MAX_MANAGED_PEERS),
          _observable(System::Memory::MakeShared<
              TransportObservable,
              System::Memory::MemoryPolicy::ExternalPreferred
          >()) {}

    static ESPNowTransport*& CallbackInstance() {
        static ESPNowTransport* instance = nullptr;
        return instance;
    }

    static uint64_t GetRawMonotonicNanoseconds() {
        return static_cast<uint64_t>(esp_timer_get_time()) * 1000ULL;
    }

    static ESPNowSendFailure ClassifySendFailure(esp_err_t error) {
        if (error == ESP_OK) return ESPNowSendFailure::None;
#ifdef ESP_ERR_ESPNOW_NOT_INIT
        if (error == ESP_ERR_ESPNOW_NOT_INIT) return ESPNowSendFailure::NotInitialized;
#endif
#ifdef ESP_ERR_ESPNOW_ARG
        if (error == ESP_ERR_ESPNOW_ARG) return ESPNowSendFailure::InvalidArgument;
#endif
#ifdef ESP_ERR_ESPNOW_NO_MEM
        if (error == ESP_ERR_ESPNOW_NO_MEM) return ESPNowSendFailure::NoMemory;
#endif
#ifdef ESP_ERR_ESPNOW_NOT_FOUND
        if (error == ESP_ERR_ESPNOW_NOT_FOUND) return ESPNowSendFailure::PeerNotFound;
#endif
#ifdef ESP_ERR_ESPNOW_IF
        if (error == ESP_ERR_ESPNOW_IF) return ESPNowSendFailure::InterfaceMismatch;
#endif
#ifdef ESP_ERR_ESPNOW_CHAN
        if (error == ESP_ERR_ESPNOW_CHAN) return ESPNowSendFailure::ChannelMismatch;
#endif
#ifdef ESP_ERR_ESPNOW_INTERNAL
        if (error == ESP_ERR_ESPNOW_INTERNAL) return ESPNowSendFailure::Internal;
#endif
        return ESPNowSendFailure::Unknown;
    }

    static wifi_interface_t ExplicitInterface(ESPNowWiFiInterface interface) {
        return interface == ESPNowWiFiInterface::AccessPoint ? WIFI_IF_AP : WIFI_IF_STA;
    }

    static ESPNowWiFiInterface PublicInterface(wifi_interface_t interface) {
        return interface == WIFI_IF_AP ? ESPNowWiFiInterface::AccessPoint : ESPNowWiFiInterface::Station;
    }

    static wifi_interface_t CurrentDefaultInterface() {
        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) != ESP_OK) return WIFI_IF_STA;
        if (mode == WIFI_MODE_AP) return WIFI_IF_AP;
        if (mode == WIFI_MODE_STA) return WIFI_IF_STA;
        if (mode == WIFI_MODE_APSTA) return ::WiFi.status() == WL_CONNECTED ? WIFI_IF_STA : WIFI_IF_AP;
        return WIFI_IF_STA;
    }

    ESPNowRadioBinding RadioBindingSnapshot() const {
        std::lock_guard<std::mutex> lock(_radioBindingMutex);
        return _radioBinding;
    }

    wifi_interface_t ResolvePeerInterface(const ESPNowPeerConfig& config) const {
        if (config.Interface != ESPNowWiFiInterface::Auto) return ExplicitInterface(config.Interface);
        wifi_interface_t hinted = WIFI_IF_STA;
        if (FindPeerInterfaceHint(config.Address, hinted)) return hinted;
        const auto binding = RadioBindingSnapshot();
        if (binding.PreferredInterface == ESPNowWiFiInterface::Station ||
            binding.PreferredInterface == ESPNowWiFiInterface::AccessPoint) {
            return ExplicitInterface(binding.PreferredInterface);
        }
        return CurrentDefaultInterface();
    }

    static bool ResolveLocalInterface(const uint8_t* destination, wifi_interface_t& interface) {
        if (destination == nullptr) return false;
        static const uint8_t broadcast[MacAddressLength] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (std::memcmp(destination, broadcast, MacAddressLength) == 0) return false;
        uint8_t local[MacAddressLength] = {};
        if (esp_wifi_get_mac(WIFI_IF_STA, local) == ESP_OK && std::memcmp(destination, local, MacAddressLength) == 0) { interface = WIFI_IF_STA; return true; }
        if (esp_wifi_get_mac(WIFI_IF_AP, local) == ESP_OK && std::memcmp(destination, local, MacAddressLength) == 0) { interface = WIFI_IF_AP; return true; }
        return false;
    }

    bool FindPeerInterfaceHint(const MacAddress& address, wifi_interface_t& interface) const {
        std::lock_guard<std::mutex> lock(_peerInterfaceMutex);
        for (const auto& hint : _peerInterfaceHints) {
            if (hint.Used && hint.Address == address) { interface = hint.Interface; return true; }
        }
        return false;
    }

    bool IsManagedPeerAuto(const MacAddress& address) const {
        std::lock_guard<std::mutex> lock(_managedPeerMutex);
        for (const auto& record : _managedPeers) {
            if (record.Used && record.Config.Address == address) return record.Config.Interface == ESPNowWiFiInterface::Auto;
        }
        return false;
    }

    void RememberPeerInterface(const MacAddress& address, wifi_interface_t interface) {
        if (address.IsZero() || !IsManagedPeerAuto(address)) return;
        {
            std::lock_guard<std::mutex> lock(_peerInterfaceMutex);
            PeerInterfaceHint* freeHint = nullptr;
            bool updated = false;
            for (auto& hint : _peerInterfaceHints) {
                if (hint.Used && hint.Address == address) { hint.Interface = interface; updated = true; break; }
                if (!hint.Used && freeHint == nullptr) freeHint = &hint;
            }
            if (!updated && freeHint != nullptr) {
                freeHint->Used = true; freeHint->Address = address; freeHint->Interface = interface;
            }
        }
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        if (_nativeSuspended.load(std::memory_order_acquire)) return;
        if (esp_now_is_peer_exist(address.Bytes)) {
            esp_now_peer_info_t peer{};
            if (esp_now_get_peer(address.Bytes, &peer) == ESP_OK && peer.ifidx != interface) {
                peer.ifidx = interface;
                (void)esp_now_mod_peer(&peer);
            }
        }
    }

    void ClearPeerInterfaceHint(const MacAddress& address) {
        std::lock_guard<std::mutex> lock(_peerInterfaceMutex);
        for (auto& hint : _peerInterfaceHints) {
            if (hint.Used && hint.Address == address) { hint = PeerInterfaceHint{}; return; }
        }
    }

    void ClearPeerInterfaceHints() {
        std::lock_guard<std::mutex> lock(_peerInterfaceMutex);
        for (auto& hint : _peerInterfaceHints) hint = PeerInterfaceHint{};
    }

    ManagedPeerRecord* FindManagedPeerLocked(const MacAddress& address) {
        for (auto& record : _managedPeers) if (record.Used && record.Config.Address == address) return &record;
        return nullptr;
    }

    const ManagedPeerRecord* FindManagedPeerLocked(const MacAddress& address) const {
        for (const auto& record : _managedPeers) if (record.Used && record.Config.Address == address) return &record;
        return nullptr;
    }

    bool StoreManagedPeer(const ESPNowPeerConfig& config) {
        std::lock_guard<std::mutex> lock(_managedPeerMutex);
        if (auto* existing = FindManagedPeerLocked(config.Address)) {
            existing->Config = config;
            return true;
        }
        for (auto& record : _managedPeers) {
            if (!record.Used) { record.Used = true; record.Config = config; return true; }
        }
        return false;
    }

    void ForgetManagedPeer(const MacAddress& address) {
        std::lock_guard<std::mutex> lock(_managedPeerMutex);
        if (auto* existing = FindManagedPeerLocked(address)) *existing = ManagedPeerRecord{};
    }

    PeerConfigSnapshot CopyManagedPeerConfigs() const {
        PeerConfigSnapshot configs;
        configs.reserve(_managedPeers.size());
        std::lock_guard<std::mutex> lock(_managedPeerMutex);
        for (const auto& record : _managedPeers) {
            if (record.Used) configs.push_back(record.Config);
        }
        return configs;
    }

    bool ProgramPeerNativeLocked(const ESPNowPeerConfig& config, bool notifyAdded) {
        const bool existed = esp_now_is_peer_exist(config.Address.Bytes);
        esp_now_peer_info_t peer{};
        if (existed && esp_now_get_peer(config.Address.Bytes, &peer) != ESP_OK) return false;
        std::memcpy(peer.peer_addr, config.Address.Bytes, MacAddressLength);
        peer.channel = config.Channel;
        peer.ifidx = ResolvePeerInterface(config);
        peer.encrypt = config.Encrypt;
        if (config.Encrypt) std::memcpy(peer.lmk, config.LocalMasterKey, sizeof(peer.lmk));
        else std::memset(peer.lmk, 0, sizeof(peer.lmk));
        const esp_err_t result = existed ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer);
        if (result != ESP_OK) return false;
        if (notifyAdded && !existed) _observable->PeerAdded(config.Address);
        return true;
    }

    bool ReconcileManagedPeersLocked() {
        if (_nativeSuspended.load(std::memory_order_acquire)) return false;
        auto configs = CopyManagedPeerConfigs();
        bool success = true;
        for (const auto& config : configs) {
            if (!ProgramPeerNativeLocked(config, false)) success = false;
        }
        return success;
    }

    bool ReinitializeNativeStateLocked() {
        if (!GetIsInitialized()) return false;
        CallbackInstance() = nullptr;
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        if (esp_now_init() != ESP_OK) return false;
        CallbackInstance() = this;
        if (esp_now_register_recv_cb(ReceiveCallback) != ESP_OK) {
            CallbackInstance() = nullptr;
            esp_now_deinit();
            return false;
        }
        _nativeSuspended.store(false, std::memory_order_release);
        return ReconcileManagedPeersLocked();
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
        if (frame.HasLocalInterface) RememberPeerInterface(received.Source, frame.LocalInterface);
        _observable->FrameReceived(received.Source, received.Protocol, received.PayloadLength, received.ReceiveMonotonicNanoseconds);

        std::shared_ptr<ProtocolHandler> handler;
        {
            std::lock_guard<std::mutex> lock(_handlerMutex);
            for (const auto& record : _handlers) {
                if (record.Handler && record.Protocol == header.Protocol) {
                    handler = record.Handler;
                    break;
                }
            }
        }
        if (handler && *handler) (*handler)(received);
    }

    void ProcessWorkerIteration() {
        CallbackFrame frame;
        while (_receiveQueue != nullptr && xQueueReceive(_receiveQueue, &frame, 0) == pdTRUE) {
            ProcessCallbackFrame(frame);
        }

        std::array<std::shared_ptr<MaintenanceHandler>, ESPRESSIO_ESPNOW_MAX_MAINTENANCE_HANDLERS> handlers{};
        {
            std::lock_guard<std::mutex> lock(_maintenanceMutex);
            std::size_t index = 0;
            for (const auto& record : _maintenanceHandlers) {
                if (record.Owner != nullptr && record.Handler && index < handlers.size()) {
                    handlers[index++] = record.Handler;
                }
            }
        }
        const uint64_t nowMilliseconds = GetRawMonotonicNanoseconds() / 1000000ULL;
        for (const auto& handler : handlers) {
            if (handler && *handler) (*handler)(nowMilliseconds);
        }
    }

    static void QueueReceivedData(
        const uint8_t* source,
        const uint8_t* destination,
        const uint8_t* data,
        int length
    ) {
        ESPNowTransport* self = CallbackInstance();
        if (self == nullptr || !self->_initialized.load(std::memory_order_acquire) ||
            self->_receiveQueue == nullptr || source == nullptr || data == nullptr || length <= 0) return;
        CallbackFrame frame;
        frame.Source = MacAddress(source);
        frame.ReceiveMonotonicNanoseconds = GetRawMonotonicNanoseconds();
        frame.Length = static_cast<uint16_t>(length > static_cast<int>(MaximumFrameSize) ? MaximumFrameSize : length);
        frame.HasLocalInterface = ResolveLocalInterface(destination, frame.LocalInterface);
        std::memcpy(frame.Data, data, frame.Length);
        (void)xQueueSend(self->_receiveQueue, &frame, 0);
    }

#if ESP_IDF_VERSION_MAJOR >= 5
    static void ReceiveCallback(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
        QueueReceivedData(
            info == nullptr ? nullptr : info->src_addr,
            info == nullptr ? nullptr : info->des_addr,
            data,
            length
        );
    }
#else
    static void ReceiveCallback(const uint8_t* source, const uint8_t* data, int length) {
        QueueReceivedData(source, nullptr, data, length);
    }
#endif

    void CleanupFailedInitialization() {
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        _nativeSuspended.store(true, std::memory_order_release);
        CallbackInstance() = nullptr;
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        if (_worker) { _worker->Shutdown(); _worker.reset(); }
        if (_receiveQueue != nullptr) { vQueueDelete(_receiveQueue); _receiveQueue = nullptr; }
        _initialized.store(false, std::memory_order_release);
    }

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
    void UnregisterObserver(IESPNowTransportObserver* observer) { _observable->UnregisterObserver(observer); }

    bool Initialize(const ESPNowTransportConfig& config = ESPNowTransportConfig()) {
        if (_initialized.load(std::memory_order_acquire)) return true;
        _config = config;
        if (_config.WorkerIterationIntervalMilliseconds == 0 || _config.ReceiveQueueLength == 0) {
            _observable->InitializationFailed();
            return false;
        }
        if (_config.InitializeWiFi) ::WiFi.mode(WIFI_STA);
        if (_config.Channel != 0 && esp_wifi_set_channel(_config.Channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            _observable->InitializationFailed();
            return false;
        }
        _receiveQueue = xQueueCreate(_config.ReceiveQueueLength, sizeof(CallbackFrame));
        if (_receiveQueue == nullptr) { _observable->InitializationFailed(); return false; }
        _worker = std::make_unique<TransportWorker>(*this);
        _worker->Configure(_config);
        const auto workerInit = _worker->Initialize();
        if (workerInit != Threads::ThreadInitializationStatus::Success &&
            workerInit != Threads::ThreadInitializationStatus::AlreadyInitialized) {
            CleanupFailedInitialization();
            _observable->InitializationFailed();
            return false;
        }
        {
            std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
            if (esp_now_init() != ESP_OK) {
                CleanupFailedInitialization();
                _observable->InitializationFailed();
                return false;
            }
            CallbackInstance() = this;
            if (esp_now_register_recv_cb(ReceiveCallback) != ESP_OK) {
                CleanupFailedInitialization();
                _observable->InitializationFailed();
                return false;
            }
            _nativeSuspended.store(false, std::memory_order_release);
        }
        _lastSendFailure.store(ESPNowSendFailure::None);
        _lastSendNativeError.store(0);
        {
            std::lock_guard<std::mutex> lock(_radioBindingMutex);
            _radioBinding.Available = true;
            _radioBinding.Channel = _config.Channel;
            _radioBinding.PreferredInterface = ESPNowWiFiInterface::Auto;
        }
        _initialized.store(true, std::memory_order_release);
        const auto workerStart = _worker->Start();
        if (workerStart != Threads::ThreadInitializationStatus::Success &&
            workerStart != Threads::ThreadInitializationStatus::AlreadyInitialized) {
            CleanupFailedInitialization();
            _observable->InitializationFailed();
            return false;
        }
        _observable->Initialized();
        return true;
    }

    void Shutdown() {
        if (!_initialized.exchange(false, std::memory_order_acq_rel)) return;
        {
            std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
            _nativeSuspended.store(true, std::memory_order_release);
            esp_now_unregister_recv_cb();
            CallbackInstance() = nullptr;
            esp_now_deinit();
        }
        if (_worker) { _worker->Shutdown(); _worker.reset(); }
        if (_receiveQueue != nullptr) { vQueueDelete(_receiveQueue); _receiveQueue = nullptr; }
        { std::lock_guard<std::mutex> lock(_handlerMutex); for (auto& record : _handlers) record = HandlerRecord{}; }
        { std::lock_guard<std::mutex> lock(_maintenanceMutex); for (auto& record : _maintenanceHandlers) record = MaintenanceRecord{}; }
        { std::lock_guard<std::mutex> lock(_peerInterfaceMutex); for (auto& hint : _peerInterfaceHints) hint = PeerInterfaceHint{}; }
        { std::lock_guard<std::mutex> lock(_managedPeerMutex); for (auto& record : _managedPeers) record = ManagedPeerRecord{}; }
        { std::lock_guard<std::mutex> lock(_radioBindingMutex); _radioBinding = ESPNowRadioBinding{}; _radioBinding.Available = false; }
        _observable->Shutdown();
    }

    bool GetIsInitialized() const { return _initialized.load(std::memory_order_acquire); }
    uint32_t GetReceiveTaskMinimumFreeStackBytes() const { return _worker ? _worker->MinimumFreeStackBytes() : 0; }
    uint32_t GetWorkerIterationIntervalMilliseconds() const noexcept { return _config.WorkerIterationIntervalMilliseconds; }

    ESPNowSendResult GetLastSendResult() const noexcept {
        ESPNowSendResult result;
        result.Failure = _lastSendFailure.load();
        result.NativeError = _lastSendNativeError.load();
        result.Success = result.Failure == ESPNowSendFailure::None && result.NativeError == 0;
        return result;
    }

    ESPNowRadioBinding GetRadioBinding() const { return RadioBindingSnapshot(); }

    MacAddress GetLocalEndpointAddress(ESPNowWiFiInterface interface = ESPNowWiFiInterface::Auto) const {
        wifi_interface_t native = WIFI_IF_STA;
        switch (interface) {
            case ESPNowWiFiInterface::Auto: {
                const auto binding = RadioBindingSnapshot();
                switch (binding.PreferredInterface) {
                    case ESPNowWiFiInterface::AccessPoint: native = WIFI_IF_AP; break;
                    case ESPNowWiFiInterface::Station: native = WIFI_IF_STA; break;
                    case ESPNowWiFiInterface::Auto: native = CurrentDefaultInterface(); break;
                }
                break;
            }
            case ESPNowWiFiInterface::AccessPoint:
            case ESPNowWiFiInterface::Station:
                native = ExplicitInterface(interface);
                break;
        }
        uint8_t address[MacAddressLength] = {};
        return esp_wifi_get_mac(native, address) == ESP_OK ? MacAddress(address) : MacAddress{};
    }

    bool SetRadioAvailable(bool available) {
        std::lock_guard<std::mutex> lock(_radioBindingMutex);
        _radioBinding.Available = available;
        return true;
    }

    bool SuspendNativeForRadioTransition() {
        if (!GetIsInitialized()) return false;
        SetRadioAvailable(false);
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        _nativeSuspended.store(true, std::memory_order_release);
        CallbackInstance() = nullptr;
        esp_now_unregister_recv_cb();
        const esp_err_t result = esp_now_deinit();
        switch (result) {
            case ESP_OK: return true;
#ifdef ESP_ERR_ESPNOW_NOT_INIT
            case ESP_ERR_ESPNOW_NOT_INIT: return true;
#endif
            default: return false;
        }
    }

    bool ReconcileManagedPeers() {
        if (!GetIsInitialized() || _nativeSuspended.load(std::memory_order_acquire)) return false;
        ClearPeerInterfaceHints();
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        return ReconcileManagedPeersLocked();
    }

    bool ReinitializeNativeState() {
        if (!GetIsInitialized()) return false;
        ClearPeerInterfaceHints();
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        return ReinitializeNativeStateLocked();
    }

    bool ApplyRadioBinding(const ESPNowRadioBinding& binding, bool reinitializeNativeState = false) {
        if (!GetIsInitialized()) return false;
        { std::lock_guard<std::mutex> lock(_radioBindingMutex); _radioBinding = binding; _radioBinding.Available = false; }
        ClearPeerInterfaceHints();
        bool success = false;
        {
            std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
            const bool mustReinitialize = reinitializeNativeState || _nativeSuspended.load(std::memory_order_acquire);
            success = mustReinitialize ? ReinitializeNativeStateLocked() : ReconcileManagedPeersLocked();
        }
        { std::lock_guard<std::mutex> lock(_radioBindingMutex); _radioBinding = binding; _radioBinding.Available = binding.Available && success; }
        return success;
    }

    bool RegisterProtocolHandler(uint8_t protocol, ProtocolHandler handler) {
        if (!handler) return false;
        auto owned = System::Memory::MakeShared<
            ProtocolHandler,
            System::Memory::MemoryPolicy::ExternalPreferred
        >(std::move(handler));
        std::lock_guard<std::mutex> lock(_handlerMutex);
        for (auto& record : _handlers) {
            if (record.Handler && record.Protocol == protocol) {
                record.Handler = std::move(owned);
                return true;
            }
        }
        for (auto& record : _handlers) {
            if (!record.Handler) {
                record.Protocol = protocol;
                record.Handler = std::move(owned);
                return true;
            }
        }
        return false;
    }

    void UnregisterProtocolHandler(uint8_t protocol) {
        std::lock_guard<std::mutex> lock(_handlerMutex);
        for (auto& record : _handlers) {
            if (record.Handler && record.Protocol == protocol) { record = HandlerRecord{}; return; }
        }
    }

    bool RegisterMaintenanceHandler(const void* owner, MaintenanceHandler handler) {
        if (owner == nullptr || !handler) return false;
        auto owned = System::Memory::MakeShared<
            MaintenanceHandler,
            System::Memory::MemoryPolicy::ExternalPreferred
        >(std::move(handler));
        std::lock_guard<std::mutex> lock(_maintenanceMutex);
        for (auto& record : _maintenanceHandlers) {
            if (record.Owner == owner) { record.Handler = std::move(owned); return true; }
        }
        for (auto& record : _maintenanceHandlers) {
            if (record.Owner == nullptr) {
                record.Owner = owner;
                record.Handler = std::move(owned);
                return true;
            }
        }
        return false;
    }

    void UnregisterMaintenanceHandler(const void* owner) {
        if (owner == nullptr) return;
        std::lock_guard<std::mutex> lock(_maintenanceMutex);
        for (auto& record : _maintenanceHandlers) {
            if (record.Owner == owner) { record = MaintenanceRecord{}; return; }
        }
    }

    bool AddPeer(const ESPNowPeerConfig& config) {
        if (!GetIsInitialized() || config.Address.IsZero()) return false;
        if (!StoreManagedPeer(config)) return false;
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        if (_nativeSuspended.load(std::memory_order_acquire)) return true;
        if (!ProgramPeerNativeLocked(config, true)) {
            ForgetManagedPeer(config.Address);
            return false;
        }
        return true;
    }

    bool RemovePeer(const MacAddress& address) {
        if (!GetIsInitialized() || address.IsZero()) return false;
        std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
        if (_nativeSuspended.load(std::memory_order_acquire)) {
            ClearPeerInterfaceHint(address);
            ForgetManagedPeer(address);
            return true;
        }
        const bool existed = esp_now_is_peer_exist(address.Bytes);
        const esp_err_t result = esp_now_del_peer(address.Bytes);
        const bool success = result == ESP_OK || result == ESP_ERR_ESPNOW_NOT_FOUND;
        if (success) { ClearPeerInterfaceHint(address); ForgetManagedPeer(address); }
        if (success && existed) _observable->PeerRemoved(address);
        return success;
    }

    ESPNowSendResult SendDetailed(
        const MacAddress& destination,
        uint8_t protocol,
        const void* payload,
        std::size_t payloadLength
    ) {
        ESPNowSendResult result;
        if (!GetIsInitialized()) {
            result.Failure = ESPNowSendFailure::NotInitialized;
            result.NativeError = static_cast<int32_t>(ESP_ERR_ESPNOW_NOT_INIT);
        } else if (destination.IsZero() || payloadLength > MaximumFrameSize - sizeof(WireHeader)) {
            result.Failure = ESPNowSendFailure::InvalidArgument;
            result.NativeError = static_cast<int32_t>(ESP_ERR_ESPNOW_ARG);
        } else {
            std::lock_guard<std::recursive_mutex> nativeLock(_nativeMutex);
            if (_nativeSuspended.load(std::memory_order_acquire) || !RadioBindingSnapshot().Available) {
                result.Failure = ESPNowSendFailure::RadioUnavailable;
                result.NativeError = static_cast<int32_t>(ESP_ERR_INVALID_STATE);
            } else {
                // Native send buffer remains on internal task stack intentionally.
                uint8_t frame[MaximumFrameSize] = {0};
                WireHeader header;
                header.Protocol = protocol;
                header.PayloadLength = static_cast<uint16_t>(payloadLength);
                std::memcpy(frame, &header, sizeof(header));
                if (payload != nullptr && payloadLength > 0) {
                    std::memcpy(frame + sizeof(header), payload, payloadLength);
                }
                const esp_err_t native = esp_now_send(destination.Bytes, frame, sizeof(header) + payloadLength);
                result.Success = native == ESP_OK;
                result.NativeError = static_cast<int32_t>(native);
                result.Failure = ClassifySendFailure(native);
            }
        }
        _lastSendFailure.store(result.Failure);
        _lastSendNativeError.store(result.NativeError);
        if (result.Success) _observable->SendAccepted(destination, protocol, payloadLength);
        else _observable->SendFailed(destination, protocol, payloadLength, result.Failure, result.NativeError);
        return result;
    }

    bool Send(const MacAddress& destination, uint8_t protocol, const void* payload, std::size_t payloadLength) {
        return SendDetailed(destination, protocol, payload, payloadLength).Success;
    }

    uint64_t GetMonotonicTimestampNanoseconds() const { return GetRawMonotonicNanoseconds(); }
};

} // namespace ESPNow
} // namespace ESPressio
