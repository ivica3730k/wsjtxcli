#include "wsjtxcli/process.hpp"

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "wsjtxcli/error.hpp"

extern char** environ;

namespace wsjtxcli::process {
namespace {

bool has_snap_path(const std::string& value) {
  return value.find("/snap/") != std::string::npos || value.find("/var/lib/snapd/") != std::string::npos;
}

bool is_snap_variable(const std::string& key) { return key == "SNAP" || key.rfind("SNAP_", 0) == 0; }

bool is_executable_file(const std::string& path) {
  struct stat info{};
  if (stat(path.c_str(), &info) != 0 || S_ISDIR(info.st_mode)) {
    return false;
  }
  return access(path.c_str(), X_OK) == 0;
}

/// Set by the SIGINT handler only. Everything that is not async-signal-safe --
/// sending the signal on, giving up on the wait -- happens in the wait loop.
volatile std::sig_atomic_t interrupted = 0;

extern "C" void on_sigint(int) { interrupted = 1; }

/// Installs a SIGINT handler for as long as a child is running, and puts the
/// previous one back afterwards. Deliberately without SA_RESTART: waitpid has
/// to come back with EINTR so the signal can be passed to the child.
class SigintForwarder {
 public:
  SigintForwarder() {
    interrupted = 0;
    struct sigaction action{};
    action.sa_handler = on_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    installed_ = sigaction(SIGINT, &action, &previous_) == 0;
  }

  ~SigintForwarder() {
    if (installed_) {
      sigaction(SIGINT, &previous_, nullptr);
    }
  }

  SigintForwarder(const SigintForwarder&) = delete;
  SigintForwarder& operator=(const SigintForwarder&) = delete;

 private:
  bool installed_ = false;
  struct sigaction previous_{};
};

std::vector<char*> to_argv(const std::vector<std::string>& values) {
  std::vector<char*> pointers;
  pointers.reserve(values.size() + 1);
  for (const std::string& value : values) {
    pointers.push_back(const_cast<char*>(value.c_str()));
  }
  pointers.push_back(nullptr);
  return pointers;
}

}  // namespace

Environment current_environment() {
  Environment env;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    const std::string line(*entry);
    const std::size_t split = line.find('=');
    // An entry with no '=' is not a variable assignment and cannot be passed
    // on as one.
    if (split != std::string::npos) {
      env.emplace(line.substr(0, split), line.substr(split + 1));
    }
  }
  return env;
}

Environment sanitize_wsjtx_environment(Environment env, const std::string& wsjtx_bin) {
  // Do not scrub anything when the target binary itself is a snap package.
  if (wsjtx_bin.find("/snap/") != std::string::npos) {
    return env;
  }

  // Decided before anything is removed: dropping the SNAP variables first
  // would hide the very context they establish.
  bool snap_context = false;
  for (const auto& [key, value] : env) {
    if (is_snap_variable(key) || has_snap_path(value)) {
      snap_context = true;
      break;
    }
  }

  // Running wsjtxcli from a snap (or a shell polluted by snap paths) can leak
  // snap glibc/Qt paths into /usr/bin/wsjtx and trigger symbol lookup errors.
  for (auto it = env.begin(); it != env.end();) {
    const bool drop = is_snap_variable(it->first) ||
                      (snap_context && (it->first.rfind("LD_", 0) == 0 || it->first == "QT_PLUGIN_PATH" ||
                                        it->first == "QML2_IMPORT_PATH" || it->first == "GTK_PATH" ||
                                        it->first == "GTK_EXE_PREFIX" || it->first == "GTK_DATA_PREFIX"));
    it = drop ? env.erase(it) : std::next(it);
  }

  return env;
}

std::optional<std::string> which(const std::string& name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (name.find('/') != std::string::npos) {
    return is_executable_file(name) ? std::optional<std::string>(name) : std::nullopt;
  }

  const char* raw_path = std::getenv("PATH");
  const std::string path = (raw_path != nullptr) ? raw_path : "/usr/bin:/bin";
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = path.find(':', start);
    const std::string directory = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!directory.empty()) {
      const std::string candidate = directory + "/" + name;
      if (is_executable_file(candidate)) {
        return candidate;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return std::nullopt;
}

int run(const std::vector<std::string>& command, const Environment& env) {
  if (command.empty()) {
    throw RuntimeFailure("no command to run");
  }

  std::vector<std::string> env_lines;
  env_lines.reserve(env.size());
  for (const auto& [key, value] : env) {
    env_lines.push_back(key + "=" + value);
  }

  std::vector<char*> argv = to_argv(command);
  std::vector<char*> envp = to_argv(env_lines);

  // Installed before the fork so a Ctrl-C landing in the gap is still caught
  // rather than killing us outright and orphaning the child.
  const SigintForwarder forwarder;

  const pid_t child = fork();
  if (child < 0) {
    throw RuntimeFailure(std::string("cannot start ") + command.front() + ": " + std::strerror(errno));
  }

  if (child == 0) {
    execve(command.front().c_str(), argv.data(), envp.data());
    // 127 is what a shell reports for a command it could not execute, and what
    // Python's subprocess surfaces here as well.
    _exit(127);
  }

  int status = 0;
  for (;;) {
    const pid_t finished = waitpid(child, &status, 0);
    if (finished == child) {
      break;
    }
    if (errno == EINTR) {
      // Every Ctrl-C is passed on, not just the first: a child that ignores
      // one would otherwise be unreachable from the terminal.
      if (interrupted != 0) {
        interrupted = 0;
        kill(child, SIGINT);
      }
      continue;
    }
    throw RuntimeFailure(std::string("waiting for ") + command.front() + " failed: " + std::strerror(errno));
  }

  if (WIFSIGNALED(status)) {
    return -WTERMSIG(status);
  }
  return WEXITSTATUS(status);
}

}  // namespace wsjtxcli::process
