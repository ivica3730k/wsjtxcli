#pragma once

#include <stdexcept>
#include <string>

namespace wsjtxcli {

/// Bad command line. Reported the way argparse reports it: usage line, one
/// message on stderr, exit status 2.
class UsageError : public std::runtime_error {
 public:
  explicit UsageError(const std::string& message) : std::runtime_error(message) {}
};

/// A request that parsed but cannot be carried out -- an unknown mode/band
/// pairing, a missing wsjtx, an unwritable config. Exit status 1.
class RuntimeFailure : public std::runtime_error {
 public:
  explicit RuntimeFailure(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace wsjtxcli
