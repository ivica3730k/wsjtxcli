# Third-party dependencies, pinned by tag and built as part of the project.
#
# A system package is preferred where one is installed, so a distribution can
# build this against the Catch2 and CLI11 it already ships; otherwise CMake
# fetches the pinned version. An offline build can point at a local checkout
# with -DFETCHCONTENT_SOURCE_DIR_CLI11=/path (and the same for catch2).
include(FetchContent)

set(WSJTXCLI_CLI11_TAG "v2.4.2" CACHE STRING "CLI11 version the command line is built against")
set(WSJTXCLI_CATCH2_TAG "v2.13.10" CACHE STRING "Catch2 version the test suite is built against")

# Marks a dependency's headers as system headers. Targets that come from
# add_subdirectory are not imported, so their include directories would
# otherwise be compiled with this project's warning flags, and a warning in
# somebody else's header is not something this build can fix.
function(wsjtxcli_mark_system target)
  if(NOT TARGET ${target})
    return()
  endif()
  get_target_property(include_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
  if(include_dirs)
    set_property(TARGET ${target} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${include_dirs}")
  endif()
endfunction()

find_package(CLI11 2.4 QUIET)
if(NOT CLI11_FOUND)
  set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG ${WSJTXCLI_CLI11_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(cli11)
  wsjtxcli_mark_system(CLI11)
endif()

# One name for the rest of the build, whether the headers came from the system
# or from the fetch above.
add_library(wx_cli11 INTERFACE)
target_link_libraries(wx_cli11 INTERFACE CLI11::CLI11)

if(WSJTXCLI_BUILD_TESTS)
  find_package(Catch2 2 QUIET)
  if(NOT Catch2_FOUND)
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_HELPERS OFF CACHE BOOL "" FORCE)

    # Catch2 2.13 still declares cmake_minimum_required(VERSION 3.5), which
    # newer CMake refuses outright. Granting it the floor is the documented
    # escape hatch and affects only the fetched subproject.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    FetchContent_Declare(catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG ${WSJTXCLI_CATCH2_TAG}
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(catch2)

    unset(CMAKE_POLICY_VERSION_MINIMUM)
    wsjtxcli_mark_system(Catch2)
  endif()

  add_library(wx_catch2 INTERFACE)
  target_link_libraries(wx_catch2 INTERFACE Catch2::Catch2)
endif()
