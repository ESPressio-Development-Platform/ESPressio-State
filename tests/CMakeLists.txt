cmake_minimum_required(VERSION 3.16)
project(ESPressioStateTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
    ESPRESSIO_SYSTEM_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../ESPressio-System/src"
    CACHE PATH
    "Path to ESPressio System src"
)
set(
    ESPRESSIO_OBSERVABLE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../ESPressio-Observable/src"
    CACHE PATH
    "Path to ESPressio Observable src"
)

if(NOT EXISTS "${ESPRESSIO_SYSTEM_DIR}/ESPressio_Memory.hpp")
    message(FATAL_ERROR "ESPressio System headers not found at ${ESPRESSIO_SYSTEM_DIR}")
endif()
if(NOT EXISTS "${ESPRESSIO_OBSERVABLE_DIR}/ESPressio_Observable.hpp")
    message(FATAL_ERROR "ESPressio Observable headers not found at ${ESPRESSIO_OBSERVABLE_DIR}")
endif()

set(ESPRESSIO_STATE_TEST_INCLUDE_DIRS
    ../src
    ${ESPRESSIO_SYSTEM_DIR}
    ${ESPRESSIO_OBSERVABLE_DIR}
)

enable_testing()

foreach(target IN ITEMS
    test_state_foundation
    test_state_introspection
    test_state_protocol
    test_state_comparison
    test_state_comparison_remote
    test_state_source_callback_boundaries
    test_local_state_registry
    test_state_publisher_reentrancy
    test_remote_state_reentrancy
)
    add_executable(${target} ${target}.cpp)
    target_include_directories(${target} PRIVATE ${ESPRESSIO_STATE_TEST_INCLUDE_DIRS})
endforeach()

add_test(NAME state_foundation COMMAND test_state_foundation)
add_test(NAME state_introspection COMMAND test_state_introspection)
add_test(NAME state_protocol COMMAND test_state_protocol)
add_test(NAME state_comparison COMMAND test_state_comparison)
add_test(NAME state_comparison_remote COMMAND test_state_comparison_remote)
add_test(NAME state_source_callback_boundaries COMMAND test_state_source_callback_boundaries)
add_test(NAME local_state_registry COMMAND test_local_state_registry)
add_test(NAME state_publisher_reentrancy COMMAND test_state_publisher_reentrancy)
add_test(NAME remote_state_reentrancy COMMAND test_remote_state_reentrancy)
