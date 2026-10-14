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

/// Does nothing on purpose. Its only job is to make a child's exit wake the
/// sigsuspend below, which a default-ignored SIGCHLD would not.
extern "C" void on_sigchld(int) {}

/// Signal handling for as long as a child is running, put back afterwards.
///
/// SIGINT and SIGCHLD are blocked for the duration and only unblocked inside
/// sigsuspend. That is what closes the window a plain handler leaves open: an
/// interrupt arriving between the check and the wait would otherwise be noted
/// and then slept through, leaving both processes running with nobody waiting
/// on the signal.
class ChildSignals {
 public:
  ChildSignals() {
    interrupted = 0;

    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blocked, &previous_mask_);

    // What sigsuspend waits under: the caller's mask with the two signals this
    // loop depends on unblocked, whatever the caller had blocked.
    wait_mask_ = previous_mask_;
    sigdelset(&wait_mask_, SIGINT);
    sigdelset(&wait_mask_, SIGCHLD);

    install(SIGINT, on_sigint, &previous_int_);
    install(SIGCHLD, on_sigchld, &previous_chld_);
  }

  ~ChildSignals() {
    sigaction(SIGINT, &previous_int_, nullptr);
    sigaction(SIGCHLD, &previous_chld_, nullptr);
    sigprocmask(SIG_SETMASK, &previous_mask_, nullptr);
  }

  ChildSignals(const ChildSignals&) = delete;
  ChildSignals& operator=(const ChildSignals&) = delete;

  /// Blocks until a signal is handled, with SIGINT and SIGCHLD let through.
  void wait_for_signal() const { sigsuspend(&wait_mask_); }

  /// The mask the child starts under: the caller's, with SIGINT and SIGCHLD
  /// let through. A blocked signal survives execve, and a wsjtx deaf to
  /// Ctrl-C is the opposite of the point.
  const sigset_t& child_mask() const { return wait_mask_; }

 private:
  static void install(int signal_number, void (*handler)(int), struct sigaction* previous) {
    struct sigaction action{};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(signal_number, &action, previous);
  }

  sigset_t previous_mask_{};
  sigset_t wait_mask_{};
  struct sigaction previous_int_{};
  struct sigaction previous_chld_{};
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
    // An empty entry is the current directory -- what a shell does with it,
    // and what shutil.which did by joining it onto a relative path.
    const std::string candidate = (directory.empty() ? "." : directory) + "/" + name;
    if (is_executable_file(candidate)) {
      return candidate;
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
  const ChildSignals signals;

  const pid_t child = fork();
  if (child < 0) {
    throw RuntimeFailure(std::string("cannot start ") + command.front() + ": " + std::strerror(errno));
  }

  if (child == 0) {
    sigprocmask(SIG_SETMASK, &signals.child_mask(), nullptr);
    execve(command.front().c_str(), argv.data(), envp.data());
    // 127 is what a shell reports for a command it could not execute, and what
    // Python's subprocess surfaces here as well.
    _exit(127);
  }

  int status = 0;
  for (;;) {
    // Every Ctrl-C is passed on, not just the first: a child that ignores one
    // would otherwise be unreachable from the terminal.
    if (interrupted != 0) {
      interrupted = 0;
      kill(child, SIGINT);
      continue;
    }

    const pid_t finished = waitpid(child, &status, WNOHANG);
    if (finished == child) {
      break;
    }
    if (finished < 0 && errno != EINTR) {
      throw RuntimeFailure(std::string("waiting for ") + command.front() + " failed: " + std::strerror(errno));
    }

    signals.wait_for_signal();
  }

  if (WIFSIGNALED(status)) {
    return -WTERMSIG(status);
  }
  return WEXITSTATUS(status);
}

}  // namespace wsjtxcli::process
