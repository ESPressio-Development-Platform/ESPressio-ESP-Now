#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowWiFiCoordinator.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>
#include <mutex>

#include <ESPressio_WiFi.hpp>
#include <ESPressio_IWiFiRadioObserver.hpp>

#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio {
namespace ESPNow {

/// <summary>Coordinates ESP-NOW with ESPressio WiFi's authoritative ownership of the shared ESP32 radio.</summary>
/// <remarks>WiFi controls native interface/mode/channel lifecycle. The coordinator mirrors that physical state into ESP-NOW binding, suspends ESP-NOW during destructive radio transitions/scans, and prefers the station interface in AP+STA mode when available so SoftAP client topology does not disturb ESP-NOW peers.</remarks>
class ESPNowWiFiCoordinator final : public WiFi::IWiFiRadioObserver {
public:
    /// <summary>Creates a coordinator over non-owning ESP-NOW transport and WiFi manager references.</summary>
    ESPNowWiFiCoordinator(
        ESPNowTransport& transport,
        WiFi::WiFiManager& wifiManager
    ) : _transport(transport), _wifiManager(wifiManager) {}

    ESPNowWiFiCoordinator(const ESPNowWiFiCoordinator&) = delete;
    ESPNowWiFiCoordinator& operator=(const ESPNowWiFiCoordinator&) = delete;

    ~ESPNowWiFiCoordinator() override { Shutdown(); }

    /// <summary>Registers for low-level WiFi radio lifecycle notifications and reconciles the current radio state into ESP-NOW.</summary>
    /// <returns>True when observation is active and the current binding can be applied.</returns>
    bool Initialize() {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        if (_observerHandle) return ApplyState(_wifiManager.RadioState(), false);
        if (!_transport.GetIsInitialized()) return false;

        _observerHandle = _wifiManager.RegisterRadioObserver(this);
        if (!_observerHandle) return false;
        return ApplyState(_wifiManager.RadioState(), false);
    }

    /// <summary>Unregisters from WiFi radio observation and clears coordinator transition bookkeeping.</summary>
    void Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _observerHandle.reset();
        _haveSnapshot = false;
        _scanSuspended = false;
        _nativeStateInvalidated = false;
        _transitionSuspendedNativeState = false;
    }

    /// <summary>Reports whether the WiFi radio observer registration is active.</summary>
    bool IsInitialized() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        return static_cast<bool>(_observerHandle);
    }

    /// <summary>Returns the current logical ESP-NOW shared-radio binding.</summary>
    ESPNowRadioBinding CurrentBinding() const {
        return _transport.GetRadioBinding();
    }

    /// <summary>Chooses the ESP-NOW interface best aligned with an authoritative WiFi radio state.</summary>
    static ESPNowWiFiInterface ResolvePreferredInterface(const WiFi::WiFiRadioState& state) {
        switch (state.Mode) {
            case WiFi::WiFiRadioMode::AccessPoint:
                return ESPNowWiFiInterface::AccessPoint;
            case WiFi::WiFiRadioMode::Station:
                return ESPNowWiFiInterface::Station;
            case WiFi::WiFiRadioMode::AccessPointStation:
                // In AP+STA mode, keep ESP-NOW on the station interface whenever
                // that interface exists. The station need not be associated with
                // an infrastructure AP for ESP-NOW to use it. This isolates the
                // ESP-NOW endpoint/peer table from SoftAP association lifecycle
                // changes (for example a phone joining the device's AP).
                if (state.StationInterfaceActive) return ESPNowWiFiInterface::Station;
                if (state.AccessPointInterfaceActive) return ESPNowWiFiInterface::AccessPoint;
                return ESPNowWiFiInterface::Auto;
            case WiFi::WiFiRadioMode::Off:
            default:
                return ESPNowWiFiInterface::Auto;
        }
    }

    /// <summary>Reconciles ESP-NOW against the WiFi manager's current physical radio state.</summary>
    /// <param name="forceNativeReinitialization">Force native ESP-NOW rebuild before managed-peer reconciliation.</param>
    /// <returns>True when the resulting binding is applied successfully.</returns>
    bool RefreshBinding(bool forceNativeReinitialization = false) {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        return ApplyState(_wifiManager.RadioState(), forceNativeReinitialization);
    }

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    void OnWiFiRadioScanBeginning(const WiFi::WiFiRadioState&) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _scanSuspended = true;
        _transport.SetRadioAvailable(false);
    }

    /// <inheritdoc/>
    void OnWiFiRadioScanCompleted(const WiFi::WiFiRadioState& after) override {
        std::lock_guard<std::recursive_mutex> lock(_stateMutex);
        _scanSuspended = false;
        if (_transitionSuspendedNativeState) return;
        (void)ApplyState(after, false);
    }

private:
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
