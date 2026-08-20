#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <freertos/FreeRTOS.h>

#include <ESPressio_ClockSynchronization.hpp>

namespace ESPressio {

    namespace ESPNow {

        static constexpr std::size_t MacAddressLength = 6;

        /*
         * ESP-NOW v1 interoperability is limited to 250-byte payloads.
         * Keep the library's initial wire format within that limit even on
         * ESP-NOW v2 capable devices.
         */
        static constexpr std::size_t MaximumFrameSize = 250;

        static constexpr uint32_t FrameMagic =
            0x4553504EU; // "ESPN"

        static constexpr uint8_t FrameVersion = 1;


        enum class ESPNowProtocol : uint8_t {
            ClockSynchronization = 1,
            EventTransport = 2,
            CommandTransport = 3,
            UserBase = 64
        };


        enum class ESPNowClockSynchronizationMode : uint8_t {
            Disabled = 0,
            Client = 1,
            Reference = 2,
            ClientAndReference = 3
        };


        struct MacAddress {
            uint8_t Bytes[
                MacAddressLength
            ] = {0, 0, 0, 0, 0, 0};


            MacAddress() = default;


            explicit MacAddress(
                const uint8_t* address
            ) {
                if (address != nullptr) {
                    std::memcpy(
                        Bytes,
                        address,
                        MacAddressLength
                    );
                }
            }


            bool operator==(
                const MacAddress& other
            ) const {
                return
                    std::memcmp(
                        Bytes,
                        other.Bytes,
                        MacAddressLength
                    ) == 0;
            }


            bool operator!=(
                const MacAddress& other
            ) const {
                return !(*this == other);
            }


            bool IsZero() const {
                static const uint8_t zero[
                    MacAddressLength
                ] = {0, 0, 0, 0, 0, 0};

                return
                    std::memcmp(
                        Bytes,
                        zero,
                        MacAddressLength
                    ) == 0;
            }
        };


        struct ESPNowTransportConfig {
            bool InitializeWiFi = true;
            uint8_t Channel = 0;
            uint32_t ReceiveTaskStackSize = 4096;
            UBaseType_t ReceiveTaskPriority = 2;
            BaseType_t ReceiveTaskCore = tskNO_AFFINITY;
            std::size_t ReceiveQueueLength = 12;
        };


        struct ESPNowPeerConfig {
            MacAddress Address;
            uint8_t Channel = 0;
            bool Encrypt = false;
            uint8_t LocalMasterKey[16] = {0};
        };


        struct ESPNowReceivedFrame {
            MacAddress Source;
            uint64_t ReceiveMonotonicNanoseconds = 0;
            uint8_t Protocol = 0;
            uint16_t PayloadLength = 0;
            uint8_t Payload[
                MaximumFrameSize
            ] = {0};
        };


        struct ESPNowClockSynchronizationConfig {
            ESPNowClockSynchronizationMode Mode =
                ESPNowClockSynchronizationMode::
                    Disabled;

            MacAddress ReferencePeer;

            uint32_t SynchronizationIntervalMilliseconds =
                1000;

            Timing::
                ClockSynchronizationAdjustmentMode
                    AdjustmentMode =
                        Timing::
                            ClockSynchronizationAdjustmentMode::
                                SlewOnly;
        };

    }

}
