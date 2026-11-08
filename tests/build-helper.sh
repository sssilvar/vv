#!/usr/bin/env bash
set -euo pipefail

scratch="$(mktemp -d)"
scratch="$(cd "$scratch" && pwd -P)"
trap 'rm -rf -- "$scratch"' EXIT
mkdir "$scratch/bin" "$scratch/project"
cp "$1/build.sh" "$scratch/project/build.sh"
touch "$scratch/project/source-sentinel"
export VV_TEST_LOG="$scratch/args"
export PATH="$scratch/bin:$PATH"
cat > "$scratch/bin/cmake" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >> "$VV_TEST_LOG"
EOF
chmod +x "$scratch/bin/cmake"

expect_failure() {
  if bash "$scratch/project/build.sh" "$@" > "$scratch/error" 2>&1; then
    printf 'Unexpected success: %s\n' "$*" >&2
    exit 1
  fi
  if ! grep -q 'error:' "$scratch/error"; then
    cat "$scratch/error" >&2
    exit 1
  fi
}

run_helper() {
  bash "$scratch/project/build.sh" "$@" > "$scratch/output" 2>&1 || {
    cat "$scratch/output" >&2
    return 1
  }
}

for option in --type --build-dir --jobs; do
  expect_failure "$option"
done
expect_failure --jobs 0
expect_failure --jobs invalid
expect_failure --type invalid

build_dir="space and ' quote \$(touch injected)"
run_helper --no-build --build-dir "$build_dir"
grep -Fx -- "$build_dir" "$VV_TEST_LOG" > /dev/null
[ ! -e "$scratch/project/injected" ]

: > "$VV_TEST_LOG"
run_helper --no-build --format-check
grep -Fx -- format-check "$VV_TEST_LOG" > /dev/null

mkdir "$scratch/project/existing-build"
touch "$scratch/project/existing-build/CMakeCache.txt"
: > "$VV_TEST_LOG"
run_helper --no-build --build-dir existing-build
if grep -Fx -- -G "$VV_TEST_LOG" > /dev/null; then
  printf 'Reconfiguration must preserve the existing generator\n' >&2
  exit 1
fi

mkdir "$scratch/project/unrelated"
expect_failure --clean --build-dir unrelated
expect_failure --clean --build-dir .
[ -f "$scratch/project/source-sentinel" ]
ln -s "$scratch/project" "$scratch/project/source-link"
expect_failure --clean --build-dir source-link
[ -f "$scratch/project/source-sentinel" ]

mkdir "$scratch/project/old-build"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\n' "$scratch/project" > "$scratch/project/old-build/CMakeCache.txt"
run_helper --no-build --clean --build-dir old-build
[ ! -e "$scratch/project/old-build" ]

cp "$1/.cppcheck-suppressions" "$1/.cppcheck-advisory" "$scratch/project/"
mkdir "$scratch/project/src" "$scratch/project/tests" "$scratch/project/build"
touch "$scratch/project/src/source with spaces.cpp" "$scratch/project/tests/test.cpp" \
  "$scratch/project/tests/StdinTests.cpp"
printf 'BUILD_TESTING:BOOL=ON\n' > "$scratch/project/build/CMakeCache.txt"
printf '[]\n' > "$scratch/project/build/compile_commands.json"
for tool in cppcheck clang-tidy clazy-standalone; do
  cat > "$scratch/bin/$tool" <<'EOF'
#!/usr/bin/env bash
for arg in "$@"; do
  [ "$arg" != --version ] || exit 0
done
printf '%s\n' "$@" >> "$VV_TEST_LOG"
exit "${VV_ANALYZER_STATUS:-0}"
EOF
  chmod +x "$scratch/bin/$tool"
done

: > "$VV_TEST_LOG"
run_helper --no-build --cppcheck-check
grep -Fx -- "--exitcode-suppressions=$scratch/project/.cppcheck-advisory" "$VV_TEST_LOG" > /dev/null

: > "$VV_TEST_LOG"
run_helper --no-build --unused-functions
grep -Fx -- --enable=unusedFunction "$VV_TEST_LOG" > /dev/null

: > "$VV_TEST_LOG"
run_helper --no-build --clazy --clang-tidy-check
grep -Fx -- 'src/source with spaces.cpp' "$VV_TEST_LOG" > /dev/null
grep -Fx -- tests/test.cpp "$VV_TEST_LOG" > /dev/null

cat > "$scratch/bin/uname" <<'EOF'
#!/usr/bin/env bash
printf 'MINGW64_NT\n'
EOF
chmod +x "$scratch/bin/uname"
: > "$VV_TEST_LOG"
run_helper --no-build --clazy
if grep -Fx -- tests/StdinTests.cpp "$VV_TEST_LOG" > /dev/null; then
  printf 'Windows analysis must exclude the Unix-only stdin test target\n' >&2
  exit 1
fi
grep -Fx -- tests/test.cpp "$VV_TEST_LOG" > /dev/null

export VV_ANALYZER_STATUS=7
for option in --cppcheck --cppcheck-check --unused-functions --clang-tidy --clang-tidy-check --clazy; do
  status=0
  bash "$scratch/project/build.sh" --no-build "$option" > "$scratch/output" 2>&1 || status=$?
  if [ "$status" != 7 ]; then
    printf '%s lost analyzer failure status: %s\n' "$option" "$status" >&2
    cat "$scratch/output" >&2
    exit 1
  fi
done
