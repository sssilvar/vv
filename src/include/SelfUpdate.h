#pragma once

#include <optional>
#include <string>

// True when release tag `candidate` ("v1.2.3" or "1.2.3") is newer than `current`.
// Returns nullopt when either is not MAJOR.MINOR.PATCH.
std::optional<bool> isNewerVersion(const std::string& candidate, const std::string& current);

// Implements `vv update`: installs the latest GitHub release over the running
// package. Returns the process exit code.
int runSelfUpdate(int argc, char* argv[]);
