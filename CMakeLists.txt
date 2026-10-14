cmake_minimum_required(VERSION 3.16)
project(wsjtxcli VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(WSJTXCLI_BUILD_TESTS "Build the Catch2 test suite" ON)
option(WSJTXCLI_STATIC_RUNTIME "Link libstdc++/libgcc statically (portable Linux builds)" OFF)

# What `--version` reports. Release builds pass the tag; everything else stays
# 0.0.0, matching the Python implementation this replaces, which kept 0.0.0 in
# git and set the real version from the tag at publish time.
set(WSJTXCLI_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string reported by the binary")

# Static libstdc++/libgcc let one Linux binary run across distributions without
# matching the build machine's GCC runtime.
add_library(wx_runtime INTERFACE)
if(WSJTXCLI_STATIC_RUNTIME)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_options(wx_runtime INTERFACE -static-libstdc++ -static-libgcc)
  else()
    message(STATUS "WSJTXCLI_STATIC_RUNTIME ignored: not supported for this compiler/platform")
  endif()
endif()

# Binaries land in a single gitignored directory so CI can collect them without
# knowing the generator's layout.
set(WSJTXCLI_BIN_DIR "${PROJECT_SOURCE_DIR}/binaries" CACHE PATH "Where built binaries are placed")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WSJTXCLI_BIN_DIR}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${WSJTXCLI_BIN_DIR}")
endforeach()

include(cmake/Warnings.cmake)
include(cmake/ThirdParty.cmake)

add_subdirectory(libs/core)
add_subdirectory(libs/station)
add_subdirectory(libs/config)
add_subdirectory(libs/process)
add_subdirectory(libs/cli)
add_subdirectory(apps/wsjtxcli)

if(WSJTXCLI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "wsjtxcli ${WSJTXCLI_VERSION}")
message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests         : ${WSJTXCLI_BUILD_TESTS}")
message(STATUS "  static runtime: ${WSJTXCLI_STATIC_RUNTIME}")
message(STATUS "  binaries ->    ${WSJTXCLI_BIN_DIR}")
