// Argument wiring: what reaches the launcher, and what is refused before
// anything is written to disk.

#include <string>
#include <vector>

#include "catch.hpp"
#include "wsjtxcli/cli.hpp"
#include "wsjtxcli/error.hpp"

using namespace wsjtxcli;

namespace {

std::vector<std::string> band_args() {
  return {"--callsign", "9a3ice", "--grid", "jn95", "--mode", "ft8", "--rx-band", "40m"};
}

}  // namespace

TEST_CASE("a band launch parses into canonical values", "[cli]") {
  const cli::ParseResult parsed = cli::parse_arguments(band_args());

  REQUIRE(parsed.should_run);
  REQUIRE(parsed.options.callsign == "9A3ICE");
  REQUIRE(parsed.options.grid == "JN95");
  REQUIRE(parsed.options.mode == "FT8");
  REQUIRE(parsed.options.rx_band == "40m");
  REQUIRE_FALSE(parsed.options.by_frequency);
  REQUIRE(parsed.options.audio_device.empty());
  REQUIRE_FALSE(parsed.options.psk_reporter);
  REQUIRE_FALSE(parsed.options.wsprnet_reporter);
  REQUIRE_FALSE(parsed.options.nogui);
}

TEST_CASE("a frequency launch carries the frequency instead", "[cli]") {
  const cli::ParseResult parsed =
      cli::parse_arguments({"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8", "--rx-frequency", "7100000"});

  REQUIRE(parsed.options.by_frequency);
  REQUIRE(parsed.options.rx_frequency == 7100000);
  REQUIRE(parsed.options.rx_band.empty());
}

TEST_CASE("exactly one of band and frequency is required", "[cli]") {
  std::vector<std::string> both = band_args();
  both.insert(both.end(), {"--rx-frequency", "7100000"});
  REQUIRE_THROWS_WITH(cli::parse_arguments(both), "argument --rx-frequency: not allowed with argument --rx-band");

  REQUIRE_THROWS_WITH(cli::parse_arguments({"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8"}),
                      "one of the arguments --rx-band --rx-frequency is required");
}

TEST_CASE("station identity is required", "[cli]") {
  REQUIRE_THROWS_AS(cli::parse_arguments({"--grid", "JN95", "--mode", "FT8", "--rx-band", "40m"}), UsageError);
  REQUIRE_THROWS_AS(cli::parse_arguments({"--callsign", "9A3ICE", "--mode", "FT8", "--rx-band", "40m"}), UsageError);
  REQUIRE_THROWS_AS(cli::parse_arguments({"--callsign", "9A3ICE", "--grid", "JN95", "--rx-band", "40m"}), UsageError);
  REQUIRE_THROWS_AS(cli::parse_arguments(
                        {"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8", "--rx-band", "40m", "--unknown"}),
                    UsageError);
}

TEST_CASE("each reporter is tied to the modes that accept it", "[cli]") {
  std::vector<std::string> psk_on_wspr = {"--callsign", "9A3ICE",    "--grid", "JN95",          "--mode",
                                          "wspr",       "--rx-band", "20m",    "--psk-reporter"};
  REQUIRE_THROWS_WITH(cli::parse_arguments(psk_on_wspr), "--psk-reporter is not allowed with --mode WSPR");

  std::vector<std::string> wsprnet_on_ft8 = band_args();
  wsprnet_on_ft8.emplace_back("--wsprnet-reporter");
  REQUIRE_THROWS_WITH(cli::parse_arguments(wsprnet_on_ft8), "--wsprnet-reporter is only allowed with --mode WSPR");

  const cli::ParseResult allowed = cli::parse_arguments(
      {"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "wspr", "--rx-band", "20m", "--wsprnet-reporter"});
  REQUIRE(allowed.options.wsprnet_reporter);

  std::vector<std::string> psk_on_ft8 = band_args();
  psk_on_ft8.emplace_back("--psk-reporter");
  REQUIRE(cli::parse_arguments(psk_on_ft8).options.psk_reporter);
}

TEST_CASE("the audio device is taken with its surrounding spaces removed", "[cli]") {
  std::vector<std::string> args = band_args();
  args.insert(args.end(), {"--audio-device", "  sdr_usb.monitor  "});
  REQUIRE(cli::parse_arguments(args).options.audio_device == "sdr_usb.monitor");
}

TEST_CASE("invalid values are refused before any config is written", "[cli]") {
  std::vector<std::string> bad_grid = {"--callsign", "9A3ICE", "--grid", "ZZ99", "--mode", "FT8", "--rx-band", "40m"};
  REQUIRE_THROWS_AS(cli::parse_arguments(bad_grid), UsageError);

  std::vector<std::string> bad_band = {"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8", "--rx-band", "forty"};
  REQUIRE_THROWS_AS(cli::parse_arguments(bad_band), UsageError);

  std::vector<std::string> bad_frequency = {"--callsign", "9A3ICE", "--grid",         "JN95",
                                            "--mode",     "FT8",    "--rx-frequency", "0"};
  REQUIRE_THROWS_AS(cli::parse_arguments(bad_frequency), UsageError);
}

TEST_CASE("a repeated option takes the last value, as argparse did", "[cli]") {
  const cli::ParseResult parsed =
      cli::parse_arguments({"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT4", "--rx-frequency", "14074000",
                            "--mode", "ft8", "--rx-frequency", "7074000"});

  REQUIRE(parsed.options.mode == "FT8");
  REQUIRE(parsed.options.rx_frequency == 7074000);

  // Still one tuning form: repeating one of them is not giving both.
  std::vector<std::string> both = band_args();
  both.insert(both.end(), {"--rx-band", "20m", "--rx-frequency", "7074000"});
  REQUIRE_THROWS_AS(cli::parse_arguments(both), UsageError);

  std::vector<std::string> repeated_flag = band_args();
  repeated_flag.insert(repeated_flag.end(), {"--psk-reporter", "--psk-reporter"});
  REQUIRE(cli::parse_arguments(repeated_flag).options.psk_reporter);
}
