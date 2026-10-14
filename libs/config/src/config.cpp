#include "wsjtxcli/config.hpp"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "wsjtxcli/error.hpp"

namespace wsjtxcli::config {
namespace {

const char* boolstr(bool flag) { return flag ? "true" : "false"; }

}  // namespace

std::string render_instance_config(const InstanceConfig& instance) {
  std::vector<std::string> lines = {
      "# WSJT-X station identity settings.",
      "[Configuration]",
      "# The callsign this temporary WSJT-X instance should use.",
      "MyCall=" + instance.callsign,
      "# The Maidenhead grid locator this temporary WSJT-X instance should use.",
      "MyGrid=" + instance.grid,
      "# Whether to report decodes to PSK Reporter.",
      std::string("PSKReporter=") + boolstr(instance.psk_reporter),
      "# Receive-only lock: no rig means no CAT/RTS/DTR keying path exists.",
      "Rig=None",
      "# Backstop: force any transmission to stop after one minute.",
      "TxWatchdog=1",
  };

  if (!instance.audio_device.empty()) {
    lines.emplace_back("# Audio input device this instance should capture from.");
    lines.emplace_back("SoundInName=" + instance.audio_device);
  }

  lines.insert(lines.end(), {
                                "# WSJT-X operating settings.",
                                "[Common]",
                                "# Whether to upload WSPR spots to wsprnet.org.",
                                std::string("UploadSpots=") + boolstr(instance.wsprnet_reporter),
                                "# The operating mode this instance should start in.",
                                "Mode=" + instance.mode,
                                "# The RF dial frequency (Hz) this instance should tune to.",
                                "DialFreq=" + std::to_string(instance.dial_frequency),
                                "# WSJT-X multi-instance behavior settings.",
                                "[MultiSettings]",
                                "# Keep the current settings profile on the default name.",
                                "CurrentName=Default",
                                "# Disable the startup splash and release-notes prompt.",
                                "Splash_v1.7=false",
                            });

  std::ostringstream rendered;
  for (const std::string& line : lines) {
    rendered << line << "\n";
  }
  return rendered.str();
}

void write_instance_config(const std::filesystem::path& config_path, const InstanceConfig& instance) {
  const std::filesystem::path parent = config_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec && !std::filesystem::is_directory(parent)) {
      throw RuntimeFailure("cannot create " + parent.string() + ": " + ec.message());
    }
  }

  std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw RuntimeFailure("cannot write " + config_path.string());
  }
  out << render_instance_config(instance);
  out.close();
  if (!out) {
    throw RuntimeFailure("cannot write " + config_path.string());
  }
}

std::filesystem::path home_directory() {
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home);
  }
  // No HOME at all: a login shell is not guaranteed, for instance under a
  // service manager, so the password database is the fallback.
  if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr) {
    return std::filesystem::path(entry->pw_dir);
  }
  throw RuntimeFailure("cannot determine the home directory");
}

std::filesystem::path xdg_path(const std::string& env_var, const std::string& fallback) {
  const char* raw_value = std::getenv(env_var.c_str());
  if (raw_value == nullptr || *raw_value == '\0') {
    return home_directory() / fallback;
  }

  std::string value(raw_value);
  if (value == "~") {
    return home_directory();
  }
  if (value.rfind("~/", 0) == 0) {
    return home_directory() / value.substr(2);
  }
  return std::filesystem::path(value);
}

}  // namespace wsjtxcli::config
