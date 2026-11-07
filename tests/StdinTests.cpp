#include "mesh_utils.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

// The standard fixes main's parameters, so argv cannot be const.
// cppcheck-suppress constParameter
int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 2;
  }
  const std::filesystem::path directory = argv[1];
  std::filesystem::create_directory(directory);
  if (setenv("TMPDIR", directory.c_str(), 1) != 0) {
    return 2;
  }
  const std::string inputPath = (directory / "input").string();
  {
    std::ofstream input(inputPath);
    input << "mesh input\n";
  }
  if (std::freopen(inputPath.c_str(), "rb", stdin) == nullptr) {
    return 2;
  }

  const std::string copiedPath = stdinToTempFile();
  std::ifstream copied(copiedPath);
  std::string contents;
  std::getline(copied, contents);
  copied.close();
  if (contents != "mesh input") {
    return 1;
  }
  std::filesystem::remove(copiedPath);

  if (std::freopen("/dev/null", "wb", stdin) == nullptr) {
    return 2;
  }
  const std::string readFailurePath = stdinToTempFile();
  if (!readFailurePath.empty()) {
    std::cerr << "A stdin read error was reported as success\n";
    return 1;
  }

  if (std::freopen(inputPath.c_str(), "rb", stdin) == nullptr) {
    return 2;
  }
  rlimit original{};
  if (getrlimit(RLIMIT_FSIZE, &original) != 0) {
    return 2;
  }
  rlimit limited = original;
  limited.rlim_cur = 0;
  const auto previousHandler = std::signal(SIGXFSZ, SIG_IGN);
  if (previousHandler == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limited) != 0) {
    return 2;
  }

  const std::string closeFailurePath = stdinToTempFile();
  const int restoreResult = setrlimit(RLIMIT_FSIZE, &original);
  std::signal(SIGXFSZ, previousHandler);
  if (restoreResult != 0) {
    return 2;
  }
  if (!closeFailurePath.empty()) {
    std::cerr << "A buffered output failure was reported as success\n";
    return 1;
  }

  std::filesystem::remove(inputPath);
  if (!std::filesystem::is_empty(directory)) {
    std::cerr << "Failed stdin copies left temporary files behind\n";
    return 1;
  }
  return std::filesystem::remove(directory) ? 0 : 1;
}
