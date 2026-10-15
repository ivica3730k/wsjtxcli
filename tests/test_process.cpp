// Environment scrubbing and executable lookup. The scrubbing exists because a
// snap-polluted environment makes /usr/bin/wsjtx fail its symbol lookups, so
// what it keeps matters as much as what it drops.

#include <catch2/catch.hpp>
#include <fstream>
#include <string>

#include "support.hpp"
#include "wsjtxcli/process.hpp"

using namespace wsjtxcli;

namespace {

process::Environment snap_polluted() {
  return {
      {"SNAP", "/snap/some-app/current"},
      {"SNAP_NAME", "some-app"},
      {"LD_LIBRARY_PATH", "/snap/some-app/current/usr/lib"},
      {"QT_PLUGIN_PATH", "/snap/some-app/current/usr/plugins"},
      {"QML2_IMPORT_PATH", "/snap/some-app/current/qml"},
      {"GTK_PATH", "/snap/some-app/current/gtk"},
      {"GTK_EXE_PREFIX", "/snap/some-app/current"},
      {"GTK_DATA_PREFIX", "/snap/some-app/current"},
      {"HOME", "/home/operator"},
      {"PATH", "/usr/bin:/bin"},
  };
}

}  // namespace

TEST_CASE("a snap wsjtx keeps the environment it needs", "[process]") {
  const process::Environment sanitized =
      process::sanitize_wsjtx_environment(snap_polluted(), "/snap/wsjtx/current/usr/bin/wsjtx");
  REQUIRE(sanitized == snap_polluted());
}

TEST_CASE("snap paths are kept out of a non-snap wsjtx", "[process]") {
  const process::Environment sanitized = process::sanitize_wsjtx_environment(snap_polluted(), "/usr/bin/wsjtx");

  for (const char* dropped : {"SNAP", "SNAP_NAME", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "GTK_PATH",
                              "GTK_EXE_PREFIX", "GTK_DATA_PREFIX"}) {
    REQUIRE(sanitized.count(dropped) == 0);
  }
  REQUIRE(sanitized.at("HOME") == "/home/operator");
  REQUIRE(sanitized.at("PATH") == "/usr/bin:/bin");
}

TEST_CASE("a snap path in any value is enough to scrub", "[process]") {
  // No SNAP* variable at all: the context has to be recognised from the paths
  // themselves, which is how a shell launched from a snap looks.
  const process::Environment polluted = {
      {"LD_PRELOAD", "/lib/x86_64-linux-gnu/libfoo.so"},
      {"XDG_DATA_DIRS", "/var/lib/snapd/desktop:/usr/share"},
      {"HOME", "/home/operator"},
  };

  const process::Environment sanitized = process::sanitize_wsjtx_environment(polluted, "/usr/bin/wsjtx");
  REQUIRE(sanitized.count("LD_PRELOAD") == 0);
  // Only the variables that steer the loader and the toolkits are dropped; the
  // one carrying the snap path is left alone, as it was in Python.
  REQUIRE(sanitized.at("XDG_DATA_DIRS") == "/var/lib/snapd/desktop:/usr/share");
  REQUIRE(sanitized.at("HOME") == "/home/operator");
}

TEST_CASE("outside a snap context only the SNAP variables go", "[process]") {
  const process::Environment clean = {
      {"LD_LIBRARY_PATH", "/opt/custom/lib"},
      {"QT_PLUGIN_PATH", "/opt/custom/plugins"},
      {"HOME", "/home/operator"},
  };

  const process::Environment sanitized = process::sanitize_wsjtx_environment(clean, "/usr/bin/wsjtx");
  REQUIRE(sanitized == clean);
}

TEST_CASE("which resolves along PATH", "[process]") {
  const ScratchDir scratch("wsjtxcli-which");
  const std::filesystem::path target = scratch.path() / "wsjtx";
  {
    std::ofstream script(target);
    script << "#!/bin/sh\n";
  }
  std::filesystem::permissions(target, std::filesystem::perms::owner_all);

  const EnvVar path("PATH", scratch.path().string().c_str());
  REQUIRE(process::which("wsjtx") == target.string());
  REQUIRE_FALSE(process::which("xvfb-run").has_value());

  SECTION("a name with a slash is a path, not a lookup") {
    REQUIRE(process::which(target.string()) == target.string());
    REQUIRE_FALSE(process::which((scratch.path() / "missing").string()).has_value());
  }

  SECTION("a directory is not an executable") { REQUIRE_FALSE(process::which(scratch.path().string()).has_value()); }

  SECTION("a file without the execute bit is not one either") {
    std::filesystem::permissions(target, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    REQUIRE_FALSE(process::which("wsjtx").has_value());
  }
}

TEST_CASE("run reports how the child ended", "[process]") {
  const process::Environment env = {{"PATH", "/usr/bin:/bin"}};

  REQUIRE(process::run({"/bin/sh", "-c", "exit 0"}, env) == 0);
  REQUIRE(process::run({"/bin/sh", "-c", "exit 7"}, env) == 7);
  // A child killed by a signal is reported as the negated signal number, the
  // convention Python's subprocess hands back.
  REQUIRE(process::run({"/bin/sh", "-c", "kill -TERM $$"}, env) == -15);
  // Nothing to execute: the shell's own convention for a command it could not
  // run, which is what the reference surfaced here too.
  REQUIRE(process::run({"/nonexistent/wsjtx"}, env) == 127);
}

TEST_CASE("an empty PATH entry means the current directory", "[process]") {
  const ScratchDir scratch("wsjtxcli-cwd");
  const std::filesystem::path target = scratch.path() / "wsjtx";
  {
    std::ofstream script(target);
    script << "#!/bin/sh\n";
  }
  std::filesystem::permissions(target, std::filesystem::perms::owner_all);

  const std::filesystem::path previous = std::filesystem::current_path();
  std::filesystem::current_path(scratch.path());
  const EnvVar path("PATH", ":/nonexistent");
  const std::optional<std::string> found = process::which("wsjtx");
  std::filesystem::current_path(previous);

  REQUIRE(found == std::string("./wsjtx"));
}
