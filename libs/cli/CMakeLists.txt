add_library(wx_cli src/cli.cpp)
target_include_directories(wx_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wx_cli PUBLIC wx_core wx_station wx_config wx_process PRIVATE wx_cli11 wx_warnings)
target_compile_definitions(wx_cli PRIVATE WSJTXCLI_VERSION="${WSJTXCLI_VERSION}")
