#include "wsjtxcli/station.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "wsjtxcli/error.hpp"

namespace wsjtxcli::station {
namespace {

const std::regex& callsign_re() {
  static const std::regex re("[A-Z0-9/]+");
  return re;
}

const std::regex& grid_re() {
  static const std::regex re("[A-R]{2}[0-9]{2}(?:[A-X]{2})?");
  return re;
}

const std::regex& band_re() {
  static const std::regex re("([0-9]+)[ \t\n\r\f\v]*M");
  return re;
}

/// Python's str.strip() with no argument: ASCII whitespace from both ends.
std::string strip(const std::string& value) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  auto end = std::find_if_not(value.rbegin(), std::string::const_reverse_iterator(begin), is_space).base();
  return std::string(begin, end);
}

std::string upper(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return value;
}

/// Python's {value!r} for the plain strings that reach these messages.
std::string repr(const std::string& value) { return "'" + value + "'"; }

}  // namespace

const std::map<std::string, std::string>& mode_canonical() {
  static const std::map<std::string, std::string> table = {
      {"FT4", "FT4"},   {"FT8", "FT8"},     {"JT4", "JT4"},       {"JT9", "JT9"},
      {"JT65", "JT65"}, {"QRA64", "QRA64"}, {"MSK144", "MSK144"}, {"WSPR", "WSPR"},
      {"ECHO", "Echo"}, {"FST4", "FST4"},   {"FST4W", "FST4W"},   {"Q65", "Q65"},
  };
  return table;
}

const std::map<std::string, BandTable>& default_dial_freqs() {
  static const std::map<std::string, BandTable> table = {
      {"FT8",
       {{"160m", 1840000},
        {"80m", 3573000},
        {"60m", 5357000},
        {"40m", 7074000},
        {"30m", 10136000},
        {"20m", 14074000},
        {"17m", 18100000},
        {"15m", 21074000},
        {"12m", 24915000},
        {"10m", 28074000},
        {"6m", 50313000},
        {"2m", 144174000}}},
      {"FT4",
       {{"80m", 3575000},
        {"40m", 7047500},
        {"30m", 10140000},
        {"20m", 14080000},
        {"17m", 18104000},
        {"15m", 21140000},
        {"12m", 24919000},
        {"10m", 28180000},
        {"6m", 50318000}}},
      {"WSPR",
       {{"160m", 1836600},
        {"80m", 3568600},
        {"60m", 5364700},
        {"40m", 7038600},
        {"30m", 10138700},
        {"20m", 14095600},
        {"17m", 18104600},
        {"15m", 21094600},
        {"12m", 24924600},
        {"10m", 28124600},
        {"6m", 50293000},
        {"2m", 144489000}}},
      {"JT65",
       {{"160m", 1838000},
        {"80m", 3570000},
        {"40m", 7076000},
        {"30m", 10138000},
        {"20m", 14076000},
        {"17m", 18102000},
        {"15m", 21076000},
        {"12m", 24917000},
        {"10m", 28076000}}},
      {"JT9",
       {{"160m", 1838000},
        {"80m", 3572000},
        {"40m", 7078000},
        {"30m", 10140000},
        {"20m", 14078000},
        {"17m", 18104000},
        {"15m", 21078000},
        {"12m", 24919000},
        {"10m", 28078000}}},
  };
  return table;
}

std::string parse_callsign(const std::string& value) {
  const std::string callsign = upper(strip(value));
  if (callsign.empty() || !std::regex_match(callsign, callsign_re())) {
    throw UsageError("invalid callsign: " + repr(value));
  }
  return callsign;
}

std::string parse_grid(const std::string& value) {
  const std::string grid = upper(strip(value));
  if (!std::regex_match(grid, grid_re())) {
    throw UsageError("grid must be a 4- or 6-character Maidenhead locator");
  }
  return grid;
}

std::string parse_mode(const std::string& value) {
  const auto& table = mode_canonical();
  const auto found = table.find(upper(strip(value)));
  if (found == table.end()) {
    std::ostringstream choices;
    for (auto it = table.begin(); it != table.end(); ++it) {
      if (it != table.begin()) {
        choices << ", ";
      }
      choices << it->first;
    }
    throw UsageError("unknown mode: " + repr(value) + " (choices: " + choices.str() + ")");
  }
  return found->second;
}

std::string parse_band(const std::string& value) {
  std::smatch match;
  const std::string candidate = upper(strip(value));
  if (!std::regex_match(candidate, match, band_re())) {
    throw UsageError("band must look like '40m', '20m', etc.: " + repr(value));
  }

  // The digits are re-emitted as a number so "040m" and "40m" name the same
  // band. A run of digits too long for the conversion cannot match any band
  // table, so it is rejected here rather than later as an unknown band.
  errno = 0;
  char* end = nullptr;
  const unsigned long long metres = std::strtoull(match[1].str().c_str(), &end, 10);
  if (errno != 0 || metres > 99999) {
    throw UsageError("band must look like '40m', '20m', etc.: " + repr(value));
  }
  return std::to_string(metres) + "m";
}

std::int64_t parse_frequency(const std::string& value) {
  const std::string candidate = strip(value);
  const bool signed_digits =
      !candidate.empty() &&
      (std::all_of(candidate.begin(), candidate.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ||
       ((candidate[0] == '+' || candidate[0] == '-') && candidate.size() > 1 &&
        std::all_of(candidate.begin() + 1, candidate.end(), [](unsigned char c) { return std::isdigit(c) != 0; })));
  if (!signed_digits) {
    throw UsageError("rx frequency must be an integer number of Hz: " + repr(value));
  }

  errno = 0;
  char* end = nullptr;
  const long long frequency = std::strtoll(candidate.c_str(), &end, 10);
  // Out of range for the conversion is out of the accepted band either way;
  // the bound below is what the user is told about.
  if (errno != 0 || frequency <= 0 || frequency > 2000000000LL) {
    throw UsageError("rx frequency must be between 1 Hz and 2 GHz");
  }
  return static_cast<std::int64_t>(frequency);
}

std::string parse_audio_device(const std::string& value) { return strip(value); }

std::int64_t resolve_dial_frequency(const std::string& mode, const std::string& band) {
  const auto& table = default_dial_freqs();
  const auto by_mode = table.find(mode);
  if (by_mode == table.end()) {
    throw RuntimeFailure("no default dial frequencies known for mode " + mode + "; pass --rx-frequency explicitly");
  }

  const BandTable& bands = by_mode->second;
  const auto by_band =
      std::find_if(bands.begin(), bands.end(), [&band](const auto& entry) { return entry.first == band; });
  if (by_band == bands.end()) {
    std::ostringstream known;
    for (auto it = bands.begin(); it != bands.end(); ++it) {
      if (it != bands.begin()) {
        known << ", ";
      }
      known << it->first;
    }
    throw RuntimeFailure("no default " + mode + " dial frequency for band " + band + " (known: " + known.str() +
                         "); pass --rx-frequency explicitly");
  }
  return by_band->second;
}

}  // namespace wsjtxcli::station
