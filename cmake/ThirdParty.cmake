# Header-only dependencies are vendored under third_party/ so a clean checkout
# builds with no network access.
add_library(wx_cli11 INTERFACE)
target_include_directories(wx_cli11 SYSTEM INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/cli11>")

add_library(wx_catch2 INTERFACE)
target_include_directories(wx_catch2 SYSTEM INTERFACE
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/catch2>")
