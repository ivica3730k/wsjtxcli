#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

/// A temporary directory that removes itself, so a failing assertion cannot
/// leave scratch trees behind.
class ScratchDir {
 public:
  explicit ScratchDir(const std::string& prefix) {
    const char* tmpdir = std::getenv("TMPDIR");
    const std::filesystem::path root =
        (tmpdir != nullptr && *tmpdir != '\0') ? std::filesystem::path(tmpdir) : std::filesystem::path("/tmp");
    std::string pattern = (root / (prefix + "-XXXXXX")).string();
    if (mkdtemp(pattern.data()) == nullptr) {
      throw std::runtime_error("cannot create a scratch directory");
    }
    path_ = pattern;
  }

  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

/// Sets an environment variable for the duration of a scope and puts the
/// previous value (or its absence) back afterwards. A null value unsets it.
class EnvVar {
 public:
  EnvVar(const char* name, const char* value) : name_(name) {
    const char* previous = std::getenv(name);
    had_previous_ = previous != nullptr;
    if (had_previous_) {
      previous_ = previous;
    }
    apply(value);
  }

  ~EnvVar() { apply(had_previous_ ? previous_.c_str() : nullptr); }

  EnvVar(const EnvVar&) = delete;
  EnvVar& operator=(const EnvVar&) = delete;

 private:
  void apply(const char* value) const {
    if (value == nullptr) {
      unsetenv(name_.c_str());
    } else {
      setenv(name_.c_str(), value, 1);
    }
  }

  std::string name_;
  std::string previous_;
  bool had_previous_ = false;
};
