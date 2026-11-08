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
#       --cppcheck-check         Gate cppcheck with known noisy checks advisory
#       --unused-functions       Audit unused functions in this build configuration
#       --clang-tidy             Run clang-tidy over the compilation database
#       --clang-tidy-check       Gate stable clang-tidy correctness checks
#       --clazy                  Run the initial Qt correctness check set
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
RUN_CPPCHECK_CHECK=0
RUN_UNUSED_FUNCTIONS=0
RUN_CLANG_TIDY=0
RUN_CLANG_TIDY_CHECK=0
RUN_CLAZY=0
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
    --cppcheck-check) RUN_CPPCHECK_CHECK=1; shift ;;
    --unused-functions) RUN_UNUSED_FUNCTIONS=1; shift ;;
    --clang-tidy) RUN_CLANG_TIDY=1; shift ;;
    --clang-tidy-check) RUN_CLANG_TIDY_CHECK=1; shift ;;
    --clazy) RUN_CLAZY=1; shift ;;
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

run_cppcheck() {
  command -v cppcheck >/dev/null || die "cppcheck not found"
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  info "Running cppcheck…"
  # --check-level=exhaustive needs cppcheck >= 2.11; older distros (e.g. Ubuntu
  # 22.04 ships 2.7) reject the flag, so probe for it instead of assuming.
  local cppcheck_args=(--project="$BUILD_DIR/compile_commands.json")
  if cppcheck --check-level=exhaustive --version >/dev/null 2>&1; then
    cppcheck_args+=(--check-level=exhaustive)
  fi
  run cppcheck \
    "${cppcheck_args[@]}" \
    --std=c++17 \
    --suppressions-list="$SCRIPT_DIR/.cppcheck-suppressions" \
    --inline-suppr \
    -i "$BUILD_DIR" \
    --library=qt \
    --quiet \
    --error-exitcode=1 "$@"
  info "cppcheck complete."
}

if [ "$RUN_CPPCHECK" = 1 ]; then
  run_cppcheck --enable=warning,style,performance,portability
fi
if [ "$RUN_CPPCHECK_CHECK" = 1 ]; then
  run_cppcheck --enable=warning,style,performance,portability \
    "--exitcode-suppressions=$SCRIPT_DIR/.cppcheck-advisory"
fi
if [ "$RUN_UNUSED_FUNCTIONS" = 1 ]; then
  run mkdir -p "$BUILD_DIR/cppcheck-unused"
  run_cppcheck --enable=unusedFunction "--cppcheck-build-dir=$BUILD_DIR/cppcheck-unused"
fi

# A small set of checks whose findings are real defects and stable across
# clang-tidy releases. CI gates these; the full .clang-tidy configuration
# (modernize/readability included) stays advisory there.
CLANG_TIDY_GATE_CHECKS='-*,clang-analyzer-*,-clang-analyzer-security.FloatLoopCounter'
CLANG_TIDY_GATE_CHECKS+=',bugprone-use-after-move,bugprone-dangling-handle,bugprone-sizeof-expression'
CLANG_TIDY_GATE_CHECKS+=',bugprone-suspicious-memset-usage,bugprone-integer-division'
CLANG_TIDY_GATE_CHECKS+=',bugprone-virtual-near-miss,bugprone-infinite-loop,bugprone-string-constructor'

if [ "$RUN_CLANG_TIDY" = 1 ] || [ "$RUN_CLANG_TIDY_CHECK" = 1 ] || [ "$RUN_CLAZY" = 1 ]; then
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  analysis_dirs=(src)
  if grep -Eiq '^BUILD_TESTING:BOOL=(1|ON|YES|TRUE|Y)$' "$BUILD_DIR/CMakeCache.txt"; then
    analysis_dirs+=(tests)
  fi
  # Generated moc/autogen units remain outside this source list.
  analysis_files=()
  analysis_platform="$(uname)"
  while IFS= read -r -d '' f; do
    # The stdin test target is Unix-only, including when running from MSYS2.
    case "$analysis_platform/$f" in
      MINGW*/tests/StdinTests.cpp|MSYS*/tests/StdinTests.cpp) continue ;;
    esac
    analysis_files+=("$f")
  done \
    < <(find "${analysis_dirs[@]}" -name '*.cpp' -print0)
  clang_args=(-p "$BUILD_DIR")
  if [ "$(uname)" = Darwin ] && command -v xcrun >/dev/null; then
    clang_args+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
  fi
fi

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
  info "Running clang-tidy ($clang_tidy)…"
  run "$clang_tidy" "${clang_args[@]}" --quiet "$@" "${analysis_files[@]}"
  info "clang-tidy clean."
}

if [ "$RUN_CLANG_TIDY_CHECK" = 1 ]; then
  run_clang_tidy "--checks=$CLANG_TIDY_GATE_CHECKS" "--warnings-as-errors=*"
fi
if [ "$RUN_CLANG_TIDY" = 1 ]; then
  run_clang_tidy
fi
if [ "$RUN_CLAZY" = 1 ]; then
  command -v clazy-standalone >/dev/null || die "clazy-standalone not found"
  clazy_args=(--extra-arg=-Werror)
  if [ "$(uname)" = Darwin ] && command -v brew >/dev/null; then
    llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
    if [ -x "$llvm_prefix/bin/clang" ]; then
      # Homebrew installs Clazy outside LLVM's bin directory, hiding builtin headers.
      clazy_args+=("--extra-arg=-resource-dir=$("$llvm_prefix/bin/clang" -print-resource-dir)")
    fi
  fi
  clazy_checks='connect-non-signal,lambda-in-connect,lambda-unique-connection'
  clazy_checks+=',temporary-iterator,writing-to-temporary,wrong-qevent-cast'
  clazy_checks+=',connect-3arg-lambda,returning-data-from-temporary'
  CLAZY_HEADER_FILTER='(^|/)(src/include|tests)/' \
    run clazy-standalone "${clang_args[@]}" "-checks=$clazy_checks" \
      "${clazy_args[@]}" "${analysis_files[@]}"
fi

if [ "$DO_INSTALL" = 1 ]; then
  INSTALL_DIR=${INSTALL_DIR:-$HOME/.local/bin}
  run mkdir -p "$INSTALL_DIR"
  run cp "$BUILD_DIR/vv" "$INSTALL_DIR/"
  info "Installed vv to $INSTALL_DIR (ensure it is on your PATH)"
fi

info "Done."
