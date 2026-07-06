#!/usr/bin/env bash
#
# package-windows.sh — stage a self-contained Windows bundle and smoke-test it.
#
# Run from an MSYS2 MINGW64 shell, after building (see build.sh). This is the
# single source of truth for the Windows packaging: the release workflow calls
# it, and you can run the identical thing locally before pushing a tag.
#
# By default it emits a single portable .exe (scripts/vv-portable.nsi): one
# file, no installer, that unpacks to %LOCALAPPDATA%\vv\ on first run and
# launches. Pass --zip to also produce the raw extract-and-run folder zip.
#
# Usage:
#   scripts/package-windows.sh [options]
#
# Options:
#   -b, --build-dir <dir>    Build directory holding vv.exe (default: build)
#   -s, --stage-name <name>  Bundle/output base name
#                            (default: vv-${GITHUB_REF_NAME:-local}-x86_64-windows)
#   -o, --out-dir <dir>      Where the .exe/.zip land (default: dist)
#       --app-version <x.y.z> Numeric version stamped into the .exe (default:
#                            derived from GITHUB_REF_NAME, else 0.0.0)
#       --no-portable        Skip building the portable .exe
#       --zip                Also write the extract-and-run folder .zip
#       --no-smoke           Skip the bundle launch smoke test
#       --no-verify-portable Skip launching the built .exe to confirm it unpacks
#   -h, --help               Show this help and exit
#
# Exit status is nonzero if staging, the smoke test, or the portable launch
# check fails, so CI fails loudly instead of shipping a bundle that won't run.
set -euo pipefail

if [ -t 1 ]; then
  CYAN='\033[0;36m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
else
  CYAN=''; YELLOW=''; RED=''; GREEN=''; NC=''
fi
run() { printf "${CYAN}>>${YELLOW} %s${NC}\n" "$*"; eval "$@"; }
info() { printf "${GREEN}%s${NC}\n" "$*"; }
die() { printf "${RED}error:${NC} %s\n" "$*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

BUILD_DIR=build
STAGE_NAME="vv-${GITHUB_REF_NAME:-local}-x86_64-windows"
OUT_DIR=dist
APP_VERSION=""
DO_PORTABLE=1
DO_ZIP=0
DO_SMOKE=1
DO_VERIFY_PORTABLE=1

while [ $# -gt 0 ]; do
  case "$1" in
    -b|--build-dir) BUILD_DIR="$2"; shift 2 ;;
    -s|--stage-name) STAGE_NAME="$2"; shift 2 ;;
    -o|--out-dir) OUT_DIR="$2"; shift 2 ;;
    --app-version) APP_VERSION="$2"; shift 2 ;;
    --no-portable) DO_PORTABLE=0; shift ;;
    --zip) DO_ZIP=1; shift ;;
    --no-smoke) DO_SMOKE=0; shift ;;
    --no-verify-portable) DO_VERIFY_PORTABLE=0; shift ;;
    -h|--help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# Normalize the VERSIONINFO stamp to numeric MAJOR.MINOR.PATCH (the .exe build
# rejects anything else). Derive from the tag (v0.1.5 → 0.1.5) when not given.
if [ -z "$APP_VERSION" ]; then APP_VERSION="${GITHUB_REF_NAME:-}"; fi
APP_VERSION="${APP_VERSION#v}"
IFS=. read -r _vmaj _vmin _vpat _ <<EOF
$APP_VERSION
EOF
_num() { case "$1" in ''|*[!0-9]*) printf 0 ;; *) printf '%s' "$1" ;; esac; }
APP_VERSION="$(_num "$_vmaj").$(_num "$_vmin").$(_num "$_vpat")"

MINGW="${MSYSTEM_PREFIX:-/mingw64}"

case "$(uname -s)" in MINGW*|MSYS*) ;; *) die "run me from an MSYS2 MINGW64 shell" ;; esac
command -v windeployqt >/dev/null || die "windeployqt not found (install mingw-w64-x86_64-qt6-tools)"
command -v ldd >/dev/null || die "ldd not found"
[ "$DO_PORTABLE" = 1 ] && { command -v makensis >/dev/null || die "makensis not found (install mingw-w64-x86_64-nsis)"; }
[ -f "$BUILD_DIR/vv.exe" ] || die "$BUILD_DIR/vv.exe not found — build first (see build.sh)"

STAGE="stage/$STAGE_NAME"

# ── stage the binary + Qt/VTK/MinGW runtime ─────────────────────────────
run "rm -rf '$STAGE'"
run "mkdir -p '$STAGE'"
run "cp '$BUILD_DIR/vv.exe' '$STAGE/'"
# windeployqt copies Qt DLLs + plugins (platforms/, styles/, …) and the MinGW
# compiler runtime (libgcc/libstdc++/libwinpthread) beside the exe.
run "windeployqt --compiler-runtime '$STAGE/vv.exe'"

# windeployqt resolves the exe's Qt deps but not VTK or other non-Qt deps.
# Copy every remaining non-system DLL any bundled binary links, looping until
# the dependency closure is stable (deps of deps).
for _ in 1 2 3 4; do
  before=$(find "$STAGE" -name '*.dll' | wc -l)
  find "$STAGE" \( -name '*.dll' -o -name '*.exe' \) | \
    while read -r bin; do ldd "$bin" 2>/dev/null; done | \
    awk -v p="$MINGW" '$0 ~ ("=> " p "/"){print $3}' | sort -u | \
    while read -r dll; do
      [ -e "$STAGE/$(basename "$dll")" ] || cp "$dll" "$STAGE/"
    done
  after=$(find "$STAGE" -name '*.dll' | wc -l)
  if [ "$before" = "$after" ]; then break; fi
done
info "Staged $(find "$STAGE" -name '*.dll' | wc -l) DLLs into $STAGE"

# ── smoke-test the bundle with only its own DLLs ───────────────────────
# `vv --version` prints and exits before any window/QApplication is created,
# so this fails fast on a missing DLL without needing a display.
if [ "$DO_SMOKE" = 1 ]; then
  ( cd "$STAGE" && PATH="$PWD" ./vv.exe --version ) || die "Windows bundle failed to launch — missing DLL"
  info "Windows bundle smoke OK"
fi

run "mkdir -p '$OUT_DIR'"
OUT_ABS="$(cd "$OUT_DIR" && pwd)"
ICON="$REPO_DIR/assets/icons/generated/vv_icon.ico"
[ -f "$ICON" ] || die "missing $ICON"

# Authenticode signing (the one thing that actually clears SmartScreen). Set
# VV_SIGN_CMD to a command that signs the file passed as its last arg, e.g.
#   export VV_SIGN_CMD='signtool sign /fd SHA256 /tr http://ts.ssl.com /td SHA256 /a'
# When unset, packaging proceeds unsigned and just notes it.
sign() {
  if [ -n "${VV_SIGN_CMD:-}" ]; then
    run "$VV_SIGN_CMD '$(cygpath -w "$1")'"
  fi
}

# Sign vv.exe *before* it goes into the bundle, so the unpacked app is signed too.
[ -n "${VV_SIGN_CMD:-}" ] && sign "$STAGE/vv.exe"

# ── portable single-exe (default) ──────────────────────────────────────
if [ "$DO_PORTABLE" = 1 ]; then
  stage_win="$(cygpath -w "$(cd "$STAGE" && pwd)")"
  exe_win="$(cygpath -w "$OUT_ABS/$STAGE_NAME.exe")"
  icon_win="$(cygpath -w "$ICON")"
  run "makensis -V2 \
    -DVERSION='$APP_VERSION' \
    -DCACHEKEY='$STAGE_NAME' \
    -DICON='$icon_win' \
    -DSTAGE_DIR='$stage_win' \
    -DOUTFILE='$exe_win' \
    '$SCRIPT_DIR/vv-portable.nsi'"
  sign "$OUT_DIR/$STAGE_NAME.exe"
  info "Wrote $OUT_DIR/$STAGE_NAME.exe (portable${VV_SIGN_CMD:+, signed})"

  if [ "$DO_VERIFY_PORTABLE" = 1 ]; then
    cache="$(cygpath -u "$LOCALAPPDATA")/vv/$STAGE_NAME"
    run "rm -rf '$cache'"
    "$OUT_DIR/$STAGE_NAME.exe" --version >/dev/null 2>&1 || true
    [ -f "$cache/ok.marker" ] || die "portable .exe did not unpack (no $cache/ok.marker)"
    [ -f "$cache/vv.exe" ] || die "portable unpack is missing vv.exe"
    ( cd "$cache" && PATH="$PWD" ./vv.exe --version ); rc=$?
    [ "$rc" = 0 ] || die "unpacked portable bundle failed smoke (rc=$rc)"
    info "Portable .exe verified — unpacks to %LOCALAPPDATA%\\vv\\$STAGE_NAME and launches OK"
  fi
fi

# ── extract-and-run folder zip (opt-in) ────────────────────────────────
if [ "$DO_ZIP" = 1 ]; then
  out_win="$(cygpath -w "$OUT_ABS/$STAGE_NAME.zip")"
  (cd stage && powershell -Command "Compress-Archive -Force -Path '$STAGE_NAME' -DestinationPath '$out_win'")
  info "Wrote $OUT_DIR/$STAGE_NAME.zip"
fi

info "Done."
