#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESPressio_ESPNowCommandHandler.hpp requires an ESP32 Arduino target"
#endif

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>

#include <esp_wifi.h>

#include <ESPressio_Command.hpp>

#include "ESPressio_ESPNowTransport.hpp"

namespace ESPressio {
namespace ESPNow {

// Optional Command integration for runtime diagnostics and controlled
// experiments. The handler deliberately distinguishes live-safe radio-binding
// changes from initialization-only worker/queue settings.
class ESPNowCommandHandler {
public:
    using RadioChannelSetter = std::function<bool(uint8_t)>;

    struct Options {
        // Invoked by `espnow channel N` before the ESP-NOW binding is updated.
        // When ESPressio WiFi owns the radio, applications should route this to
        // WiFiManager::Configure(); standalone ESP-NOW applications may route it
        // directly to esp_wifi_set_channel().
        RadioChannelSetter SetRadioChannel;
    };

    bool Initialize(
        Command::CommandRegistry& registry,
        ESPNowTransport& transport,
        const ESPNowTransportConfig& initializationConfig,
        Options options = {}
    ) {
        if (_registration.Active()) return true;
        _transport = &transport;
        _initializationConfig = initializationConfig;
        _options = std::move(options);

        _registration = registry.RegisterCommand("espnow");
        if (!_registration.Active()) {
            _transport = nullptr;
            return false;
        }

        auto& root = registry.Command("espnow")
            .Description("ESPressio ESP-NOW runtime diagnostics and controlled configuration");

        root.Command("status")
            .Description("Show ESP-NOW transport, binding, native-radio and worker status")
            .OnExecute([this](const Command::CommandContext&) { return Status(); });

        root.Command("config")
            .Description("Show initialization-only and current runtime settings")
            .OnExecute([this](const Command::CommandContext&) { return Configuration(); });

        auto& channel = root.Command("channel")
            .Description("Set the physical shared-radio channel and ESP-NOW binding channel");
        channel.Parameter<unsigned int>("channel").Range(1, 14);
        channel.OnExecute([this](const Command::CommandContext& context) {
            return SetChannel(static_cast<uint8_t>(context.Get<unsigned int>("channel")));
        });

        auto& binding = root.Command("binding")
            .Description("Inspect or modify ESP-NOW's logical shared-radio binding");
        binding.Command("status")
            .OnExecute([this](const Command::CommandContext&) { return BindingStatus(); });

        auto& interfaceCommand = binding.Command("interface")
            .Description("Set preferred ESP-NOW interface without directly changing WiFi mode");
        interfaceCommand.Parameter("interface", Command::ParameterKind::Enumeration)
            .OneOf({"auto", "sta", "ap"});
        interfaceCommand.OnExecute([this](const Command::CommandContext& context) {
            auto current = _transport->GetRadioBinding();
            const auto value = context.Get<std::string>("interface");
            current.PreferredInterface = value == "sta"
                ? ESPNowWiFiInterface::Station
                : value == "ap" ? ESPNowWiFiInterface::AccessPoint : ESPNowWiFiInterface::Auto;
            return ApplyBinding(current, false);
        });

        auto& bindingChannel = binding.Command("channel")
            .Description("Set ESP-NOW peer binding channel; 0 follows the active radio channel");
        bindingChannel.Parameter<unsigned int>("channel").Range(0, 14);
        bindingChannel.OnExecute([this](const Command::CommandContext& context) {
            auto current = _transport->GetRadioBinding();
            current.Channel = static_cast<uint8_t>(context.Get<unsigned int>("channel"));
            return ApplyBinding(current, false);
        });

        auto& available = binding.Command("available")
            .Description("Temporarily enable/disable ESP-NOW transport access to the shared radio");
        available.Parameter<bool>("available");
        available.OnExecute([this](const Command::CommandContext& context) {
            auto current = _transport->GetRadioBinding();
            current.Available = context.Get<bool>("available");
            return ApplyBinding(current, false);
        });

        root.Command("reconcile")
            .Description("Force native ESP-NOW reinitialization and managed-peer reconciliation")
            .OnExecute([this](const Command::CommandContext&) {
                return ApplyBinding(_transport->GetRadioBinding(), true);
            });

        return true;
    }

    void Shutdown() {
        _registration.Reset();
        _transport = nullptr;
        _options = {};
        _initializationConfig = {};
    }

private:
    Command::CommandResult SetChannel(uint8_t channel) {
        if (!_options.SetRadioChannel) {
            return Command::CommandResult::Error(
                "No radio-channel authority configured; use the owning WiFi controller or supply ESPNowCommandHandler::Options::SetRadioChannel"
            );
        }
        if (!_options.SetRadioChannel(channel))
            return Command::CommandResult::Error("Radio channel change failed");

        auto binding = _transport->GetRadioBinding();
        binding.Channel = channel;
        binding.Available = true;
        return ApplyBinding(binding, true);
    }

    Command::CommandResult ApplyBinding(const ESPNowRadioBinding& binding, bool forceNativeReinitialization) {
        if (!_transport || !_transport->GetIsInitialized())
            return Command::CommandResult::Error("ESP-NOW transport is not initialized");
        if (!_transport->ApplyRadioBinding(binding, forceNativeReinitialization))
            return Command::CommandResult::Error("ESP-NOW radio binding/reconciliation failed");
        return Command::CommandResult::Ok(BindingText(_transport->GetRadioBinding()));
    }

    Command::CommandResult BindingStatus() const {
        if (!_transport) return Command::CommandResult::Error("ESP-NOW command handler is not initialized");
        return Command::CommandResult::Ok(BindingText(_transport->GetRadioBinding()));
    }

    Command::CommandResult Status() const {
        if (!_transport) return Command::CommandResult::Error("ESP-NOW command handler is not initialized");

        wifi_mode_t mode = WIFI_MODE_NULL;
        uint8_t primary = 0;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        const esp_err_t modeResult = esp_wifi_get_mode(&mode);
        const esp_err_t channelResult = esp_wifi_get_channel(&primary, &secondary);
        const auto binding = _transport->GetRadioBinding();
        const auto send = _transport->GetLastSendResult();

        std::ostringstream out;
        out << "initialized=" << (_transport->GetIsInitialized() ? "true" : "false")
            << " binding.interface=" << InterfaceName(binding.PreferredInterface)
            << " binding.channel=" << static_cast<unsigned>(binding.Channel)
            << " binding.available=" << (binding.Available ? "true" : "false")
            << " native.mode=" << (modeResult == ESP_OK ? NativeModeName(mode) : "unknown")
            << " native.channel=" << (channelResult == ESP_OK ? std::to_string(primary) : std::string("unknown"))
            << " worker.interval-ms=" << _transport->GetWorkerIterationIntervalMilliseconds()
            << " worker.minimum-free-stack=" << _transport->GetReceiveTaskMinimumFreeStackBytes()
            << " last-send.success=" << (send.Success ? "true" : "false")
            << " last-send.failure=" << static_cast<unsigned>(send.Failure)
            << " last-send.native-error=" << send.NativeError;
        return Command::CommandResult::Ok(out.str());
    }

    Command::CommandResult Configuration() const {
        std::ostringstream out;
        out << "initialize-wifi=" << (_initializationConfig.InitializeWiFi ? "true" : "false")
            << " initial-channel=" << static_cast<unsigned>(_initializationConfig.Channel)
            << " receive-stack=" << _initializationConfig.ReceiveTaskStackSize
            << " receive-priority=" << static_cast<unsigned>(_initializationConfig.ReceiveTaskPriority)
            << " receive-core=" << static_cast<int>(_initializationConfig.ReceiveTaskCore)
            << " receive-queue=" << _initializationConfig.ReceiveQueueLength
            << " worker.interval-ms=" << _initializationConfig.WorkerIterationIntervalMilliseconds
            << "\n"
            << BindingText(_transport ? _transport->GetRadioBinding() : ESPNowRadioBinding{})
            << "\ninitialization-only=initialize-wifi,receive-stack,receive-priority,receive-core,receive-queue,worker.interval-ms";
        return Command::CommandResult::Ok(out.str());
    }

    static std::string BindingText(const ESPNowRadioBinding& binding) {
        std::ostringstream out;
        out << "interface=" << InterfaceName(binding.PreferredInterface)
            << " channel=" << static_cast<unsigned>(binding.Channel)
            << " available=" << (binding.Available ? "true" : "false");
        return out.str();
    }

    static const char* InterfaceName(ESPNowWiFiInterface value) {
        switch (value) {
            case ESPNowWiFiInterface::Station: return "sta";
            case ESPNowWiFiInterface::AccessPoint: return "ap";
            default: return "auto";
        }
    }

    static const char* NativeModeName(wifi_mode_t mode) {
        switch (mode) {
            case WIFI_MODE_STA: return "sta";
            case WIFI_MODE_AP: return "ap";
            case WIFI_MODE_APSTA: return "apsta";
            case WIFI_MODE_NULL:
            default: return "off";
        }
    }

    ESPNowTransport* _transport = nullptr;
    ESPNowTransportConfig _initializationConfig{};
    Options _options{};
    Command::CommandRegistrationHandle _registration;
};

} // namespace ESPNow
} // namespace ESPressio
