#!/usr/bin/env bash
#
# build.sh — configure, build, analyze, and install vv.
#
# Usage:
#   ./build.sh [options]
#
# Options:
#   -t, --type <Debug|Release>   Build type (default: Release)
#   -d, --build-dir <dir>        Build directory (default: build)
#   -j, --jobs <N>               Parallel build jobs (default: CPU count)
#   -c, --clean                  Remove the build directory before configuring
#       --no-build               Configure only; skip the compile step
#       --cppcheck               Run cppcheck static analysis after building
#       --clang-tidy             Run clang-tidy over the compilation database
#       --clang-tidy-check       Gate stable clang-tidy correctness checks
#       --analyze                Shorthand for --cppcheck --clang-tidy
#       --format-check           Verify clang-format cleanliness (no changes)
#       --install                Install the binary after a successful build
#   -h, --help                   Show this help and exit
#
# Static analysis relies on build/compile_commands.json, which this script
# always generates (CMAKE_EXPORT_COMPILE_COMMANDS=ON).
set -euo pipefail

if [ -t 1 ]; then
  CYAN='\033[0;36m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
else
  CYAN=''; YELLOW=''; RED=''; GREEN=''; NC=''
fi
run() {
  printf '%b>>%b' "$CYAN" "$YELLOW"
  printf ' %q' "$@"
  printf '%b\n' "$NC"
  "$@"
}
info() { printf "${GREEN}%s${NC}\n" "$*"; }
die() { printf "${RED}error:${NC} %s\n" "$*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

BUILD_TYPE=Release
BUILD_DIR=build
JOBS=""
CLEAN=0
DO_BUILD=1
RUN_CPPCHECK=0
RUN_CLANG_TIDY=0
RUN_CLANG_TIDY_CHECK=0
RUN_FORMAT_CHECK=0
DO_INSTALL=0

require_value() {
  [ "$#" -ge 2 ] && [ -n "$2" ] || die "$1 requires a value (try --help)"
  case "$2" in --*) die "$1 requires a value (try --help)" ;; esac
}

while [ $# -gt 0 ]; do
  case "$1" in
    -t|--type) require_value "$@"; BUILD_TYPE="$2"; shift 2 ;;
    -d|--build-dir) require_value "$@"; BUILD_DIR="$2"; shift 2 ;;
    -j|--jobs) require_value "$@"; JOBS="$2"; shift 2 ;;
    -c|--clean) CLEAN=1; shift ;;
    --no-build) DO_BUILD=0; shift ;;
    --cppcheck) RUN_CPPCHECK=1; shift ;;
    --clang-tidy) RUN_CLANG_TIDY=1; shift ;;
    --clang-tidy-check) RUN_CLANG_TIDY_CHECK=1; shift ;;
    --analyze) RUN_CPPCHECK=1; RUN_CLANG_TIDY=1; shift ;;
    --format-check) RUN_FORMAT_CHECK=1; shift ;;
    --install) DO_INSTALL=1; shift ;;
    -h|--help) sed -n '2,/^set -euo pipefail/{ /^set -euo pipefail/d; s/^# \{0,1\}//; p; }' "$0"; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

case "$BUILD_TYPE" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) die "invalid build type: $BUILD_TYPE" ;;
esac

if [ -z "$JOBS" ]; then
  JOBS="$( { nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4; } )"
fi
case "$JOBS" in
  ''|*[!0-9]*|0*) die "jobs must be a positive integer" ;;
esac

command -v cmake >/dev/null || die "cmake not found"

if [ "$CLEAN" = 1 ] && [ -d "$BUILD_DIR" ]; then
  clean_dir="$(cd -- "$BUILD_DIR" && pwd -P)"
  [ "$clean_dir" != / ] || die "refusing to remove the filesystem root"
  case "$SCRIPT_DIR/" in
    "$clean_dir/"*) die "refusing to remove the source directory or an ancestor" ;;
  esac
  [ -f "$BUILD_DIR/CMakeCache.txt" ] || die "refusing to clean a directory without a CMake cache"
  cache_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
  [ -d "$cache_source" ] && cache_source="$(cd -- "$cache_source" && pwd -P)"
  [ "$cache_source" = "$SCRIPT_DIR" ] || die "refusing to clean another project's build directory"
  run rm -rf -- "$BUILD_DIR"
fi

configure_args=(-S . -B "$BUILD_DIR")
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null; then
  configure_args+=(-G Ninja)
fi

run cmake "${configure_args[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DVV_ENABLE_WARNINGS=ON \
  -DVV_WARNINGS_AS_ERRORS=ON

if [ "$RUN_FORMAT_CHECK" = 1 ]; then
  info "Checking formatting…"
  run cmake --build "$BUILD_DIR" --target format-check
  info "Formatting clean."
fi

if [ "$DO_BUILD" = 1 ]; then
  run cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"
fi

if [ "$RUN_CPPCHECK" = 1 ]; then
  command -v cppcheck >/dev/null || die "cppcheck not found"
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  info "Running cppcheck…"
  # --check-level=exhaustive needs cppcheck >= 2.11; older distros (e.g. Ubuntu
  # 22.04 ships 2.7) reject the flag, so probe for it instead of assuming.
  cppcheck_args=(--project="$BUILD_DIR/compile_commands.json")
  if cppcheck --check-level=exhaustive --version >/dev/null 2>&1; then
    cppcheck_args+=(--check-level=exhaustive)
  fi
  run cppcheck \
    "${cppcheck_args[@]}" \
    --enable=warning,style,performance,portability \
    --suppressions-list="$SCRIPT_DIR/.cppcheck-suppressions" \
    --inline-suppr \
    -i "$BUILD_DIR" \
    --library=qt \
    --quiet \
    --error-exitcode=1
  info "cppcheck clean."
fi

# A small set of checks whose findings are real defects and stable across
# clang-tidy releases. CI gates these; the full .clang-tidy configuration
# (modernize/readability included) stays advisory there.
CLANG_TIDY_GATE_CHECKS='-*,clang-analyzer-*,-clang-analyzer-security.FloatLoopCounter'
CLANG_TIDY_GATE_CHECKS+=',bugprone-use-after-move,bugprone-dangling-handle,bugprone-sizeof-expression'
CLANG_TIDY_GATE_CHECKS+=',bugprone-suspicious-memset-usage,bugprone-integer-division'
CLANG_TIDY_GATE_CHECKS+=',bugprone-virtual-near-miss,bugprone-infinite-loop,bugprone-string-constructor'

run_clang_tidy() {
  # Prefer PATH; fall back to a Homebrew LLVM install (Apple doesn't ship clang-tidy).
  local clang_tidy
  clang_tidy="$(command -v clang-tidy || true)"
  if [ -z "$clang_tidy" ] && command -v brew >/dev/null; then
    local llvm_prefix
    llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
    [ -x "$llvm_prefix/bin/clang-tidy" ] && clang_tidy="$llvm_prefix/bin/clang-tidy"
  fi
  [ -n "$clang_tidy" ] || die "clang-tidy not found (try: brew install llvm)"
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  info "Running clang-tidy ($clang_tidy)…"
  # Only our own sources; skip generated autogen/moc translation units.
  # (Portable array fill — macOS ships bash 3.2, which lacks `mapfile`.)
  local files=()
  while IFS= read -r f; do files+=("$f"); done \
    < <(find src -name '*.cpp' -not -path '*/build/*')
  # On macOS a Homebrew clang-tidy needs the SDK sysroot to find libc++/system
  # headers, since the compile DB was produced by AppleClang.
  local tidy_args=(-p "$BUILD_DIR" --quiet "$@")
  if [ "$(uname)" = "Darwin" ] && command -v xcrun >/dev/null; then
    tidy_args+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
  fi
  run "$clang_tidy" "${tidy_args[@]}" "${files[@]}"
  info "clang-tidy clean."
}

if [ "$RUN_CLANG_TIDY_CHECK" = 1 ]; then
  run_clang_tidy "--checks=$CLANG_TIDY_GATE_CHECKS" "--warnings-as-errors=*"
fi
if [ "$RUN_CLANG_TIDY" = 1 ]; then
  run_clang_tidy
fi

if [ "$DO_INSTALL" = 1 ]; then
  INSTALL_DIR=${INSTALL_DIR:-$HOME/.local/bin}
  run mkdir -p "$INSTALL_DIR"
  run cp "$BUILD_DIR/vv" "$INSTALL_DIR/"
  info "Installed vv to $INSTALL_DIR (ensure it is on your PATH)"
fi

info "Done."
