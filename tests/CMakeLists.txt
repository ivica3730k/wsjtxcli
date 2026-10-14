# One translation unit provides Catch2's main; the rest are plain test files.
add_executable(wsjtxcli-tests
  main.cpp
  test_cli.cpp
  test_config.cpp
  test_launch.cpp
  test_process.cpp
  test_station.cpp)

target_include_directories(wsjtxcli-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wsjtxcli-tests PRIVATE wx_cli wx_catch2 wx_runtime wx_warnings)

# The golden configs came out of the Python implementation; the launch tests
# drive the binary that ships rather than the library behind it.
target_compile_definitions(wsjtxcli-tests PRIVATE
  WSJTXCLI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  WSJTXCLI_BINARY="$<TARGET_FILE:wsjtxcli>"
  WSJTXCLI_VERSION="${WSJTXCLI_VERSION}")
add_dependencies(wsjtxcli-tests wsjtxcli)

add_test(NAME wsjtxcli-tests COMMAND wsjtxcli-tests)
set_tests_properties(wsjtxcli-tests PROPERTIES TIMEOUT 300)
