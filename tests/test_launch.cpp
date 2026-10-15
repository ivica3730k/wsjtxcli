// End to end, against the binary that ships: a fake wsjtx on PATH records what
// it was handed, and the test checks the instance config, the argv, the child
// environment and the cleanup afterwards.

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
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

/// Waits for a file the fake wsjtx is expected to create. Polling keeps the
/// test independent of how fast a shell gets going on a loaded CI runner.
bool wait_for(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

/// Waits for a directory to stop being empty. Used to tell that wsjtxcli has
/// got as far as writing its instance config.
bool wait_for_contents(const std::filesystem::path& directory, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!std::filesystem::is_empty(directory)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

/// Waits for a child to exit, so a hang shows up as a failing test rather than
/// a test run that never finishes.
bool wait_for_exit(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    int status = 0;
    const pid_t finished = waitpid(pid, &status, WNOHANG);
    if (finished == pid) {
      return true;
    }
    if (finished < 0 || std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
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
                 "/bin/cp \"$CONFIG_HOME/WSJT-X - $rig.ini\" \"$RECORD/instance.ini\"\n"
                 "/bin/mkdir -p \"$DATA_HOME/WSJT-X - $rig\"\n"
                 "echo decoded > \"$DATA_HOME/WSJT-X - $rig/decodes.txt\"\n"
                 "echo \"$rig\" > \"$RECORD/rig\"\n"
                 "exit ${FAKE_WSJTX_EXIT:-0}\n");
  }

  /// A wsjtx that stays up until it is interrupted, so the Ctrl-C path has
  /// something to interrupt.
  void install_waiting_wsjtx() const {
    write_script(bin() / "wsjtx",
                 "#!/bin/sh\n"
                 "trap 'echo interrupted > \"$RECORD/signal\"; exit 130' INT\n"
                 "rig=${1#--rig-name=}\n"
                 "/bin/cp \"$CONFIG_HOME/WSJT-X - $rig.ini\" \"$RECORD/instance.ini\"\n"
                 "echo ready > \"$RECORD/ready\"\n"
                 "while true; do /bin/sleep 0.1; done\n");
  }

  /// Starts wsjtxcli directly, with no shell in between, so the test holds the
  /// pid it needs to signal.
  /// filler_variables pads the child's environment. wsjtxcli copies and
  /// scrubs the whole environment between writing the instance config and
  /// starting the wait, so a large one widens that stretch from microseconds
  /// to milliseconds -- wide enough for a test to aim an interrupt at it.
  pid_t spawn(const std::vector<std::string>& args, std::size_t filler_variables = 0) const {
    std::vector<std::string> argv_strings = {WSJTXCLI_BINARY};
    argv_strings.insert(argv_strings.end(), args.begin(), args.end());
    std::vector<std::string> env_strings = {"PATH=" + bin().string(),
                                            "HOME=" + scratch_.path().string(),
                                            "XDG_CONFIG_HOME=" + config_home().string(),
                                            "XDG_DATA_HOME=" + data_home().string(),
                                            "CONFIG_HOME=" + config_home().string(),
                                            "DATA_HOME=" + data_home().string(),
                                            "TMPDIR=" + scratch_.path().string(),
                                            "RECORD=" + record().string()};
    for (std::size_t index = 0; index < filler_variables; ++index) {
      env_strings.push_back("WSJTXCLI_FILLER_" + std::to_string(index) + "=" + std::string(64, 'x'));
    }

    std::vector<char*> argv;
    for (std::string& value : argv_strings) {
      argv.push_back(value.data());
    }
    argv.push_back(nullptr);
    std::vector<char*> envp;
    for (std::string& value : env_strings) {
      envp.push_back(value.data());
    }
    envp.push_back(nullptr);

    // SIGINT is held off across the fork. The tests below interrupt what comes
    // back from here immediately, and a signal landing in the gap before
    // execve would be handled by the inherited Catch2 handler -- the forked
    // test process reporting a failure on the real one's behalf.
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigprocmask(SIG_BLOCK, &blocked, &previous);

    const pid_t pid = fork();
    if (pid == 0) {
      signal(SIGINT, SIG_DFL);
      sigprocmask(SIG_SETMASK, &previous, nullptr);
      execve(WSJTXCLI_BINARY, argv.data(), envp.data());
      _exit(127);
    }
    sigprocmask(SIG_SETMASK, &previous, nullptr);

    REQUIRE(pid >= 0);
    return pid;
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
            << " PATH=" << path << " HOME=" << scratch_.path().string() << " XDG_CONFIG_HOME='"
            << config_home().string() << "' XDG_DATA_HOME='" << data_home().string() << "' CONFIG_HOME='"
            << config_home().string() << "' DATA_HOME='" << data_home().string() << "'"
            << " TMPDIR=" << scratch_.path().string() << " RECORD=" << record().string() << " " << extra_env << " "
            << WSJTXCLI_BINARY << " " << arguments << " 2>&1";

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

  // Where WSJT-X would look, which is not the same on both systems. The
  // sandbox redirects $HOME, and on macOS that is all there is to redirect:
  // Qt, and so wsjtxcli, ignores the XDG variables there.
  std::filesystem::path config_home() const {
#ifdef __APPLE__
    return scratch_.path() / "Library/Preferences";
#else
    return scratch_.path() / "config";
#endif
  }

  std::filesystem::path data_home() const {
#ifdef __APPLE__
    return scratch_.path() / "Library/Application Support";
#else
    return scratch_.path() / "data";
#endif
  }

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

#ifdef __APPLE__

TEST_CASE("--nogui is refused on macOS before anything is written", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result = sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m --nogui");

  REQUIRE(result.exit_code == 2);
  REQUIRE(result.output.find("--nogui is not supported on macOS") != std::string::npos);
  REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
}

#else

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

TEST_CASE("a headless run without xvfb-run is reported, not guessed at", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result = sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m --nogui");

  REQUIRE(result.exit_code == 1);
  REQUIRE(result.output == "xvfb-run not found in PATH (install xvfb)\n");
}

#endif

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

TEST_CASE("a missing wsjtx is reported, not guessed at", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result =
      sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m", "", sandbox.record().string());

  REQUIRE(result.exit_code == 1);
  REQUIRE(result.output == "wsjtx binary not found in PATH\n");
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

TEST_CASE("Ctrl-C reaches wsjtx and the instance is still cleaned up", "[launch]") {
  const Sandbox sandbox;
  sandbox.install_waiting_wsjtx();

  const pid_t cli = sandbox.spawn({"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8", "--rx-band", "40m"});
  REQUIRE(wait_for(sandbox.record() / "ready", std::chrono::seconds(10)));

  REQUIRE(kill(cli, SIGINT) == 0);

  int status = 0;
  REQUIRE(waitpid(cli, &status, 0) == cli);
  REQUIRE(WIFEXITED(status));
  // WSJT-X exits 130 on an interrupt of its own, and that status is passed on.
  REQUIRE(WEXITSTATUS(status) == 130);
  REQUIRE(wait_for(sandbox.record() / "signal", std::chrono::seconds(1)));

  // The whole point of forwarding rather than dying: the cleanup still runs.
  REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
  REQUIRE(std::filesystem::is_empty(sandbox.data_home()));
}

TEST_CASE("an interrupt after the config is written is never lost", "[launch]") {
  // Repeated, because what the interrupt lands on is a race by nature: it may
  // find the launcher waiting, or still between writing the config and
  // getting there, or the child still short of its exec. Each of those has
  // lost the signal at some point during this port's development, and one
  // attempt hits only one of them.
  for (int attempt = 0; attempt < 3; ++attempt) {
    const Sandbox sandbox;
    sandbox.install_waiting_wsjtx();

    // Padded environment: see spawn(). Without it the stretch this aims at is
    // a few microseconds wide and the interrupt lands somewhere harmless
    // instead, which is how the first version of this test passed against the
    // very code it was meant to catch.
    const pid_t cli =
        sandbox.spawn({"--callsign", "9A3ICE", "--grid", "JN95", "--mode", "FT8", "--rx-band", "40m"}, 4000);

    // Interrupting only once the instance config exists is what keeps this
    // honest: from here on there is state on disk to clean up, so an exit
    // with both directories empty cannot be explained by wsjtxcli having been
    // killed before it did anything.
    REQUIRE(wait_for_contents(sandbox.config_home(), std::chrono::seconds(10)));
    REQUIRE(kill(cli, SIGINT) == 0);

    REQUIRE(wait_for_exit(cli, std::chrono::seconds(10)));
    // The status is deliberately not asserted: whether WSJT-X gets to handle
    // the interrupt itself, is killed by it, or was never reached depends on
    // how far the launch had got. The cleanup is not optional in any of those.
    REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
    REQUIRE(std::filesystem::is_empty(sandbox.data_home()));
  }
}

TEST_CASE("an unusable TMPDIR does not stop a launch", "[launch]") {
  const Sandbox sandbox;
  const CommandResult result =
      sandbox.run("--callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m", "TMPDIR=/nonexistent-wsjtxcli-tmpdir");

  REQUIRE(result.exit_code == 0);
  REQUIRE(read_file(sandbox.record() / "rig").rfind("wsjtx-", 0) == 0);
  REQUIRE(std::filesystem::is_empty(sandbox.config_home()));
}
