cmake_minimum_required(VERSION 3.16)
project(ESPressioESPNowTests LANGUAGES CXX)

enable_testing()

if(NOT ESPRESSIO_COMMAND_INCLUDE_DIR)
    message(FATAL_ERROR "ESPRESSIO_COMMAND_INCLUDE_DIR is required")
endif()

add_executable(test_command_transport test_command_transport.cpp)
target_compile_features(test_command_transport PRIVATE cxx_std_17)
target_compile_options(test_command_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(test_command_transport PRIVATE
    ../src
    ${ESPRESSIO_COMMAND_INCLUDE_DIR}
)
add_test(NAME ESPNowCommandTransport COMMAND test_command_transport)
