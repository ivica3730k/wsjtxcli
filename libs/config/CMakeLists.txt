add_library(wx_config src/config.cpp)
target_include_directories(wx_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wx_config PUBLIC wx_core PRIVATE wx_warnings)
