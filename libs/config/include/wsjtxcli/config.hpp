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
std::filesystem::path xdg_path(const std::string& env_var, const std::string& fallback);

}  // namespace wsjtxcli::config
