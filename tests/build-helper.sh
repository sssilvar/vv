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
