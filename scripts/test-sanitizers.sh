#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
  printf 'Usage: xvfb-run -a %s BUILD_DIR\n' "$0" >&2
  exit 2
fi
: "${DISPLAY:?Run this script under xvfb-run -a}"

VV_WM_READY_FILE="$(mktemp)"
export VV_WM_READY_FILE
wm_log="$(mktemp)"
# Openbox invokes its startup command after it begins managing windows.
openbox --sm-disable --startup "sh -c 'printf ready > \"\$VV_WM_READY_FILE\"'" > "$wm_log" 2>&1 &
wm_pid=$!
cleanup() {
  kill "$wm_pid" 2>/dev/null || true
  wait "$wm_pid" 2>/dev/null || true
  unlink "$VV_WM_READY_FILE"
  unlink "$wm_log"
}
trap cleanup EXIT

for ((attempt = 0; attempt < 100; ++attempt)); do
  [ ! -s "$VV_WM_READY_FILE" ] || break
  if ! kill -0 "$wm_pid" 2>/dev/null; then
    cat "$wm_log" >&2
    printf 'Openbox startup failed\n' >&2
    exit 1
  fi
  sleep 0.1
done
if [ ! -s "$VV_WM_READY_FILE" ]; then
  cat "$wm_log" >&2
  printf 'Openbox startup timed out after 10 seconds\n' >&2
  exit 1
fi

# These checks use raster widgets and count VTK submissions without rendering.
# Avoid loading Mesa just to choose visuals for raster windows.
QT_XCB_GL_INTEGRATION=none ctest --test-dir "$1" --output-on-failure
