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
#       --analyze                Shorthand for --cppcheck --clang-tidy
#       --format-check           Verify clang-format cleanliness (no changes)
#       --install                Install the app/binary after a successful build
#   -h, --help                   Show this help and exit
#
# Static analysis relies on build/compile_commands.json, which this script
# always generates (CMAKE_EXPORT_COMPILE_COMMANDS=ON).
set -euo pipefail

# ── pretty output ──────────────────────────────────────────────────────
if [ -t 1 ]; then
  CYAN='\033[0;36m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
else
  CYAN=''; YELLOW=''; RED=''; GREEN=''; NC=''
fi
run() { printf "${CYAN}>>${YELLOW} %s${NC}\n" "$*"; eval "$@"; }
info() { printf "${GREEN}%s${NC}\n" "$*"; }
die() { printf "${RED}error:${NC} %s\n" "$*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── defaults ───────────────────────────────────────────────────────────
BUILD_TYPE=Release
BUILD_DIR=build
JOBS=""
CLEAN=0
DO_BUILD=1
RUN_CPPCHECK=0
RUN_CLANG_TIDY=0
RUN_FORMAT_CHECK=0
DO_INSTALL=0

while [ $# -gt 0 ]; do
  case "$1" in
    -t|--type) BUILD_TYPE="$2"; shift 2 ;;
    -d|--build-dir) BUILD_DIR="$2"; shift 2 ;;
    -j|--jobs) JOBS="$2"; shift 2 ;;
    -c|--clean) CLEAN=1; shift ;;
    --no-build) DO_BUILD=0; shift ;;
    --cppcheck) RUN_CPPCHECK=1; shift ;;
    --clang-tidy) RUN_CLANG_TIDY=1; shift ;;
    --analyze) RUN_CPPCHECK=1; RUN_CLANG_TIDY=1; shift ;;
    --format-check) RUN_FORMAT_CHECK=1; shift ;;
    --install) DO_INSTALL=1; shift ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

if [ -z "$JOBS" ]; then
  JOBS="$( { nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4; } )"
fi

command -v cmake >/dev/null || die "cmake not found"

# ── configure ──────────────────────────────────────────────────────────
if [ "$CLEAN" = 1 ] && [ -d "$BUILD_DIR" ]; then
  run "rm -rf '$BUILD_DIR'"
fi

GENERATOR=""
command -v ninja >/dev/null && GENERATOR="-G Ninja"

run "cmake -S . -B '$BUILD_DIR' $GENERATOR \
  -DCMAKE_BUILD_TYPE='$BUILD_TYPE' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DVV_ENABLE_WARNINGS=ON \
  -DVV_WARNINGS_AS_ERRORS=ON"

# ── format check (optional) ────────────────────────────────────────────
if [ "$RUN_FORMAT_CHECK" = 1 ]; then
  command -v clang-format >/dev/null || die "clang-format not found"
  info "Checking formatting…"
  run "cmake --build '$BUILD_DIR' --target format"
  if ! git diff --quiet; then
    die "clang-format produced changes; run 'cmake --build $BUILD_DIR --target format' and commit"
  fi
  info "Formatting clean."
fi

# ── build ──────────────────────────────────────────────────────────────
if [ "$DO_BUILD" = 1 ]; then
  run "cmake --build '$BUILD_DIR' --config '$BUILD_TYPE' -j '$JOBS'"
fi

# ── cppcheck (optional) ────────────────────────────────────────────────
if [ "$RUN_CPPCHECK" = 1 ]; then
  command -v cppcheck >/dev/null || die "cppcheck not found"
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  info "Running cppcheck…"
  # --check-level=exhaustive needs cppcheck >= 2.11; older distros (e.g. Ubuntu
  # 22.04 ships 2.7) reject the flag, so probe for it instead of assuming.
  CHECK_LEVEL=""
  if cppcheck --check-level=exhaustive --version >/dev/null 2>&1; then
    CHECK_LEVEL="--check-level=exhaustive"
  fi
  run "cppcheck \
    --project='$BUILD_DIR/compile_commands.json' \
    $CHECK_LEVEL \
    --enable=warning,style,performance,portability \
    --suppressions-list='$SCRIPT_DIR/.cppcheck-suppressions' \
    --inline-suppr \
    -i '$BUILD_DIR' \
    --library=qt \
    --quiet \
    --error-exitcode=1"
  info "cppcheck clean."
fi

# ── clang-tidy (optional) ──────────────────────────────────────────────
if [ "$RUN_CLANG_TIDY" = 1 ]; then
  # Prefer PATH; fall back to a Homebrew LLVM install (Apple doesn't ship clang-tidy).
  CLANG_TIDY="$(command -v clang-tidy || true)"
  if [ -z "$CLANG_TIDY" ] && command -v brew >/dev/null; then
    _llvm="$(brew --prefix llvm 2>/dev/null || true)"
    [ -x "$_llvm/bin/clang-tidy" ] && CLANG_TIDY="$_llvm/bin/clang-tidy"
  fi
  [ -n "$CLANG_TIDY" ] || die "clang-tidy not found (try: brew install llvm)"
  [ -f "$BUILD_DIR/compile_commands.json" ] || die "missing $BUILD_DIR/compile_commands.json"
  info "Running clang-tidy ($CLANG_TIDY)…"
  # Only our own sources; skip generated autogen/moc translation units.
  # (Portable array fill — macOS ships bash 3.2, which lacks `mapfile`.)
  files=()
  while IFS= read -r f; do files+=("$f"); done \
    < <(find src -name '*.cpp' -not -path '*/build/*')
  # On macOS a Homebrew clang-tidy needs the SDK sysroot to find libc++/system
  # headers, since the compile DB was produced by AppleClang.
  tidy_args=(-p "$BUILD_DIR" --quiet)
  if [ "$(uname)" = "Darwin" ] && command -v xcrun >/dev/null; then
    tidy_args+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
  fi
  # Invoke directly (not via the eval-based `run`) so the file list isn't split.
  printf "${CYAN}>>${YELLOW} %s${NC}\n" "$CLANG_TIDY ${tidy_args[*]} <sources>"
  "$CLANG_TIDY" "${tidy_args[@]}" "${files[@]}"
  info "clang-tidy clean."
fi

# ── install (optional) ─────────────────────────────────────────────────
if [ "$DO_INSTALL" = 1 ]; then
  INSTALL_DIR=${INSTALL_DIR:-$HOME/.local/bin}
  run "mkdir -p '$INSTALL_DIR'"
  case "$(uname)" in
    Darwin)
      APP_INSTALL_DIR=${APP_INSTALL_DIR:-$HOME/Applications}
      run "mkdir -p '$APP_INSTALL_DIR'"
      run "rm -rf '$APP_INSTALL_DIR/vv.app'"
      run "cp -R '$BUILD_DIR/vv.app' '$APP_INSTALL_DIR/vv.app'"
      run "ln -sf '$APP_INSTALL_DIR/vv.app/Contents/MacOS/vv' '$INSTALL_DIR/vv'"
      run "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f '$APP_INSTALL_DIR/vv.app'"
      run "qlmanage -r" || true
      info "Installed vv.app to $APP_INSTALL_DIR (CLI symlink: $INSTALL_DIR/vv)"
      ;;
    Linux)
      run "cp '$BUILD_DIR/vv' '$INSTALL_DIR/'"
      info "Installed vv to $INSTALL_DIR (ensure it is on your PATH)"
      ;;
    *)
      die "unsupported platform for --install: $(uname)"
      ;;
  esac
fi

info "Done."
