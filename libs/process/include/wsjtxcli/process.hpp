#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wsjtxcli::process {

/// Child environment, keyed by variable name.
using Environment = std::map<std::string, std::string>;

/// A copy of this process's environment.
Environment current_environment();

/// Strips snap runtime variables that would otherwise follow us into a
/// non-snap wsjtx. Returns the environment unchanged when the target binary is
/// itself a snap package.
Environment sanitize_wsjtx_environment(Environment env, const std::string& wsjtx_bin);

/// Resolves an executable the way a shell does: a name containing a slash is
/// taken as a path, anything else is looked up along $PATH.
std::optional<std::string> which(const std::string& name);

/// Runs a command to completion with the given environment, forwarding Ctrl-C
/// to it. Returns the child's exit status, or the negated signal number when
/// the child was killed by one -- the convention the Python implementation
/// inherited from subprocess.
int run(const std::vector<std::string>& command, const Environment& env);

}  // namespace wsjtxcli::process
