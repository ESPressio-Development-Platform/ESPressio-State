cmake_minimum_required(VERSION 3.16)
project(ESPressioStateTypedContracts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ESPRESSIO_DEPENDENCIES "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Sibling ESPressio dependency checkout root")
set(STATE_INCLUDES ../src)
foreach(dependency System Primitive Task Threads Units Timing Serializable Persistence)
    list(APPEND STATE_INCLUDES "${ESPRESSIO_DEPENDENCIES}/ESPressio-${dependency}/src")
endforeach()
# Timing still declares Observable for its SystemClock observer API. This is a
# test-harness transitive include only; ESPressio-State itself does not depend on Observable.
list(APPEND STATE_INCLUDES "${ESPRESSIO_DEPENDENCIES}/ESPressio-Observable/src")
list(APPEND STATE_INCLUDES "${ESPRESSIO_DEPENDENCIES}/ESPressio-Timing/tests/stubs")

# S5-20/S5-21 eradication guards. These predecessor surfaces are intentionally
# absent from the V1 architecture and must not re-enter through compatibility headers.
set(STATE_FORBIDDEN_HEADERS
    ESPressio_LocalStateRegistry.hpp
    ESPressio_RemoteStateManager.hpp
    ESPressio_RemoteStateObserverThread.hpp
    ESPressio_StateAvailability.hpp
    ESPressio_StateCodec.hpp
    ESPressio_StateContract.hpp
    ESPressio_StateObservers.hpp
    ESPressio_StatePublisher.hpp
    ESPressio_StateProtocol.hpp
    ESPressio_StateRuntimeEpoch.hpp
    ESPressio_StateSerialization.hpp
    ESPressio_StateSubscriberRegistry.hpp
    ESPressio_StateTransport.hpp)
foreach(header IN LISTS STATE_FORBIDDEN_HEADERS)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/${header}")
        message(FATAL_ERROR "Legacy State header unexpectedly present: ${header}")
    endif()
endforeach()
file(GLOB STATE_PUBLIC_HEADERS "${CMAKE_CURRENT_LIST_DIR}/../src/*.hpp")
set(STATE_FORBIDDEN_SYMBOLS
    StateEpoch
    StateRuntimeEpoch
    RemoteStateObserverThread
    StateAvailability
    StateSourceReachability
    StateCodec
    RemoteStateManager
    StatePublisher
    LocalStateRegistry)
foreach(header IN LISTS STATE_PUBLIC_HEADERS)
    file(READ "${header}" header_text)
    foreach(symbol IN LISTS STATE_FORBIDDEN_SYMBOLS)
        string(FIND "${header_text}" "${symbol}" legacy_index)
        if(NOT legacy_index EQUAL -1)
            message(FATAL_ERROR "Legacy State symbol ${symbol} found in ${header}")
        endif()
    endforeach()
endforeach()

enable_testing()
foreach(name type_contract version local_runtime owner_runtime observer wire_v1 persistence remote_sessions remote_admission remote_runtime transport_binding subscriptions dynamic_read runtime_identity_projection shutdown initialization continuity_feedback convergence_feedback remote_tooling remote_observation comparison_policy resources)
    add_executable(state_${name} test_state_${name}.cpp)
    target_include_directories(state_${name} PRIVATE ${STATE_INCLUDES})
    target_compile_options(state_${name} PRIVATE -Wall -Wextra -Werror -fno-rtti -UNDEBUG)
    add_test(NAME state_${name} COMMAND state_${name})
endforeach()
set(NEGATIVE_INCLUDE_ARGS "-I${CMAKE_CURRENT_LIST_DIR}/../src")
foreach(dependency System Primitive Task Threads Units Timing Serializable Persistence)
    list(APPEND NEGATIVE_INCLUDE_ARGS "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-${dependency}/src")
endforeach()
list(APPEND NEGATIVE_INCLUDE_ARGS "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-Observable/src")
list(APPEND NEGATIVE_INCLUDE_ARGS "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-Timing/tests/stubs")
function(add_state_compile_failure name)
    add_test(NAME state_compile_fail_${name}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++17 -fno-rtti -Wall -Wextra -Werror -fsyntax-only ${NEGATIVE_INCLUDE_ARGS}
            "${CMAKE_CURRENT_LIST_DIR}/negative/${name}.cpp")
    set_tests_properties(state_compile_fail_${name} PROPERTIES WILL_FAIL TRUE)
endfunction()
add_state_compile_failure(zero_state_type_id)
add_state_compile_failure(invalid_state_storage_traits)
add_state_compile_failure(unbounded_serializable_state)
