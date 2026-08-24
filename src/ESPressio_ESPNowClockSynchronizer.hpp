#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <ESPressio_IClockSynchronizationTarget.hpp>
#include <ESPressio_SystemClock.hpp>

#include "ESPressio_ESPNowTransport.hpp"
#include "ESPressio_ESPNowTypes.hpp"

namespace ESPressio {

    namespace ESPNow {

        class ESPNowClockSynchronizer {
            private:
                enum class MessageType : uint8_t {
                    Request = 1,
                    Response = 2
                };


                #pragma pack(push, 1)
                struct MessageHeader {
                    uint8_t Type = 0;
                    uint8_t Version = 1;
                    uint16_t Reserved = 0;
                    uint32_t Sequence = 0;
                };


                struct RequestMessage {
                    MessageHeader Header;
                    uint64_t T1 = 0;
                };


                struct ResponseMessage {
                    MessageHeader Header;
                    uint64_t T1 = 0;
                    uint64_t T2 = 0;
                    uint64_t T3 = 0;
                };
                #pragma pack(pop)


                ESPNowTransport* _transport =
                    nullptr;

                Timing::
                    IClockSynchronizationTarget<
                        Timing::ClockTick
                    >* _target =
                        nullptr;

                ESPNowClockSynchronizationConfig
                    _config;

                std::atomic<uint32_t>
                    _nextSequence{1};

                std::atomic<uint32_t>
                    _pendingSequence{0};

                uint64_t
                    _lastRequestMonotonicNanoseconds =
                        0;

                bool _initialized = false;

                mutable std::mutex
                    _stateMutex;


                bool IsClientMode() const {
                    return
                        _config.Mode ==
                            ESPNowClockSynchronizationMode::
                                Client ||
                        _config.Mode ==
                            ESPNowClockSynchronizationMode::
                                ClientAndReference;
                }


                bool IsReferenceMode() const {
                    return
                        _config.Mode ==
                            ESPNowClockSynchronizationMode::
                                Reference ||
                        _config.Mode ==
                            ESPNowClockSynchronizationMode::
                                ClientAndReference;
                }


                uint64_t
                RecoverSystemTimestamp(
                    uint64_t receiveMonotonicTimestamp
                ) const {
                    const uint64_t
                        nowMonotonic =
                            _transport->
                                GetMonotonicTimestampNanoseconds();

                    const uint64_t
                        nowSystem =
                            _target->
                                GetSynchronizationTimestampNanoseconds();

                    if (
                        nowMonotonic <=
                            receiveMonotonicTimestamp
                    ) {
                        return nowSystem;
                    }

                    const uint64_t elapsed =
                        nowMonotonic -
                        receiveMonotonicTimestamp;

                    return
                        nowSystem >= elapsed
                            ? nowSystem -
                                elapsed
                            : 0;
                }


                void ProcessFrame(
                    const ESPNowReceivedFrame&
                        frame
                ) {
                    if (
                        frame.PayloadLength <
                        sizeof(MessageHeader)
                    ) {
                        return;
                    }

                    MessageHeader header;

                    std::memcpy(
                        &header,
                        frame.Payload,
                        sizeof(header)
                    );

                    if (header.Version != 1) {
                        return;
                    }

                    if (
                        header.Type ==
                        static_cast<uint8_t>(
                            MessageType::Request
                        )
                    ) {
                        ProcessRequest(
                            frame
                        );
                    } else if (
                        header.Type ==
                        static_cast<uint8_t>(
                            MessageType::Response
                        )
                    ) {
                        ProcessResponse(
                            frame
                        );
                    }
                }


                void ProcessRequest(
                    const ESPNowReceivedFrame&
                        frame
                ) {
                    if (
                        !IsReferenceMode() ||
                        frame.PayloadLength !=
                            sizeof(RequestMessage)
                    ) {
                        return;
                    }

                    RequestMessage request;

                    std::memcpy(
                        &request,
                        frame.Payload,
                        sizeof(request)
                    );

                    const uint64_t t2 =
                        RecoverSystemTimestamp(
                            frame.
                                ReceiveMonotonicNanoseconds
                        );

                    ResponseMessage response;

                    response.Header.Type =
                        static_cast<uint8_t>(
                            MessageType::Response
                        );

                    response.Header.Version =
                        1;

                    response.Header.Sequence =
                        request.Header.Sequence;

                    response.T1 =
                        request.T1;

                    response.T2 =
                        t2;

                    ESPNowPeerConfig peer;

                    peer.Address =
                        frame.Source;

                    if (
                        !_transport->AddPeer(
                            peer
                        )
                    ) {
                        return;
                    }

                    /*
                     * Capture T3 only after any peer-registration work, as
                     * close to the actual ESP-NOW send operation as possible.
                     */
                    response.T3 =
                        _target->
                            GetSynchronizationTimestampNanoseconds();

                    _transport->Send(
                        frame.Source,
                        static_cast<uint8_t>(
                            ESPNowProtocol::
                                ClockSynchronization
                        ),
                        &response,
                        sizeof(response)
                    );
                }


                void ProcessResponse(
                    const ESPNowReceivedFrame&
                        frame
                ) {
                    if (
                        !IsClientMode() ||
                        frame.PayloadLength !=
                            sizeof(ResponseMessage)
                    ) {
                        return;
                    }

                    if (
                        !_config.ReferencePeer.
                            IsZero() &&
                        frame.Source !=
                            _config.ReferencePeer
                    ) {
                        return;
                    }

                    ResponseMessage response;

                    std::memcpy(
                        &response,
                        frame.Payload,
                        sizeof(response)
                    );

                    const uint32_t
                        pendingSequence =
                            _pendingSequence.load(
                                std::memory_order_acquire
                            );

                    if (
                        pendingSequence == 0 ||
                        response.Header.Sequence !=
                            pendingSequence
                    ) {
                        return;
                    }

                    /*
                     * Consume the outstanding exchange before submitting it;
                     * duplicate or delayed responses for this sequence will
                     * therefore be ignored.
                     */
                    _pendingSequence.store(
                        0,
                        std::memory_order_release
                    );

                    Timing::
                        ClockSynchronizationSample<
                            Timing::ClockTick
                        > sample;

                    sample.
                        LocalRequestTransmitTime =
                            response.T1;

                    sample.
                        RemoteRequestReceiveTime =
                            response.T2;

                    sample.
                        RemoteResponseTransmitTime =
                            response.T3;

                    sample.
                        LocalResponseReceiveTime =
                            RecoverSystemTimestamp(
                                frame.
                                    ReceiveMonotonicNanoseconds
                            );

                    _target->
                        SubmitSynchronizationSample(
                            sample,
                            _config.
                                AdjustmentMode
                        );
                }


            public:
                explicit
                ESPNowClockSynchronizer(
                    ESPNowTransport* transport =
                        nullptr,
                    Timing::
                        IClockSynchronizationTarget<
                            Timing::ClockTick
                        >* target =
                            nullptr
                ) :
                    _transport(
                        transport == nullptr
                            ? &ESPNowTransport::
                                GetInstance()
                            : transport
                    ),
                    _target(
                        target == nullptr
                            ? static_cast<
                                Timing::
                                    IClockSynchronizationTarget<
                                        Timing::ClockTick
                                    >*
                              >(
                                &Timing::
                                    SystemClock<>::
                                        GetInstance()
                              )
                            : target
                    ) {
                }


                ~ESPNowClockSynchronizer() {
                    Shutdown();
                }


                bool Initialize(
                    const ESPNowClockSynchronizationConfig&
                        config
                ) {
                    if (
                        _transport == nullptr ||
                        _target == nullptr ||
                        !_transport->
                            GetIsInitialized()
                    ) {
                        return false;
                    }

                    {
                        std::lock_guard<
                            std::mutex
                        > lock(
                            _stateMutex
                        );

                        _config =
                            config;
                    }

                    if (
                        IsClientMode() &&
                        !_config.ReferencePeer.
                            IsZero()
                    ) {
                        ESPNowPeerConfig peer;

                        peer.Address =
                            _config.ReferencePeer;

                        if (
                            !_transport->
                                AddPeer(
                                    peer
                                )
                        ) {
                            return false;
                        }
                    }

                    if (
                        !_transport->
                            RegisterProtocolHandler(
                                static_cast<uint8_t>(
                                    ESPNowProtocol::
                                        ClockSynchronization
                                ),
                                [this](
                                    const ESPNowReceivedFrame&
                                        frame
                                ) {
                                    ProcessFrame(
                                        frame
                                    );
                                }
                            )
                    ) {
                        return false;
                    }

                    _lastRequestMonotonicNanoseconds =
                        0;

                    _pendingSequence.store(
                        0,
                        std::memory_order_release
                    );

                    _initialized =
                        true;

                    return true;
                }


                void Shutdown() {
                    if (!_initialized) {
                        return;
                    }

                    _initialized =
                        false;

                    _pendingSequence.store(
                        0,
                        std::memory_order_release
                    );

                    if (_transport != nullptr) {
                        _transport->
                            UnregisterProtocolHandler(
                                static_cast<uint8_t>(
                                    ESPNowProtocol::
                                        ClockSynchronization
                                )
                            );
                    }
                }


                bool RequestSynchronization() {
                    if (
                        !_initialized ||
                        !IsClientMode() ||
                        _config.ReferencePeer.
                            IsZero()
                    ) {
                        return false;
                    }

                    RequestMessage request;

                    request.Header.Type =
                        static_cast<uint8_t>(
                            MessageType::Request
                        );

                    request.Header.Version =
                        1;

                    request.Header.Sequence =
                        _nextSequence.
                            fetch_add(
                                1
                            );

                    /*
                     * T1 is captured as close to esp_now_send() as practical.
                     */
                    request.T1 =
                        _target->
                            GetSynchronizationTimestampNanoseconds();

                    _pendingSequence.store(
                        request.Header.Sequence,
                        std::memory_order_release
                    );

                    /*
                     * Rate-limit synchronization attempts, not only successful
                     * sends. ESP-IDF explicitly permits transient send
                     * rejection such as ESP_ERR_ESPNOW_NO_MEM; leaving this
                     * timestamp at zero would make Update() retry on every
                     * ESP-NOW worker iteration and turn resource pressure into
                     * a tight retry storm.
                     */
                    _lastRequestMonotonicNanoseconds =
                        _transport->
                            GetMonotonicTimestampNanoseconds();

                    const bool sent =
                        _transport->Send(
                            _config.ReferencePeer,
                            static_cast<uint8_t>(
                                ESPNowProtocol::
                                    ClockSynchronization
                            ),
                            &request,
                            sizeof(request)
                        );

                    if (!sent) {
                        _pendingSequence.store(
                            0,
                            std::memory_order_release
                        );
                    }

                    return sent;
                }


                void Update() {
                    if (
                        !_initialized ||
                        !IsClientMode() ||
                        _config.
                            SynchronizationIntervalMilliseconds ==
                                0
                    ) {
                        return;
                    }

                    const uint64_t now =
                        _transport->
                            GetMonotonicTimestampNanoseconds();

                    const uint64_t interval =
                        static_cast<uint64_t>(
                            _config.
                                SynchronizationIntervalMilliseconds
                        ) *
                        1000000ULL;

                    if (
                        _lastRequestMonotonicNanoseconds ==
                            0 ||
                        now -
                            _lastRequestMonotonicNanoseconds >=
                            interval
                    ) {
                        RequestSynchronization();
                    }
                }


                bool GetIsInitialized()
                    const {
                    return _initialized;
                }


                ESPNowClockSynchronizationConfig
                GetConfig() const {
                    std::lock_guard<
                        std::mutex
                    > lock(
                        _stateMutex
                    );

                    return _config;
                }


                Timing::
                    ClockSynchronizationStatus<
                        Timing::ClockTick
                    >
                GetSynchronizationStatus()
                    const {
                    return
                        _target->
                            GetSynchronizationStatus();
                }
        };

    }

}
