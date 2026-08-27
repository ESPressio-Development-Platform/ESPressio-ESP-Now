cmake_minimum_required(VERSION 3.16)
project(ESPressioESPNowTests LANGUAGES CXX)

enable_testing()

add_executable(test_core_types test_core_types.cpp)
target_compile_features(test_core_types PRIVATE cxx_std_17)
target_compile_options(test_core_types PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_core_types PRIVATE stubs ../src)
add_test(NAME ESPNowCoreTypes COMMAND test_core_types)

add_executable(test_peer_liveness test_peer_liveness.cpp)
target_compile_features(test_peer_liveness PRIVATE cxx_std_17)
target_compile_options(test_peer_liveness PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_peer_liveness PRIVATE stubs ../src)
add_test(NAME ESPNowPeerLiveness COMMAND test_peer_liveness)

add_executable(test_security_protocol test_security_protocol.cpp)
target_compile_features(test_security_protocol PRIVATE cxx_std_17)
target_compile_options(test_security_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_security_protocol PRIVATE stubs ../src)
add_test(NAME ESPNowSecurityProtocol COMMAND test_security_protocol)

if(NOT ESPRESSIO_SYSTEM_INCLUDE_DIR)
    message(FATAL_ERROR "ESPRESSIO_SYSTEM_INCLUDE_DIR is required")
endif()

if(NOT ESPRESSIO_COMMAND_INCLUDE_DIR)
    message(FATAL_ERROR "ESPRESSIO_COMMAND_INCLUDE_DIR is required")
endif()

if(NOT ESPRESSIO_OBSERVABLE_INCLUDE_DIR)
    message(FATAL_ERROR "ESPRESSIO_OBSERVABLE_INCLUDE_DIR is required")
endif()

add_executable(test_command_transport test_command_transport.cpp)
target_compile_features(test_command_transport PRIVATE cxx_std_17)
target_compile_options(test_command_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_command_transport PRIVATE
    ../src
    ${ESPRESSIO_SYSTEM_INCLUDE_DIR}
    ${ESPRESSIO_COMMAND_INCLUDE_DIR}
    ${ESPRESSIO_OBSERVABLE_INCLUDE_DIR}
)
add_test(NAME ESPNowCommandTransport COMMAND test_command_transport)

add_executable(test_async_command_handoff test_async_command_handoff.cpp)
target_compile_features(test_async_command_handoff PRIVATE cxx_std_17)
target_compile_options(test_async_command_handoff PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_async_command_handoff PRIVATE
    ../src
    ${ESPRESSIO_SYSTEM_INCLUDE_DIR}
    ${ESPRESSIO_COMMAND_INCLUDE_DIR}
    ${ESPRESSIO_OBSERVABLE_INCLUDE_DIR}
)
add_test(NAME ESPNowAsyncCommandHandoff COMMAND test_async_command_handoff)

add_executable(test_observer_contract test_observer_contract.cpp)
target_compile_features(test_observer_contract PRIVATE cxx_std_17)
target_compile_options(test_observer_contract PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_observer_contract PRIVATE
    stubs
    ../src
    ${ESPRESSIO_SYSTEM_INCLUDE_DIR}
    ${ESPRESSIO_OBSERVABLE_INCLUDE_DIR}
)
add_test(NAME ESPNowObserverContract COMMAND test_observer_contract)
