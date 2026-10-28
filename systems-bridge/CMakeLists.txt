cmake_minimum_required(VERSION 3.24)
project(NSISystemsBridge VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

if(NOT TARGET StationSystems::StationSystems)
    add_subdirectory(../station-systems "${CMAKE_CURRENT_BINARY_DIR}/station-systems")
endif()
if(NOT TARGET nsi::core)
    add_subdirectory(../core "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

add_library(nsi-systems-bridge STATIC src/Atmosphere.cpp)
add_library(nsi::systemsBridge ALIAS nsi-systems-bridge)
target_compile_features(nsi-systems-bridge PUBLIC cxx_std_20)
target_link_libraries(nsi-systems-bridge PUBLIC nsi::core StationSystems::StationSystems)
target_include_directories(nsi-systems-bridge PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
set_target_properties(nsi-systems-bridge PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(nsi-systems-bridge-consumer tests/AtmosphereConsumer.cpp)
    target_link_libraries(nsi-systems-bridge-consumer PRIVATE nsi::systemsBridge)
    add_test(NAME nsi-systems-bridge-consumer COMMAND nsi-systems-bridge-consumer)
    set_tests_properties(nsi-systems-bridge-consumer PROPERTIES
        LABELS "systems;functional;validation-fast" TIMEOUT 30)
endif()
