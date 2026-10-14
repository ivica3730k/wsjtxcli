add_library(wx_process src/process.cpp)
target_include_directories(wx_process PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wx_process PUBLIC wx_core PRIVATE wx_warnings)
