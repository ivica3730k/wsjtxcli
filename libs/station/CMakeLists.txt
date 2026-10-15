add_library(wx_station src/station.cpp)
target_include_directories(wx_station PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wx_station PUBLIC wx_core PRIVATE wx_warnings)
