cmake_minimum_required(VERSION 3.24)
project(NuminaSpaceIndustries VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)

# Supported builds provide these two configurations only. Single-configuration
# generators use Release by default.
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available build configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "numina-space-industries only supports x64 (64-bit) builds")
endif()

add_subdirectory(station-systems)
add_subdirectory(editor)
