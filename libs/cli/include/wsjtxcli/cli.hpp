#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wsjtxcli::cli {

/// The command line, validated. Exactly one of rx_band / rx_frequency is set.
struct Options {
  std::string callsign;
  std::string grid;
  std::string mode;
  std::string rx_band;
  std::int64_t rx_frequency = 0;
  bool by_frequency = false;
  std::string audio_device;
  bool psk_reporter = false;
  bool wsprnet_reporter = false;
  bool nogui = false;
};

/// Outcome of parsing: either options to act on, or an exit status for a run
/// that is already finished -- --help and --version print and stop.
struct ParseResult {
  bool should_run = false;
  int exit_code = 0;
  Options options;
};

/// Parses and validates arguments (argv without the program name).
/// Throws UsageError for anything argparse would have rejected.
ParseResult parse_arguments(const std::vector<std::string>& args);

/// Launches WSJT-X against a temporary instance config and waits for it.
int launch(const Options& options);

/// The whole program: parse, report usage errors, launch.
int run(const std::vector<std::string>& args);

}  // namespace wsjtxcli::cli
