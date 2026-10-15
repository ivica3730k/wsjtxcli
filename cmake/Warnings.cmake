# One interface target carrying the project's warning policy; every library
# links it so the flags cannot drift between targets.
add_library(wx_warnings INTERFACE)

target_compile_options(wx_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
