#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace wsjtxcli::config {

/// Everything that varies between one temporary WSJT-X instance and the next.
struct InstanceConfig {
  std::string callsign;
  std::string grid;
  std::string mode;
  std::int64_t dial_frequency = 0;
  bool psk_reporter = false;
  bool wsprnet_reporter = false;
  /// Empty means "leave WSJT-X on the system default input".
  std::string audio_device;
};

/// Renders the instance .ini. Its exact text is what WSJT-X reads back, so the
/// golden files under tests/golden pin it.
std::string render_instance_config(const InstanceConfig& instance);

/// Writes the instance .ini, creating its parent directory if needed.
void write_instance_config(const std::filesystem::path& config_path, const InstanceConfig& instance);

/// $HOME, from the environment and falling back to the password database.
std::filesystem::path home_directory();

/// An XDG base directory: the environment variable when it is set and not
/// empty, otherwise the fallback relative to $HOME. A leading ~ is expanded.
/// Linux only -- macOS has its own locations, below.
std::filesystem::path xdg_path(const std::string& env_var, const std::string& fallback);

/// Where WSJT-X reads an instance .ini from. It builds that path out of Qt's
/// ConfigLocation, so this has to be the same directory Qt would hand it:
/// $XDG_CONFIG_HOME (default ~/.config) on Linux, ~/Library/Preferences on
/// macOS, where Qt ignores the XDG variables entirely.
std::filesystem::path config_home();

/// Where WSJT-X keeps an instance's writeable data. Qt's DataLocation:
/// $XDG_DATA_HOME (default ~/.local/share) on Linux, ~/Library/Application
/// Support on macOS.
std::filesystem::path data_home();

}  // namespace wsjtxcli::config
