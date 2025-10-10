# Simple vtk viewer

## Requirements

### System Dependencies

For Debian/Ubuntu distributions:

```sh
sudo apt install -y curl zip unzip tar pkg-config libxmu-dev libxi-dev libgl-dev libxt-dev libglvnd-dev libopengl-dev libglx-dev libegl-dev cmake ninja-build
```

### vcpkg

- Install [vcpkg](https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started?pivots=shell-bash)
- Ensure `VCPKG_ROOT` is set (usually to `$HOME/vcpkg`)

## Build (with CMake presets)

Choose the appropriate preset for your platform and configuration (e.g., `macos-debug`, `macos-release`, `linux-debug`, `linux-release`, `win-x64-debug`, `win-x64-release`).

```sh
PRESET=your-preset
cmake --preset=$PRESET
cmake --build --preset=$PRESET  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cp build/compile_commands.json .  # Needed for C/C++ autocompletion and symbol search in VSCode
```

To analyze your code for common issues and style problems, use:

```sh
cppcheck  --check-level=exhaustive  --enable=warning,style,performance,portability,unusedFunction --project=build/compile_commands.json
```

## Install

No need to build (already done in install script).

```sh
vcpkg install  # Downloads and builds dependencies
./install.sh
```

Make sure `~/.local/bin` is in your PATH.
