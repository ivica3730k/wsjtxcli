#include <string>
#include <vector>

#include "wsjtxcli/cli.hpp"

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  return wsjtxcli::cli::run(args);
}
