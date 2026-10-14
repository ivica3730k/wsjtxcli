#include "wsjtxcli/cli.hpp"

#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "CLI11.hpp"
#include "wsjtxcli/config.hpp"
#include "wsjtxcli/error.hpp"
#include "wsjtxcli/process.hpp"
#include "wsjtxcli/station.hpp"

namespace wsjtxcli::cli {
namespace {

// Virtual display geometry used for headless (--nogui) operation.
constexpr const char* kNoguiScreen = "1024x768x24";

/// The directory temporary files go in. Only TMPDIR is honoured, the one
/// variable POSIX defines for it.
std::filesystem::path temp_directory() {
  const char* tmpdir = std::getenv("TMPDIR");
  if (tmpdir != nullptr && *tmpdir != '\0') {
    return std::filesystem::path(tmpdir);
  }
  return std::filesystem::path("/tmp");
}

/// A unique rig name, held by an open temporary file for as long as the
/// instance lives. Two concurrent runs cannot be handed the same name, which
/// is what keeps their WSJT-X configs from colliding.
class RigNameReservation {
 public:
  RigNameReservation() {
    std::string pattern = (temp_directory() / "wsjtx-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    fd_ = mkstemp(buffer.data());
    if (fd_ < 0) {
      throw RuntimeFailure("cannot create a temporary file in " + temp_directory().string());
    }
    path_ = std::string(buffer.data());
    name_ = std::filesystem::path(path_).filename().string();
  }

  ~RigNameReservation() {
    if (fd_ >= 0) {
      close(fd_);
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  RigNameReservation(const RigNameReservation&) = delete;
  RigNameReservation& operator=(const RigNameReservation&) = delete;

  const std::string& name() const { return name_; }

 private:
  int fd_ = -1;
  std::string path_;
  std::string name_;
};

/// Removes the instance config and data directory however the run ends --
/// normal exit, Ctrl-C, or an exception on the way to starting WSJT-X.
class InstanceCleanup {
 public:
  InstanceCleanup(std::filesystem::path config_path, std::filesystem::path data_path)
      : config_path_(std::move(config_path)), data_path_(std::move(data_path)) {}

  ~InstanceCleanup() {
    std::error_code ec;
    std::filesystem::remove(config_path_, ec);
    std::filesystem::remove_all(data_path_, ec);
  }

  InstanceCleanup(const InstanceCleanup&) = delete;
  InstanceCleanup& operator=(const InstanceCleanup&) = delete;

 private:
  std::filesystem::path config_path_;
  std::filesystem::path data_path_;
};

/// Reports a rejected value the way argparse did, naming the flag it came
/// from: "argument --grid: grid must be a 4- or 6-character Maidenhead ...".
template <typename Parser>
auto validated(const char* flag, Parser parse, const std::string& value) -> decltype(parse(value)) {
  try {
    return parse(value);
  } catch (const UsageError& e) {
    throw UsageError(std::string("argument ") + flag + ": " + e.what());
  }
}

}  // namespace

ParseResult parse_arguments(const std::vector<std::string>& args) {
  CLI::App app{"Launch WSJT-X with a temporary per-instance rig name.", "wsjtxcli"};
  app.set_version_flag("--version", std::string(WSJTXCLI_VERSION));

  std::string callsign;
  std::string grid;
  std::string mode;
  std::string rx_band;
  std::string rx_frequency;
  std::string audio_device;
  bool psk_reporter = false;
  bool wsprnet_reporter = false;
  bool nogui = false;

  app.add_option("--callsign", callsign, "Station callsign.")->required();
  app.add_option("--grid", grid, "4- or 6-character Maidenhead locator.")->required();
  app.add_option("--mode", mode, "Operating mode (FT8, FT4, WSPR, ...).")->required();
  const CLI::Option* band_opt =
      app.add_option("--rx-band", rx_band, "Band like 40m; resolves to the mode's default dial frequency.");
  const CLI::Option* freq_opt = app.add_option("--rx-frequency", rx_frequency, "Explicit RF dial frequency in Hz.");
  app.add_option("--audio-device", audio_device, "Capture source name. Omit for the system default.");
  app.add_flag("--psk-reporter", psk_reporter, "Upload decodes to PSK Reporter.");
  app.add_flag("--wsprnet-reporter", wsprnet_reporter, "Upload WSPR spots to wsprnet.org.");
  app.add_flag("--nogui", nogui, "run headless on a virtual X display via xvfb-run");

  // CLI11 parses from argc/argv; the vector overload expects its arguments
  // reversed, which is a trap not worth leaving in the code.
  std::vector<const char*> argv;
  argv.reserve(args.size() + 1);
  argv.push_back("wsjtxcli");
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }

  try {
    app.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const CLI::CallForHelp& e) {
    return ParseResult{false, app.exit(e), Options{}};
  } catch (const CLI::CallForVersion& e) {
    return ParseResult{false, app.exit(e), Options{}};
  } catch (const CLI::ParseError& e) {
    throw UsageError(e.what());
  }

  Options options;
  options.callsign = validated("--callsign", station::parse_callsign, callsign);
  options.grid = validated("--grid", station::parse_grid, grid);
  options.mode = validated("--mode", station::parse_mode, mode);

  if (band_opt->count() > 0 && freq_opt->count() > 0) {
    throw UsageError("argument --rx-frequency: not allowed with argument --rx-band");
  }
  if (band_opt->count() == 0 && freq_opt->count() == 0) {
    throw UsageError("one of the arguments --rx-band --rx-frequency is required");
  }
  if (freq_opt->count() > 0) {
    options.by_frequency = true;
    options.rx_frequency = validated("--rx-frequency", station::parse_frequency, rx_frequency);
  } else {
    options.rx_band = validated("--rx-band", station::parse_band, rx_band);
  }

  // Both reporters target a service that only accepts one kind of traffic, so
  // the mode decides which of them can be on at all.
  if (psk_reporter && options.mode == "WSPR") {
    throw UsageError("--psk-reporter is not allowed with --mode WSPR");
  }
  if (wsprnet_reporter && options.mode != "WSPR") {
    throw UsageError("--wsprnet-reporter is only allowed with --mode WSPR");
  }

  // argparse's type hook stripped this one too, so " default " and "default"
  // have always named the same device.
  options.audio_device = audio_device;
  while (!options.audio_device.empty() && std::isspace(static_cast<unsigned char>(options.audio_device.front()))) {
    options.audio_device.erase(options.audio_device.begin());
  }
  while (!options.audio_device.empty() && std::isspace(static_cast<unsigned char>(options.audio_device.back()))) {
    options.audio_device.pop_back();
  }

  options.psk_reporter = psk_reporter;
  options.wsprnet_reporter = wsprnet_reporter;
  options.nogui = nogui;

  return ParseResult{true, 0, options};
}

int launch(const Options& options) {
  const std::int64_t dial_frequency =
      options.by_frequency ? options.rx_frequency : station::resolve_dial_frequency(options.mode, options.rx_band);

  const std::optional<std::string> wsjtx_bin = process::which("wsjtx");
  if (!wsjtx_bin.has_value()) {
    std::cerr << "wsjtx binary not found in PATH" << std::endl;
    return 1;
  }

  std::optional<std::string> xvfb_run_bin;
  if (options.nogui) {
    xvfb_run_bin = process::which("xvfb-run");
    if (!xvfb_run_bin.has_value()) {
      std::cerr << "xvfb-run not found in PATH (install xvfb)" << std::endl;
      return 1;
    }
  }

  const std::filesystem::path config_home = config::xdg_path("XDG_CONFIG_HOME", ".config");
  const std::filesystem::path data_home = config::xdg_path("XDG_DATA_HOME", ".local/share");

  const RigNameReservation rig;
  const std::filesystem::path instance_config_path = config_home / ("WSJT-X - " + rig.name() + ".ini");
  const std::filesystem::path instance_data_path = data_home / ("WSJT-X - " + rig.name());
  const InstanceCleanup cleanup(instance_config_path, instance_data_path);

  config::InstanceConfig instance;
  instance.callsign = options.callsign;
  instance.grid = options.grid;
  instance.mode = options.mode;
  instance.dial_frequency = dial_frequency;
  instance.psk_reporter = options.psk_reporter;
  instance.wsprnet_reporter = options.wsprnet_reporter;
  instance.audio_device = options.audio_device;
  config::write_instance_config(instance_config_path, instance);

  std::vector<std::string> command;
  if (xvfb_run_bin.has_value()) {
    command = {*xvfb_run_bin, "-a", "-s", std::string("-screen 0 ") + kNoguiScreen};
  }
  command.push_back(*wsjtx_bin);
  command.push_back("--rig-name=" + rig.name());

  process::Environment env = process::sanitize_wsjtx_environment(process::current_environment(), *wsjtx_bin);
  if (options.nogui) {
    // Xvfb provides an X11 display; do not let Qt select Wayland.
    env["QT_QPA_PLATFORM"] = "xcb";
    env.erase("WAYLAND_DISPLAY");
  }

  return process::run(command, env);
}

int run(const std::vector<std::string>& args) {
  try {
    const ParseResult parsed = parse_arguments(args);
    if (!parsed.should_run) {
      return parsed.exit_code;
    }
    return launch(parsed.options);
  } catch (const UsageError& e) {
    std::cerr << "wsjtxcli: error: " << e.what() << std::endl;
    std::cerr << "Run with --help for more information." << std::endl;
    return 2;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}

}  // namespace wsjtxcli::cli
