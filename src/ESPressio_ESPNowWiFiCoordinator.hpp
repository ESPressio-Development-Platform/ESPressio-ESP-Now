#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowWiFiCoordinator.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>

#include <esp_now.h>

#include <ESPressio_WiFi.hpp>
#include <ESPressio_IWiFiRadioObserver.hpp>

#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio {
namespace ESPNow {

// Optional integration layer between ESPressio WiFi and ESP-NOW. WiFi owns
// the shared radio. This coordinator subscribes directly to WiFi's low-level
// radio lifecycle and keeps ESP-NOW's logical peers aligned with the currently
// useful interface/channel. Standalone ESP-NOW users simply do not include
// this header and retain the native/Arduino-WiFi behavior.
class ESPNowWiFiCoordinator final : public WiFi::IWiFiRadioObserver {
public:
    ESPNowWiFiCoordinator(
        ESPNowTransport& transport,
        WiFi::WiFiManager& wifiManager
    ) : _transport(transport), _wifiManager(wifiManager) {}

    ESPNowWiFiCoordinator(const ESPNowWiFiCoordinator&) = delete;
    ESPNowWiFiCoordinator& operator=(const ESPNowWiFiCoordinator&) = delete;

    ~ESPNowWiFiCoordinator() override { Shutdown(); }

    bool Initialize() {
        if (_observerHandle) return ApplyState(_wifiManager.RadioState(), false);
        if (!_transport.GetIsInitialized()) return false;

        _observerHandle = _wifiManager.RegisterRadioObserver(this);
        if (!_observerHandle) return false;
        return ApplyState(_wifiManager.RadioState(), false);
    }

    void Shutdown() {
        _observerHandle.reset();
        _haveSnapshot = false;
        _scanSuspended = false;
        _nativeStateInvalidated = false;
        _transitionSuspendedNativeState = false;
    }

    bool IsInitialized() const noexcept { return static_cast<bool>(_observerHandle); }
    ESPNowRadioBinding CurrentBinding() const { return _transport.GetRadioBinding(); }

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
        return ApplyState(_wifiManager.RadioState(), forceNativeReinitialization);
    }

    void OnWiFiRadioTransitionBeginning(
        const WiFi::WiFiRadioState&,
        WiFi::WiFiRadioTransitionReason
    ) override {
        // WiFi owns disruptive radio transitions. Block new ESP-NOW sends
        // first, then detach ESP-NOW from ESP-IDF's WiFi interface objects
        // before WiFi.mode()/SoftAP/STA startup tears those objects down.
        // Logical managed peers, protocol handlers, queues and the worker stay
        // alive in ESPressio; the post-transition rebuild recreates only the
        // native ESP-NOW attachment and peer table.
        _transport.SetRadioAvailable(false);
        if (_transport.GetIsInitialized()) {
            const esp_err_t result = esp_now_deinit();
            (void)result;
            _nativeStateInvalidated = true;
            _transitionSuspendedNativeState = true;
        }
    }

    void OnWiFiRadioTransitionCompleted(
        const WiFi::WiFiRadioState&,
        const WiFi::WiFiRadioState& after,
        WiFi::WiFiRadioTransitionReason
    ) override {
        // A transition-suspended native instance must always be rebuilt before
        // traffic resumes, even when the resulting interface/channel happens
        // to compare equal to the previous snapshot.
        const bool rebuild = _transitionSuspendedNativeState || _nativeStateInvalidated;
        _transitionSuspendedNativeState = false;
        (void)ApplyState(after, rebuild);
    }

    void OnWiFiRadioStateChanged(
        const WiFi::WiFiRadioState&,
        const WiFi::WiFiRadioState& after
    ) override {
        if (after.Scanning) {
            _scanSuspended = true;
            _transport.SetRadioAvailable(false);
            return;
        }
        if (_transitionSuspendedNativeState) return;
        (void)ApplyState(after, false);
    }

    void OnWiFiRadioScanBeginning(const WiFi::WiFiRadioState&) override {
        _scanSuspended = true;
        _transport.SetRadioAvailable(false);
    }

    void OnWiFiRadioScanCompleted(const WiFi::WiFiRadioState& after) override {
        _scanSuspended = false;
        if (_transitionSuspendedNativeState) return;
        (void)ApplyState(after, false);
    }

private:
    bool ApplyState(
        const WiFi::WiFiRadioState& state,
        bool forceNativeReinitialization
    ) {
        if (!_transport.GetIsInitialized()) return false;

        if (state.Mode == WiFi::WiFiRadioMode::Off) {
            // WiFi driver shutdown invalidates the native ESP-NOW instance.
            // Do not probe or reconcile the native peer table while the radio
            // is off; remember that the next active state requires a rebuild.
            _nativeStateInvalidated = true;
            _scanSuspended = false;
            _transport.SetRadioAvailable(false);
            _lastRadioMode = state.Mode;
            _lastPreferredInterface = ESPNowWiFiInterface::Auto;
            _lastChannel = 0;
            _haveSnapshot = true;
            return true;
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

        const bool rebuildRequired = forceNativeReinitialization || _nativeStateInvalidated;
        bool success = _transport.ApplyRadioBinding(binding, rebuildRequired);

        if (!success && !rebuildRequired &&
            (interfaceChanged || channelChanged || radioModeChanged)) {
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
    Observable::ObserverHandlePtr _observerHandle;
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
