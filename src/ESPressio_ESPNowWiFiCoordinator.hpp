#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowWiFiCoordinator.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>
#include <mutex>

#include <ESPressio_WiFi.hpp>
#include <ESPressio_IWiFiObserver.hpp>
#include <ESPressio_IWiFiRadioObserver.hpp>

#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio {
namespace ESPNow {

// Optional integration layer between ESPressio WiFi and ESP-NOW. WiFi owns
// the shared radio. This coordinator subscribes to both WiFi's low-level radio
// lifecycle and the AP-station topology lifecycle so ESP-NOW's native state
// stays aligned with the currently useful interface/channel. Standalone
// ESP-NOW users simply do not include this header and retain native/Arduino-
// WiFi behavior.
class ESPNowWiFiCoordinator final :
    public WiFi::IWiFiRadioObserver,
    public WiFi::IWiFiObserver {
public:
    ESPNowWiFiCoordinator(
        ESPNowTransport& transport,
        WiFi::WiFiManager& wifiManager
    ) : _transport(transport), _wifiManager(wifiManager) {}

    ESPNowWiFiCoordinator(const ESPNowWiFiCoordinator&) = delete;
    ESPNowWiFiCoordinator& operator=(const ESPNowWiFiCoordinator&) = delete;

    ~ESPNowWiFiCoordinator() override { Shutdown(); }

    bool Initialize() {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        if (_radioObserverHandle && _wifiObserverHandle) {
            return ApplyState(_wifiManager.RadioState(), false);
        }
        if (!_transport.GetIsInitialized()) return false;

        _radioObserverHandle = _wifiManager.RegisterRadioObserver(this);
        if (!_radioObserverHandle) return false;

        _wifiObserverHandle = _wifiManager.RegisterObserver(this);
        if (!_wifiObserverHandle) {
            _radioObserverHandle.reset();
            return false;
        }

        return ApplyState(_wifiManager.RadioState(), false);
    }

    void Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _wifiObserverHandle.reset();
        _radioObserverHandle.reset();
        _haveSnapshot = false;
        _scanSuspended = false;
        _nativeStateInvalidated = false;
        _transitionSuspendedNativeState = false;
    }

    bool IsInitialized() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        return static_cast<bool>(_radioObserverHandle) &&
            static_cast<bool>(_wifiObserverHandle);
    }

    ESPNowRadioBinding CurrentBinding() const {
        return _transport.GetRadioBinding();
    }

    static ESPNowWiFiInterface ResolvePreferredInterface(const WiFi::WiFiRadioState& state) {
        switch (state.Mode) {
            case WiFi::WiFiRadioMode::AccessPoint:
                return ESPNowWiFiInterface::AccessPoint;
            case WiFi::WiFiRadioMode::Station:
                return ESPNowWiFiInterface::Station;
            case WiFi::WiFiRadioMode::AccessPointStation:
                if (state.StationConnected) return ESPNowWiFiInterface::Station;
                if (state.AccessPointInterfaceActive) return ESPNowWiFiInterface::AccessPoint;
                return ESPNowWiFiInterface::Station;
            case WiFi::WiFiRadioMode::Off:
            default:
                return ESPNowWiFiInterface::Auto;
        }
    }

    bool RefreshBinding(bool forceNativeReinitialization = false) {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        return ApplyState(_wifiManager.RadioState(), forceNativeReinitialization);
    }

    void OnWiFiRadioTransitionBeginning(
        const WiFi::WiFiRadioState&,
        WiFi::WiFiRadioTransitionReason
    ) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);

        // The transport exclusively owns native ESP-NOW lifecycle operations.
        // Suspending through it serializes deinit against send/peer/reconcile
        // operations before WiFi mutates the underlying interface objects.
        if (_transport.GetIsInitialized()) {
            (void)_transport.SuspendNativeForRadioTransition();
            _nativeStateInvalidated = true;
            _transitionSuspendedNativeState = true;
        }
    }

    void OnWiFiRadioTransitionCompleted(
        const WiFi::WiFiRadioState&,
        const WiFi::WiFiRadioState& after,
        WiFi::WiFiRadioTransitionReason
    ) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        const bool rebuild =
            _transitionSuspendedNativeState ||
            _nativeStateInvalidated;
        _transitionSuspendedNativeState = false;
        (void)ApplyState(after, rebuild);
    }

    void OnWiFiRadioStateChanged(
        const WiFi::WiFiRadioState&,
        const WiFi::WiFiRadioState& after
    ) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        if (after.Scanning) {
            _scanSuspended = true;
            _transport.SetRadioAvailable(false);
            return;
        }
        if (_transitionSuspendedNativeState) return;
        (void)ApplyState(after, false);
    }

    void OnWiFiRadioScanBeginning(const WiFi::WiFiRadioState&) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _scanSuspended = true;
        _transport.SetRadioAvailable(false);
    }

    void OnWiFiRadioScanCompleted(const WiFi::WiFiRadioState& after) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _scanSuspended = false;
        if (_transitionSuspendedNativeState) return;
        (void)ApplyState(after, false);
    }

    void OnAccessPointStationConnected(const WiFi::MacAddress&) override {
        ReconcileAccessPointTopology();
    }

    void OnAccessPointStationDisconnected(const WiFi::MacAddress&) override {
        ReconcileAccessPointTopology();
    }

private:
    void ReconcileAccessPointTopology() {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        if (!_transport.GetIsInitialized() || _transitionSuspendedNativeState) return;

        // AP station association/disassociation is not a WiFiRadioState mode or
        // channel transition, but ESP-IDF may alter the operational SoftAP
        // interface underneath ESP-NOW. Rebuild the native ESP-NOW attachment
        // and replay logical peers so a previously valid AP-bound peer cannot
        // become permanently transmit-dead after a station topology change.
        _nativeStateInvalidated = true;
        (void)ApplyState(_wifiManager.RadioState(), true);
    }

    bool ApplyState(
        const WiFi::WiFiRadioState& state,
        bool forceNativeReinitialization
    ) {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        if (!_transport.GetIsInitialized()) return false;

        switch (state.Mode) {
            case WiFi::WiFiRadioMode::Off:
                _nativeStateInvalidated = true;
                _scanSuspended = false;
                _transport.SetRadioAvailable(false);
                _lastRadioMode = state.Mode;
                _lastPreferredInterface = ESPNowWiFiInterface::Auto;
                _lastChannel = 0;
                _haveSnapshot = true;
                return true;

            case WiFi::WiFiRadioMode::AccessPoint:
            case WiFi::WiFiRadioMode::Station:
            case WiFi::WiFiRadioMode::AccessPointStation:
                break;
        }

        if (state.Scanning) {
            _scanSuspended = true;
            _transport.SetRadioAvailable(false);
            return true;
        }

        const ESPNowWiFiInterface preferred = ResolvePreferredInterface(state);
        const bool interfaceChanged =
            _haveSnapshot && preferred != _lastPreferredInterface;
        const bool channelChanged = _haveSnapshot && state.Channel != _lastChannel;
        const bool radioModeChanged = _haveSnapshot && state.Mode != _lastRadioMode;

        ESPNowRadioBinding binding;
        binding.PreferredInterface = preferred;
        binding.Channel = state.Channel;
        binding.Available = !_scanSuspended;

        const bool rebuildRequired =
            forceNativeReinitialization ||
            _nativeStateInvalidated;
        bool success = _transport.ApplyRadioBinding(binding, rebuildRequired);

        if (
            !success &&
            !rebuildRequired &&
            (interfaceChanged || channelChanged || radioModeChanged)
        ) {
            success = _transport.ApplyRadioBinding(binding, true);
        }

        if (success) _nativeStateInvalidated = false;
        _lastRadioMode = state.Mode;
        _lastPreferredInterface = preferred;
        _lastChannel = state.Channel;
        _haveSnapshot = true;
        _scanSuspended = false;
        return success;
    }

    ESPNowTransport& _transport;
    WiFi::WiFiManager& _wifiManager;
    mutable std::recursive_mutex _stateMutex;
    Observable::ObserverHandlePtr _radioObserverHandle;
    Observable::ObserverHandlePtr _wifiObserverHandle;
    WiFi::WiFiRadioMode _lastRadioMode = WiFi::WiFiRadioMode::Off;
    ESPNowWiFiInterface _lastPreferredInterface = ESPNowWiFiInterface::Auto;
    uint8_t _lastChannel = 0;
    bool _haveSnapshot = false;
    bool _scanSuspended = false;
    bool _nativeStateInvalidated = false;
    bool _transitionSuspendedNativeState = false;
};

} // namespace ESPNow
} // namespace ESPressio
