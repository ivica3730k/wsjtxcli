// The instance .ini. WSJT-X reads this back, so the golden files under
// tests/golden -- produced by the Python implementation this port replaces --
// are what pin the format.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "catch.hpp"
#include "support.hpp"
#include "wsjtxcli/config.hpp"

using namespace wsjtxcli;

namespace {

std::string read_golden(const std::string& name) {
  const std::filesystem::path path = std::filesystem::path(WSJTXCLI_GOLDEN_DIR) / name;
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

}  // namespace

TEST_CASE("rendered configs match the reference implementation byte for byte", "[config]") {
  SECTION("band-tuned FT8 with no reporting and the default audio input") {
    config::InstanceConfig instance;
    instance.callsign = "9A3ICE";
    instance.grid = "JN95";
    instance.mode = "FT8";
    instance.dial_frequency = 7074000;
    REQUIRE(config::render_instance_config(instance) == read_golden("ft8_band_plain.ini"));
  }

  SECTION("WSPR uploading spots from a named capture source") {
    config::InstanceConfig instance;
    instance.callsign = "M0ABC/P";
    instance.grid = "IO91WM";
    instance.mode = "WSPR";
    instance.dial_frequency = 14095600;
    instance.wsprnet_reporter = true;
    instance.audio_device = "sdr_usb.monitor";
    REQUIRE(config::render_instance_config(instance) == read_golden("wspr_wsprnet_audio.ini"));
  }

  SECTION("FT4 on an explicit frequency reporting to PSK Reporter") {
    config::InstanceConfig instance;
    instance.callsign = "K1ABC";
    instance.grid = "FN42";
    instance.mode = "FT4";
    instance.dial_frequency = 7100000;
    instance.psk_reporter = true;
    instance.audio_device = "default";
    REQUIRE(config::render_instance_config(instance) == read_golden("ft4_psk_explicit.ini"));
  }

  SECTION("a mode whose canonical spelling is not all upper case") {
    config::InstanceConfig instance;
    instance.callsign = "VK2XYZ";
    instance.grid = "QF56";
    instance.mode = "Echo";
    instance.dial_frequency = 144174000;
    REQUIRE(config::render_instance_config(instance) == read_golden("echo_plain.ini"));
  }
}

TEST_CASE("an empty audio device leaves the input line out", "[config]") {
  config::InstanceConfig instance;
  instance.callsign = "9A3ICE";
  instance.grid = "JN95";
  instance.mode = "FT8";
  instance.dial_frequency = 7074000;

  const std::string rendered = config::render_instance_config(instance);
  REQUIRE(rendered.find("SoundInName") == std::string::npos);

  instance.audio_device = "sdr_usb.monitor";
  REQUIRE(config::render_instance_config(instance).find("SoundInName=sdr_usb.monitor\n") != std::string::npos);
}

TEST_CASE("writing creates the directory the config lives in", "[config]") {
  const ScratchDir scratch("wsjtxcli-config");
  const std::filesystem::path target = scratch.path() / "nested" / "WSJT-X - wsjtx-abc123.ini";

  config::InstanceConfig instance;
  instance.callsign = "9A3ICE";
  instance.grid = "JN95";
  instance.mode = "FT8";
  instance.dial_frequency = 7074000;
  config::write_instance_config(target, instance);

  REQUIRE(std::filesystem::exists(target));
  std::ifstream input(target, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  REQUIRE(contents.str() == config::render_instance_config(instance));
}

TEST_CASE("XDG paths follow the environment and fall back to $HOME", "[config]") {
  const EnvVar home("HOME", "/home/operator");

  SECTION("unset falls back") {
    const EnvVar config_home("XDG_CONFIG_HOME", nullptr);
    REQUIRE(config::xdg_path("XDG_CONFIG_HOME", ".config") == std::filesystem::path("/home/operator/.config"));
  }

  SECTION("empty falls back as well") {
    const EnvVar config_home("XDG_CONFIG_HOME", "");
    REQUIRE(config::xdg_path("XDG_CONFIG_HOME", ".config") == std::filesystem::path("/home/operator/.config"));
  }

  SECTION("an absolute value is taken as given") {
    const EnvVar config_home("XDG_CONFIG_HOME", "/srv/state/config");
    REQUIRE(config::xdg_path("XDG_CONFIG_HOME", ".config") == std::filesystem::path("/srv/state/config"));
  }

  SECTION("a leading tilde expands") {
    const EnvVar data_home("XDG_DATA_HOME", "~/state");
    REQUIRE(config::xdg_path("XDG_DATA_HOME", ".local/share") == std::filesystem::path("/home/operator/state"));
  }
}

TEST_CASE("the instance directories are the ones WSJT-X reads", "[config]") {
  const EnvVar home("HOME", "/home/operator");

#ifdef __APPLE__
  // Qt resolves its ConfigLocation and DataLocation from Cocoa on macOS and
  // never looks at the XDG variables, so neither does wsjtxcli.
  const EnvVar config_override("XDG_CONFIG_HOME", "/srv/ignored");
  const EnvVar data_override("XDG_DATA_HOME", "/srv/ignored");

  REQUIRE(config::config_home() == std::filesystem::path("/home/operator/Library/Preferences"));
  REQUIRE(config::data_home() == std::filesystem::path("/home/operator/Library/Application Support"));
#else
  SECTION("the XDG variables decide") {
    const EnvVar config_override("XDG_CONFIG_HOME", "/srv/state/config");
    const EnvVar data_override("XDG_DATA_HOME", "/srv/state/data");

    REQUIRE(config::config_home() == std::filesystem::path("/srv/state/config"));
    REQUIRE(config::data_home() == std::filesystem::path("/srv/state/data"));
  }

  SECTION("and fall back to the XDG defaults") {
    const EnvVar config_override("XDG_CONFIG_HOME", nullptr);
    const EnvVar data_override("XDG_DATA_HOME", nullptr);

    REQUIRE(config::config_home() == std::filesystem::path("/home/operator/.config"));
    REQUIRE(config::data_home() == std::filesystem::path("/home/operator/.local/share"));
  }
#endif
}
