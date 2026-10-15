# Header-only: the exception types every other library throws.
add_library(wx_core INTERFACE)
target_include_directories(wx_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(wx_core INTERFACE cxx_std_17)
