#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wsjtxcli::station {

/// Default RF dial frequency (Hz) per band for one mode. A vector rather than
/// a map because the "known bands" error message lists them in table order,
/// the way the Python dict did; sorting them would change what users read.
using BandTable = std::vector<std::pair<std::string, std::int64_t>>;

/// Normalized (uppercased) mode input -> the value WSJT-X writes to disk.
const std::map<std::string, std::string>& mode_canonical();

/// Canonical mode -> its per-band dial frequencies, mirroring WSJT-X defaults.
const std::map<std::string, BandTable>& default_dial_freqs();

/// Upper-cases and validates a callsign. Throws UsageError if it is not one.
std::string parse_callsign(const std::string& value);

/// Upper-cases and validates a 4- or 6-character Maidenhead locator.
std::string parse_grid(const std::string& value);

/// Maps free-form mode input onto the canonical WSJT-X spelling.
std::string parse_mode(const std::string& value);

/// Normalizes a band token such as "40 M" or "040m" to "40m".
std::string parse_band(const std::string& value);

/// Parses an RF dial frequency in Hz, bounded to 1 Hz .. 2 GHz.
std::int64_t parse_frequency(const std::string& value);

/// Takes a capture source name with its surrounding spaces removed. Empty
/// means "leave WSJT-X on the system default input".
std::string parse_audio_device(const std::string& value);

/// Looks up the default dial frequency for a mode/band pairing.
/// Throws RuntimeFailure when the pairing has no default.
std::int64_t resolve_dial_frequency(const std::string& mode, const std::string& band);

}  // namespace wsjtxcli::station
