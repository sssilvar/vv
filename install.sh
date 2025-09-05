#!/bin/sh
set -e

# Detect platform and set preset
if [ "$(uname)" = "Darwin" ]; then
  PRESET=macos-release
elif [ "$(uname)" = "Linux" ]; then
  PRESET=linux-release
else
  echo "Unsupported platform: $(uname)"
  exit 1
fi


cmake --preset=$PRESET
cmake --build --preset=$PRESET
cp ./out/build/$PRESET/vv ~/.local/bin/

echo "Installed vv to ~/.local/bin (make sure it's in your PATH)"
