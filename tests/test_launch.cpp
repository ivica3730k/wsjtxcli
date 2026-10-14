// End to end, against the binary that ships: a fake wsjtx on PATH records what
// it was handed, and the test checks the instance config, the argv, the child
// environment and the cleanup afterwards.

#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "catch.hpp"
#include "support.hpp"

namespace {

struct CommandResult {
  int exit_code = 0;
  std::string output;
};

void write_script(const std::filesystem::path& path, const std::string& body) {
  std::ofstream script(path);
  script << body;
  script.close();
  std::filesystem::permissions(path, std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

/// A launch under a sandbox: PATH, HOME and both XDG roots point into a
/// scratch tree, so the test never touches the operator's real WSJT-X profile.
class Sandbox {
 public:
  Sandbox() {
    std::filesystem::create_directories(bin());
    std::filesystem::create_directories(record());
    std::filesystem::create_directories(config_home());
    std::filesystem::create_directories(data_home());

    // Copies out the instance config it was started with, then leaves a data
    // directory behind so the cleanup has something to remove.
    write_script(bin() / "wsjtx",
                 "#!/bin/sh\n"
                 // Absolute paths and shell builtins only: the sandbox PATH
                 // holds this directory alone, so that a wsjtx missing from it
                 // stays missing rather than being found in /usr/bin.
                 "/usr/bin/env > \"$RECORD/env\"\n"
                 "for arg in \"$@\"; do echo \"$arg\"; done > \"$RECORD/argv\"\n"
                 "rig=${1#--rig-name=}\n"
                 "/bin/cp \"$XDG_CONFIG_HOME/WSJT-X - $rig.ini\" \"$RECORD/instance.ini\"\n"
                 "/bin/mkdir -p \"$XDG_DATA_HOME/WSJT-X - $rig\"\n"
                 "echo decoded > \"$XDG_DATA_HOME/WSJT-X - $rig/decodes.txt\"\n"
                 "echo \"$rig\" > \"$RECORD/rig\"\n"
                 "exit ${FAKE_WSJTX_EXIT:-0}\n");
  }

  /// xvfb-run as wsjtxcli calls it: -a, -s <screen spec>, then the command.
  void install_fake_xvfb() const {
    write_script(bin() / "xvfb-run",
                 "#!/bin/sh\n"
                 "for arg in \"$@\"; do echo \"$arg\"; done > \"$RECORD/xvfb-argv\"\n"
                 "shift 3\n"
                 "exec \"$@\"\n");
  }

  CommandResult run(const std::string& arguments, const std::string& extra_env = "",
                    const std::string& path_override = "") const {
    const std::string path = path_override.empty() ? bin().string() : path_override;
    std::ostringstream command;
    command << "env -i"
            << " PATH=" << path << " HOME=" << scratch_.path().string() << " XDG_CONFIG_HOME=" << config_home().string()
            << " XDG_DATA_HOME=" << data_home().string() << " TMPDIR=" << scratch_.path().string()
            << " RECORD=" << record().string() << " " << extra_env << " " << WSJTXCLI_BINARY << " " << arguments
            << " 2>&1";

    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.str().c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
      output += buffer.data();
    }
    const int status = pclose(pipe);
    return CommandResult{WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
  }

  std::filesystem::path bin() const { return scratch_.path() / "bin"; }
  std::filesystem::path record() const { return scratch_.path() / "record"; }
  std::filesystem::path config_home() const { return scratch_.path() / "config"; }
  std::filesystem::path data_home() const { return scratch_.path() / "data"; }

 private:
  ScratchDir scratch_{"wsjtxcli-launch"};
};

}  // namespace

TEST_CASE("a band launch writes the instance config and cleans it up", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result =
      sandbox.run("--callsign 9a3ice --grid jn95 --mode ft8 --rx-band 40m --audio-device sdr_usb.monitor");

  REQUIRE(result.exit_code == 0);

  const std::string instance = read_file(sandbox.record() / "instance.ini");
  REQUIRE(instance.find("MyCall=9A3ICE\n") != std::string::npos);
  REQUIRE(instance.find("MyGrid=JN95\n") != std::string::npos);
  REQUIRE(instance.find("Mode=FT8\n") != std::string::npos);
  REQUIRE(instance.find("DialFreq=7074000\n") != std::string::npos);
  REQUIRE(instance.find("SoundInName=sdr_usb.monitor\n") != std::string::npos);
  REQUIRE(instance.find("Rig=None\n") != std::string::npos);
  REQUIRE(instance.find("TxWatchdog=1\n") != std::string::npos);
  REQUIRE(instance.find("PSKReporter=false\n") != std::string::npos);
  REQUIRE(instance.find("UploadSpots=false\n") != std::string::npos);

  const std::string rig = read_file(sandbox.record() / "rig");
  REQUIRE(rig.rfind("wsjtx-", 0) == 0);
  REQUIRE(read_file(sandbox.record() / "argv") == "--rig-name=" + rig);

  // Nothing of the instance survives the run: not the config, not its data
  // directory, not the temporary file the rig name came from.
  REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
  REQUIRE(std::filesystem::is_empty(sandbox.data_home()));
}

TEST_CASE("the child's exit status is the command's exit status", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result =
      sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m", "FAKE_WSJTX_EXIT=3");
  REQUIRE(result.exit_code == 3);
}

TEST_CASE("--nogui runs wsjtx under xvfb-run on an X11 display", "[launch]") {
  const Sandbox sandbox;
  sandbox.install_fake_xvfb();
  const CommandResult result = sandbox.run(
      "--callsign 9A3ICE --grid JN95 --mode WSPR --rx-band 20m "
      "--wsprnet-reporter --nogui",
      "WAYLAND_DISPLAY=wayland-0");

  REQUIRE(result.exit_code == 0);
  REQUIRE(read_file(sandbox.record() / "xvfb-argv").rfind("-a\n-s\n-screen 0 1024x768x24\n", 0) == 0);

  const std::string env = read_file(sandbox.record() / "env");
  REQUIRE(env.find("QT_QPA_PLATFORM=xcb\n") != std::string::npos);
  // Qt would pick Wayland over the display Xvfb just started.
  REQUIRE(env.find("WAYLAND_DISPLAY=") == std::string::npos);
  REQUIRE(read_file(sandbox.record() / "instance.ini").find("UploadSpots=true\n") != std::string::npos);
}

TEST_CASE("snap variables do not reach a non-snap wsjtx", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result = sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m",
                                           "SNAP_NAME=some-app LD_LIBRARY_PATH=/snap/some-app/current/usr/lib");

  REQUIRE(result.exit_code == 0);
  const std::string env = read_file(sandbox.record() / "env");
  REQUIRE(env.find("SNAP_NAME=") == std::string::npos);
  REQUIRE(env.find("LD_LIBRARY_PATH=") == std::string::npos);
  REQUIRE(env.find("HOME=") != std::string::npos);
}

TEST_CASE("a missing wsjtx or xvfb-run is reported, not guessed at", "[launch]") {
  const Sandbox sandbox;

  SECTION("no wsjtx on PATH") {
    const CommandResult result =
        sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m", "", sandbox.record().string());
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.output == "wsjtx binary not found in PATH\n");
  }

  SECTION("no xvfb-run for a headless run") {
    const CommandResult result = sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m --nogui");
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.output == "xvfb-run not found in PATH (install xvfb)\n");
  }
}

TEST_CASE("a mode and band with no default dial frequency stops the launch", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result = sandbox.run("--callsign 9A3ICE --grid JN95 --mode Q65 --rx-band 40m");

  REQUIRE(result.exit_code == 1);
  REQUIRE(result.output == "no default dial frequencies known for mode Q65; pass --rx-frequency explicitly\n");
  REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
}

TEST_CASE("a usage error exits 2 the way argparse did", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result = sandbox.run("--callsign 9A3ICE --grid ZZ99 --mode FT8 --rx-band 40m");

  REQUIRE(result.exit_code == 2);
  REQUIRE(result.output.find("grid must be a 4- or 6-character Maidenhead locator") != std::string::npos);
}

TEST_CASE("--help and --version answer without launching anything", "[launch]") {
  const Sandbox sandbox;

  const CommandResult help = sandbox.run("--help");
  REQUIRE(help.exit_code == 0);
  REQUIRE(help.output.find("--callsign") != std::string::npos);
  REQUIRE(help.output.find("--rx-band") != std::string::npos);

  const CommandResult version = sandbox.run("--version");
  REQUIRE(version.exit_code == 0);
  REQUIRE(version.output == std::string(WSJTXCLI_VERSION) + "\n");

  REQUIRE_FALSE(std::filesystem::exists(sandbox.record() / "argv"));
}
