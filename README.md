# Simple vtk viewer

## Requirements

- Install [vcpkg](https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started?pivots=shell-bash)
- Ensure `VCPKG_ROOT` is set (usually to `$HOME/vcpkg`)

## Build (with CMake presets)

Choose the appropriate preset for your platform and configuration (e.g., `macos-debug`, `macos-release`, `linux-debug`, `linux-release`, `win-x64-debug`, `win-x64-release`).

Debug:

```sh
cmake --preset=<your-debug-preset>
cmake --build --preset=<your-debug-preset>
```

Release:

```sh
cmake --preset=<your-release-preset>
cmake --build --preset=<your-release-preset>
```

## Install

Copy the built binary to your local bin directory:

```sh
cp ./out/build/<your-preset>/vv ~/.local/bin/
```

Make sure `~/.local/bin` is in your PATH.
