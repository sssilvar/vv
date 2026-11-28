#!/usr/bin/env bash
#
# ci-linux.sh — run the Linux CI jobs on Ubuntu 24.04 or in its container.
#
# Usage:
#   scripts/ci-linux.sh [options] [job...]
#
# Jobs (default: all):
#   format       Check tracked C++ sources with clang-format
#   quality      Release build with warnings as errors, tests, cppcheck, clang-tidy
#   sanitizers   Debug ASan/UBSan build, then tests under Xvfb with leak detection
#
# Options:
#       --docker       Run the jobs in the Dockerfile's ci stage (Docker or Podman)
#   -j, --jobs <N>     Parallel build jobs (default: CPU count)
#   -h, --help         Show this help and exit
#
# Build trees are build/ci-quality and build/ci-sanitizers. A job runs every
# step it still can after a failure and the script lists all failures at the end.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

USE_DOCKER=0
JOBS=""
jobs=()
while [ $# -gt 0 ]; do
  case "$1" in
    --docker) USE_DOCKER=1; shift ;;
    -j|--jobs)
      [ "$#" -ge 2 ] || die "$1 requires a value (try --help)"
      JOBS="$2"; shift 2 ;;
    -h|--help) sed -n '2,/^set -euo pipefail/{ /^set -euo pipefail/d; s/^# \{0,1\}//; p; }' "$0"; exit 0 ;;
    format|quality|sanitizers) jobs+=("$1"); shift ;;
    *) die "unknown argument: $1 (try --help)" ;;
  esac
done
case "$JOBS" in
  *[!0-9]*|0*) die "jobs must be a positive integer" ;;
esac
[ "${#jobs[@]}" -gt 0 ] || jobs=(format quality sanitizers)

if [ "$USE_DOCKER" = 1 ]; then
  engine=""
  for candidate in docker podman; do
    if command -v "$candidate" >/dev/null; then
      engine="$candidate"
      break
    fi
  done
  [ -n "$engine" ] || die "docker or podman not found"

  "$engine" build --target ci --tag vv-ci "$ROOT"
  # Build trees on the mounted checkout must stay owned by the caller.
  user_args=(--user "$(id -u):$(id -g)")
  [ "$engine" != podman ] || user_args=(--userns=keep-id)
  inner=(scripts/ci-linux.sh)
  [ -z "$JOBS" ] || inner+=(--jobs "$JOBS")
  # LeakSanitizer stops its own threads with ptrace.
  exec "$engine" run --rm --init --cap-add SYS_PTRACE "${user_args[@]}" \
    --env HOME=/tmp --volume "$ROOT:$ROOT" --workdir "$ROOT" \
    vv-ci "${inner[@]}" "${jobs[@]}"
fi

[ "$(uname)" = Linux ] || die "these jobs need Linux; use --docker"
[ -n "$JOBS" ] || JOBS="$(nproc)"

failures=()
step() {
  local name="$1" status=0
  shift
  if [ "${GITHUB_ACTIONS:-}" = true ]; then
    printf '::group::%s\n' "$name"
  else
    printf '\n== %s\n' "$name"
  fi
  "$@" || status=$?
  [ "${GITHUB_ACTIONS:-}" != true ] || printf '::endgroup::\n'
  if [ "$status" -ne 0 ]; then
    printf 'FAILED: %s (exit %s)\n' "$name" "$status" >&2
    failures+=("$name")
  fi
  return "$status"
}

list_sources() {
  if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git ls-files -z -- 'src/*.cpp' 'src/include/*.h' 'tests/*.cpp' 'tests/*.h'
  else
    # A container sees no usable repository for a linked worktree or a checkout
    # owned by another user.
    find src tests \( -name '*.cpp' -o -name '*.h' \) -print0
  fi
}

check_format() {
  clang-format --version
  list_sources | xargs -0 clang-format --dry-run --Werror
}

job_quality() {
  local dir=build/ci-quality
  step "quality: configure" cmake -S . -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVV_ENABLE_WARNINGS=ON -DVV_WARNINGS_AS_ERRORS=ON \
    -DBUILD_TESTING=ON || return 0
  # -k 0 reports every failing translation unit instead of the first few.
  if step "quality: build" cmake --build "$dir" -j "$JOBS" -- -k 0; then
    step "quality: tests" ctest --test-dir "$dir" --output-on-failure || true
  fi
  # The analyzers read the compilation database, so they still run after a
  # failed build. Then clang-tidy also reports generated moc files as missing.
  step "quality: cppcheck" ./build.sh -d "$dir" --no-build --cppcheck-check || true
  step "quality: clang-tidy" ./build.sh -d "$dir" --no-build --clang-tidy-check || true
}

job_sanitizers() {
  local dir=build/ci-sanitizers
  step "sanitizers: configure" cmake -S . -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    -DVV_ENABLE_SANITIZERS=ON \
    -DVV_TEST_QPA_PLATFORM=xcb || return 0
  step "sanitizers: build" cmake --build "$dir" -j "$JOBS" -- -k 0 || return 0
  step "sanitizers: tests" env \
    ASAN_OPTIONS=fast_unwind_on_malloc=0 \
    "LSAN_OPTIONS=suppressions=$ROOT/tests/lsan-fontconfig.supp:print_suppressions=1" \
    xvfb-run -a scripts/test-sanitizers.sh "$dir" || true
}

for job in "${jobs[@]}"; do
  case "$job" in
    format) step "format" check_format || true ;;
    quality) job_quality ;;
    sanitizers) job_sanitizers ;;
  esac
done

if [ "${#failures[@]}" -gt 0 ]; then
  printf '\nFailed steps:\n' >&2
  printf '  %s\n' "${failures[@]}" >&2
  exit 1
fi
printf '\nAll steps passed: %s\n' "${jobs[*]}"
