#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowWiFiCoordinator.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>

#include <esp_wifi.h>

#include <ESPressio_WiFi.hpp>
#include <ESPressio_IWiFiObserver.hpp>

#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio {
namespace ESPNow {

// Optional integration layer between ESPressio WiFi and ESP-NOW. WiFi remains
// the authority for the shared radio; this coordinator observes WiFi directly
// and keeps ESP-NOW's logical peers bound to the currently useful interface
// and channel. Applications that do not use ESPressio WiFi do not include this
// header and retain the standalone ESP-NOW behavior.
class ESPNowWiFiCoordinator final : public WiFi::IWiFiObserver {
public:
    ESPNowWiFiCoordinator(
        ESPNowTransport& transport,
        WiFi::WiFiManager& wifiManager
    ) : _transport(transport), _wifiManager(wifiManager) {}

    ESPNowWiFiCoordinator(const ESPNowWiFiCoordinator&) = delete;
    ESPNowWiFiCoordinator& operator=(const ESPNowWiFiCoordinator&) = delete;

    ~ESPNowWiFiCoordinator() override { Shutdown(); }

    bool Initialize() {
        if (_observerHandle) return RefreshBinding(true);
        if (!_transport.GetIsInitialized()) return false;

        _observerHandle = _wifiManager.RegisterObserver(this);
        if (!_observerHandle) return false;
        return RefreshBinding(true);
    }

    void Shutdown() {
        _observerHandle.reset();
        _haveSnapshot = false;
        _scanSuspended = false;
    }

    bool IsInitialized() const noexcept { return static_cast<bool>(_observerHandle); }

    ESPNowRadioBinding CurrentBinding() const { return _transport.GetRadioBinding(); }

    static ESPNowWiFiInterface ResolvePreferredInterface(
        const WiFi::WiFiRuntimeState& state,
        wifi_mode_t nativeMode
    ) {
        if (nativeMode == WIFI_MODE_AP) return ESPNowWiFiInterface::AccessPoint;
        if (nativeMode == WIFI_MODE_STA) return ESPNowWiFiInterface::Station;
        if (nativeMode != WIFI_MODE_APSTA) return ESPNowWiFiInterface::Auto;

        const bool connected = state.Client.State == WiFi::ClientState::Connected;
        const bool fallbackAP = state.APUntilClient.FallbackAccessPointActive;
        const bool apActive = state.AccessPoint.State == WiFi::AccessPointState::Active || fallbackAP;

        if (state.Mode == WiFi::WiFiMode::APUntilClient) {
            if (connected && !fallbackAP) return ESPNowWiFiInterface::Station;
            if (fallbackAP) return ESPNowWiFiInterface::AccessPoint;
        }

        if (connected) return ESPNowWiFiInterface::Station;
        if (apActive) return ESPNowWiFiInterface::AccessPoint;
        return ESPNowWiFiInterface::Station;
    }

    bool RefreshBinding(bool forceNativeReinitialization = false) {
        if (!_transport.GetIsInitialized()) return false;

        const WiFi::WiFiRuntimeState state = _wifiManager.State();
        if (state.Scan == WiFi::ScanState::Scanning) {
            _scanSuspended = true;
            _transport.SetRadioAvailable(false);
            return true;
        }

        wifi_mode_t nativeMode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&nativeMode) != ESP_OK) {
            _transport.SetRadioAvailable(false);
            return false;
        }

        uint8_t primaryChannel = 0;
        wifi_second_chan_t secondaryChannel = WIFI_SECOND_CHAN_NONE;
        if (nativeMode != WIFI_MODE_NULL) {
            if (esp_wifi_get_channel(&primaryChannel, &secondaryChannel) != ESP_OK) {
                _transport.SetRadioAvailable(false);
                return false;
            }
        }

        const ESPNowWiFiInterface preferred = ResolvePreferredInterface(state, nativeMode);
        const bool nativeModeChanged = _haveSnapshot && nativeMode != _lastNativeMode;
        const bool interfaceChanged = _haveSnapshot && preferred != _lastPreferredInterface;
        const bool channelChanged = _haveSnapshot && primaryChannel != _lastChannel;

        ESPNowRadioBinding binding;
        binding.PreferredInterface = preferred;
        binding.Channel = primaryChannel;
        binding.Available = nativeMode != WIFI_MODE_NULL && !_scanSuspended;

        // Native AP/STA mode changes can invalidate the driver's ESP-NOW
        // interface binding. Rebuild native ESP-NOW state in that case while
        // preserving ESPressio logical peers/protocol handlers/worker state.
        const bool rebuildNative = forceNativeReinitialization || nativeModeChanged || interfaceChanged;
        const bool success = _transport.ApplyRadioBinding(binding, rebuildNative);

        _lastNativeMode = nativeMode;
        _lastPreferredInterface = preferred;
        _lastChannel = primaryChannel;
        _haveSnapshot = true;
        _scanSuspended = false;

        if (!success && channelChanged && !rebuildNative) {
            // A lightweight peer modification may fail after a driver channel
            // transition; perform one controlled native rebuild before giving
            // up the binding transition.
            return _transport.ApplyRadioBinding(binding, true);
        }
        return success;
    }

    void OnWiFiModeChanged(WiFi::WiFiMode, WiFi::WiFiMode) override {
        (void)RefreshBinding(true);
    }

    void OnClientStateChanged(
        const WiFi::ClientRuntimeState&,
        const WiFi::ClientRuntimeState&
    ) override {
        (void)RefreshBinding(false);
    }

    void OnAccessPointStateChanged(
        const WiFi::AccessPointRuntimeState&,
        const WiFi::AccessPointRuntimeState&
    ) override {
        (void)RefreshBinding(false);
    }

    void OnAPUntilClientStateChanged(
        const WiFi::APUntilClientRuntimeState&,
        const WiFi::APUntilClientRuntimeState&
    ) override {
        (void)RefreshBinding(false);
    }

    void OnScanStateChanged(WiFi::ScanState, WiFi::ScanState after) override {
        if (after == WiFi::ScanState::Scanning) {
            _scanSuspended = true;
            _transport.SetRadioAvailable(false);
            return;
        }
        const bool wasSuspended = _scanSuspended;
        _scanSuspended = false;
        (void)RefreshBinding(wasSuspended);
    }

    void OnClientNetworkSelected(const WiFi::ClientNetworkCandidate&) override {
        // Selection identifies the channel WiFi is about to pursue. Actual
        // ESP-NOW rebinding waits for the driver's authoritative channel after
        // the connection/scan transition has completed.
        _transport.SetRadioAvailable(false);
    }

private:
    ESPNowTransport& _transport;
    WiFi::WiFiManager& _wifiManager;
    Observable::ObserverHandlePtr _observerHandle;
    wifi_mode_t _lastNativeMode = WIFI_MODE_NULL;
    ESPNowWiFiInterface _lastPreferredInterface = ESPNowWiFiInterface::Auto;
    uint8_t _lastChannel = 0;
    bool _haveSnapshot = false;
    bool _scanSuspended = false;
};

} // namespace ESPNow
} // namespace ESPressio
