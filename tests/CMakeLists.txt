cmake_minimum_required(VERSION 3.16)
project(ESPressioStateTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(test_state_foundation
    test_state_foundation.cpp
)

target_include_directories(test_state_foundation PRIVATE
    ../src
)

add_test(NAME state_foundation COMMAND test_state_foundation)
