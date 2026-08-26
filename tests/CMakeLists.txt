cmake_minimum_required(VERSION 3.16)
project(ESPressioStateTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
    ESPRESSIO_OBSERVABLE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../ESPressio-Observable/src"
    CACHE PATH
    "Path to ESPressio Observable src"
)

if(NOT EXISTS "${ESPRESSIO_OBSERVABLE_DIR}/ESPressio_Observable.hpp")
    message(FATAL_ERROR "ESPressio Observable headers not found at ${ESPRESSIO_OBSERVABLE_DIR}")
endif()

enable_testing()

add_executable(test_state_foundation
    test_state_foundation.cpp
)

target_include_directories(test_state_foundation PRIVATE
    ../src
    ${ESPRESSIO_OBSERVABLE_DIR}
)

add_test(NAME state_foundation COMMAND test_state_foundation)

add_executable(test_state_introspection
    test_state_introspection.cpp
)

target_include_directories(test_state_introspection PRIVATE
    ../src
    ${ESPRESSIO_OBSERVABLE_DIR}
)

add_test(NAME state_introspection COMMAND test_state_introspection)

add_executable(test_state_protocol
    test_state_protocol.cpp
)

target_include_directories(test_state_protocol PRIVATE
    ../src
    ${ESPRESSIO_OBSERVABLE_DIR}
)

add_test(NAME state_protocol COMMAND test_state_protocol)

add_executable(test_state_comparison
    test_state_comparison.cpp
)

target_include_directories(test_state_comparison PRIVATE
    ../src
    ${ESPRESSIO_OBSERVABLE_DIR}
)

add_test(NAME state_comparison COMMAND test_state_comparison)

add_executable(test_state_comparison_remote
    test_state_comparison_remote.cpp
)

target_include_directories(test_state_comparison_remote PRIVATE
    ../src
    ${ESPRESSIO_OBSERVABLE_DIR}
)

add_test(NAME state_comparison_remote COMMAND test_state_comparison_remote)
