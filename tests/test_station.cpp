// Argument validation and the dial frequency tables. The messages are part of
// the contract: they are what a user sees when a launch is refused.

#include <string>

#include "catch.hpp"
#include "wsjtxcli/error.hpp"
#include "wsjtxcli/station.hpp"

using namespace wsjtxcli;

TEST_CASE("callsigns are upper-cased and validated", "[station]") {
  REQUIRE(station::parse_callsign(" 9a3ice ") == "9A3ICE");
  REQUIRE(station::parse_callsign("m0abc/p") == "M0ABC/P");

  REQUIRE_THROWS_AS(station::parse_callsign(""), UsageError);
  REQUIRE_THROWS_AS(station::parse_callsign("   "), UsageError);
  REQUIRE_THROWS_AS(station::parse_callsign("9A3-ICE"), UsageError);
  REQUIRE_THROWS_WITH(station::parse_callsign("9A3 ICE"), "invalid callsign: '9A3 ICE'");
}

TEST_CASE("grids accept 4 and 6 characters only", "[station]") {
  REQUIRE(station::parse_grid("jn95") == "JN95");
  REQUIRE(station::parse_grid(" io91wm ") == "IO91WM");

  REQUIRE_THROWS_WITH(station::parse_grid("JN9"), "grid must be a 4- or 6-character Maidenhead locator");
  // The field letters stop at X and the square letters at R.
  REQUIRE_THROWS_AS(station::parse_grid("JN95YY"), UsageError);
  REQUIRE_THROWS_AS(station::parse_grid("ZZ95"), UsageError);
  REQUIRE_THROWS_AS(station::parse_grid("JN95WM12"), UsageError);
}

TEST_CASE("modes map onto the spelling WSJT-X writes to disk", "[station]") {
  REQUIRE(station::parse_mode("ft8") == "FT8");
  REQUIRE(station::parse_mode(" echo ") == "Echo");
  REQUIRE(station::parse_mode("fst4w") == "FST4W");

  REQUIRE_THROWS_WITH(
      station::parse_mode("ft6"),
      "unknown mode: 'ft6' (choices: ECHO, FST4, FST4W, FT4, FT8, JT4, JT65, JT9, MSK144, Q65, QRA64, WSPR)");
}

TEST_CASE("bands normalize to the table's spelling", "[station]") {
  REQUIRE(station::parse_band("40m") == "40m");
  REQUIRE(station::parse_band("40 M") == "40m");
  REQUIRE(station::parse_band("040m") == "40m");

  REQUIRE_THROWS_WITH(station::parse_band("40"), "band must look like '40m', '20m', etc.: '40'");
  REQUIRE_THROWS_AS(station::parse_band("m40"), UsageError);
  REQUIRE_THROWS_AS(station::parse_band("4.5m"), UsageError);
  // Digits that cannot name any band are refused here rather than later.
  REQUIRE_THROWS_AS(station::parse_band("123456789012345678901234567890m"), UsageError);
}

TEST_CASE("frequencies are bounded integers of Hz", "[station]") {
  REQUIRE(station::parse_frequency("7074000") == 7074000);
  REQUIRE(station::parse_frequency(" 1 ") == 1);
  REQUIRE(station::parse_frequency("2000000000") == 2000000000);

  REQUIRE_THROWS_WITH(station::parse_frequency("7.074e6"), "rx frequency must be an integer number of Hz: '7.074e6'");
  REQUIRE_THROWS_WITH(station::parse_frequency("0"), "rx frequency must be between 1 Hz and 2 GHz");
  REQUIRE_THROWS_WITH(station::parse_frequency("-1"), "rx frequency must be between 1 Hz and 2 GHz");
  REQUIRE_THROWS_WITH(station::parse_frequency("2000000001"), "rx frequency must be between 1 Hz and 2 GHz");
  REQUIRE_THROWS_AS(station::parse_frequency("99999999999999999999"), UsageError);
}

TEST_CASE("default dial frequencies match WSJT-X", "[station]") {
  REQUIRE(station::resolve_dial_frequency("FT8", "40m") == 7074000);
  REQUIRE(station::resolve_dial_frequency("FT8", "2m") == 144174000);
  REQUIRE(station::resolve_dial_frequency("FT4", "20m") == 14080000);
  REQUIRE(station::resolve_dial_frequency("WSPR", "20m") == 14095600);
  REQUIRE(station::resolve_dial_frequency("JT65", "10m") == 28076000);
  REQUIRE(station::resolve_dial_frequency("JT9", "30m") == 10140000);

  const auto& tables = station::default_dial_freqs();
  REQUIRE(tables.size() == 5);
  REQUIRE(tables.at("FT8").size() == 12);
  REQUIRE(tables.at("FT4").size() == 9);
  REQUIRE(tables.at("WSPR").size() == 12);
  REQUIRE(tables.at("JT65").size() == 9);
  REQUIRE(tables.at("JT9").size() == 9);
}

TEST_CASE("a mode or band with no default says so", "[station]") {
  REQUIRE_THROWS_WITH(station::resolve_dial_frequency("Q65", "40m"),
                      "no default dial frequencies known for mode Q65; pass --rx-frequency explicitly");

  // The known bands are listed in table order, lowest frequency first.
  REQUIRE_THROWS_WITH(station::resolve_dial_frequency("FT4", "160m"),
                      "no default FT4 dial frequency for band 160m "
                      "(known: 80m, 40m, 30m, 20m, 17m, 15m, 12m, 10m, 6m); pass --rx-frequency explicitly");
}
