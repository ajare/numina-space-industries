cmake_minimum_required(VERSION 3.24)
project(NSICore VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

# No implementation classes yet; keep the scaffold dependency-free.
add_library(nsi-core INTERFACE)
add_library(nsi::core ALIAS nsi-core)
target_compile_features(nsi-core INTERFACE cxx_std_20)
target_include_directories(nsi-core INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

if(BUILD_TESTING)
    add_executable(nsi-core-consumer tests/CoreConsumer.cpp)
    target_link_libraries(nsi-core-consumer PRIVATE nsi::core)
    add_test(NAME nsi-core-consumer COMMAND nsi-core-consumer)
    set_tests_properties(nsi-core-consumer PROPERTIES
        LABELS "core;functional;validation-fast" TIMEOUT 30)
endif()
