#!/bin/sh
set -e

run() {
  CYAN='\033[0;36m'
  YELLOW='\033[1;33m'
  NC='\033[0m'
  printf "${CYAN}>>${YELLOW} %s${NC}\n" "$*"
  eval "$@"
}

export VCPKG_ROOT=$(dirname "$(which vcpkg)")
INSTALL_DIR=${INSTALL_DIR:-~/.local/bin}
run "mkdir -p $INSTALL_DIR"

# Detect platform and set preset
if [ "$(uname)" = "Darwin" ]; then
  PRESET=macos-release
elif [ "$(uname)" = "Linux" ]; then
  PRESET=linux-release
else
  echo "Unsupported platform: $(uname)"
  exit 1
fi

run "cmake --preset=$PRESET"
run "cmake --build --preset=$PRESET"
run "cp ./build/vv $INSTALL_DIR"

echo
echo "Installed vv to $INSTALL_DIR (make sure it's in your PATH)"
