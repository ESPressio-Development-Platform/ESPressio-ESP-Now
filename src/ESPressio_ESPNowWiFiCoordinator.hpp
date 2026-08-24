#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowWiFiCoordinator.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>

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
        if (_observerHandle) return ApplyState(_wifiManager.RadioState(), true);
        if (!_transport.GetIsInitialized()) return false;

        _observerHandle = _wifiManager.RegisterRadioObserver(this);
        if (!_observerHandle) return false;
        return ApplyState(_wifiManager.RadioState(), true);
    }

    void Shutdown() {
        _observerHandle.reset();
        _haveSnapshot = false;
        _scanSuspended = false;
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
        // No new ESP-NOW transmission should enter the driver while WiFi is
        // changing the shared radio mode/AP/STA/channel configuration.
        _transport.SetRadioAvailable(false);
    }

    void OnWiFiRadioTransitionCompleted(
        const WiFi::WiFiRadioState& before,
        const WiFi::WiFiRadioState& after,
        WiFi::WiFiRadioTransitionReason
    ) override {
        const bool modeChanged = before.Mode != after.Mode;
        const bool preferredChanged =
            ResolvePreferredInterface(before) != ResolvePreferredInterface(after);
        (void)ApplyState(after, modeChanged || preferredChanged);
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
        (void)ApplyState(after, false);
    }

    void OnWiFiRadioScanBeginning(const WiFi::WiFiRadioState&) override {
        _scanSuspended = true;
        _transport.SetRadioAvailable(false);
    }

    void OnWiFiRadioScanCompleted(const WiFi::WiFiRadioState& after) override {
        const bool wasSuspended = _scanSuspended;
        _scanSuspended = false;
        (void)ApplyState(after, wasSuspended);
    }

private:
    bool ApplyState(
        const WiFi::WiFiRadioState& state,
        bool forceNativeReinitialization
    ) {
        if (!_transport.GetIsInitialized()) return false;

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
        binding.Available = state.Mode != WiFi::WiFiRadioMode::Off && !_scanSuspended;

        const bool rebuildNative =
            forceNativeReinitialization || interfaceChanged || radioModeChanged;
        bool success = _transport.ApplyRadioBinding(binding, rebuildNative);

        if (!success && channelChanged && !rebuildNative) {
            // Some IDF transitions leave the native ESP-NOW peer table bound
            // to stale channel/interface internals even when esp_now_mod_peer
            // was attempted. Escalate once to a controlled native rebuild.
            success = _transport.ApplyRadioBinding(binding, true);
        }

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
};

} // namespace ESPNow
} // namespace ESPressio
