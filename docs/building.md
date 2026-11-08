# Building and installing vv

[README](../README.md) · [Usage](usage.md) · [Contributing](../CONTRIBUTING.md)

## Requirements

- A C++17 compiler and CMake 3.14 or later. The supplied presets require CMake 3.25 or later and Ninja.
- VTK development libraries with `GUISupportQt`, OpenGL rendering and `IOHDF` support. Available VTKHDF features depend on the installed VTK version.
- Qt Widgets matching VTK's Qt build. CMake follows VTK's imported Qt5 or Qt6 target; tests also require Qt Test.
- HDF5 C libraries, using VTK's HDF5 target when available and a system package otherwise.

CMake uses installed `cxxopts` and `nlohmann_json` packages when found, or fetches the revisions declared in [CMakeLists.txt](../CMakeLists.txt). Fetching requires Git and network access. Configure with `-DVV_FETCH_DEPS=OFF` to require installed packages.

## Platform setup

### Linux

The Debian/Ubuntu dependency set used by CI includes Qt5 and VTK's Qt integration:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config \
  qtbase5-dev libcxxopts-dev nlohmann-json3-dev \
  libvtk9-dev libvtk9-qt-dev libgl-dev libglx-dev libxt-dev
```

Continue with [configure, build, and run](#configure-build-and-run). Static-analysis tools are listed in [contributing](../CONTRIBUTING.md#tests-and-checks).

### macOS

Install the Homebrew dependencies used by CI, then configure their search paths:

```sh
brew install cmake ninja vtk qtbase cxxopts nlohmann-json
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix vtk)"
```

Build with `cmake --build build` and run `./build/vv mesh.vtk`. Intel Macs can build from source; release packages target Apple Silicon.

### Windows

#### MSYS2 / MinGW

CI and release builds use the **MSYS2 MINGW64** shell. Install its dependencies:

```sh
pacman -S --needed \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-qt6-base mingw-w64-x86_64-vtk \
  mingw-w64-x86_64-fast_float mingw-w64-x86_64-hdf5 \
  mingw-w64-x86_64-cxxopts mingw-w64-x86_64-nlohmann-json
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
./build/vv.exe mesh.vtk
```

The `win-x64-*` presets select MSVC, so use the explicit configure command above for MinGW.

#### MSVC / vcpkg

This route is available through retained presets and is not covered by current CI. Install Visual Studio C++ Build Tools, CMake, Ninja and Git. Use **Developer PowerShell** or the **x64 Native Tools Command Prompt**, which initialize the compiler's environment.

The presets expect vcpkg at `%USERPROFILE%\vcpkg` with the `x64-windows` triplet. From Developer PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
cmake --preset=win-x64-release
cmake --build --preset=win-x64-release
.\build\vv.exe mesh.vtk
```

The [vcpkg manifest](../vcpkg.json) supplies dependencies. For Qt6 builds, CMake copies vcpkg's Qt plugins beside `vv.exe`; without vcpkg it invokes `windeployqt` when available. The vcpkg toolchain also deploys dependent DLLs. `-DVV_QT_WINDOWS_DEPLOY=OFF` disables vv's Qt deployment step.

## Configure, build, and run

From the repository root on macOS/Linux, the helper configures and builds a Release binary in `build`:

```sh
./build.sh
./build/vv mesh.vtk
```

Use `./build.sh -t Debug` for Debug or `./build.sh -d build/dev` for a separate build directory. The compilation database is `<build-dir>/compile_commands.json`. `./build.sh --help` owns the complete helper flag reference; `--clean` only removes a CMake build directory configured from this source tree and refuses the source directory or its ancestors.

Alternatively, use a preset from [CMakePresets.json](../CMakePresets.json):

```sh
cmake --preset=linux-debug
cmake --build --preset=linux-debug
./build/debug/vv mesh.vtk
```

Replace `linux-debug` with `macos-debug`, or use a `*-release` preset. Release presets write to `build`; Debug presets write to `build/debug`. The same paths apply to the Windows MSVC presets, with `vv.exe` as the executable.

For manual configuration without presets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Pass `-DNAME=VALUE` to the configure command, not `cmake --build`. These project options are declared in [CMakeLists.txt](../CMakeLists.txt):

| Option                  | Default | Purpose                                                      |
| ----------------------- | ------- | ------------------------------------------------------------ |
| `VV_FETCH_DEPS`         | `ON`    | Fetch missing cxxopts and nlohmann/json dependencies.        |
| `VV_ENABLE_WARNINGS`    | `ON`    | Enable strict compiler warnings.                             |
| `VV_WARNINGS_AS_ERRORS` | `ON`    | Treat enabled compiler warnings as errors.                   |
| `VV_QT_WINDOWS_DEPLOY`  | `ON`    | Deploy Qt plugins/runtime after linking on Windows with Qt6. |
| `VV_ENABLE_SANITIZERS`  | `OFF`   | Enable ASan/UBSan with GCC or Clang on macOS/Linux.          |
| `BUILD_TESTING`         | `OFF`   | Build and register regression tests; also build benchmarks.  |

## Local installation

On macOS/Linux, build and copy the binary to `~/.local/bin`:

```sh
./build.sh --install
```

Set `INSTALL_DIR` to choose another binary directory, and ensure it is on your `PATH`. This source installation uses your existing runtime libraries; use the [release packages](../README.md#install) for bundled dependencies.

## Troubleshooting

- **VTK not found:** ensure the Qt-enabled VTK development package is installed. For a custom installation, configure `-DVTK_DIR=/path/to/vtk/cmake` or `-DCMAKE_PREFIX_PATH=/prefix`. Qt must match the version used to build VTK.
- **MSVC compiler not found:** start a Visual Studio developer shell. Adding only `cl.exe` to `PATH` does not initialize its include and library paths.
- **Windows Qt platform plugin missing:** check the post-build deployment output and the plugins beside `vv.exe`. For normal vcpkg development, `QT_PLUGIN_PATH` should not be needed.

## Release packaging

Tags matching `v*` trigger the [release workflow](../.github/workflows/release.yml), which can also be dispatched manually. It produces:

| Platform            | Packaging                                                                                                                                                |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux x86_64        | AppImage via linuxdeploy and its Qt plugin; a separate `-bin.tar.gz` contains the bare binary.                                                           |
| macOS Apple Silicon | `macdeployqt` bundles dependencies into a tar archive laid out for `~/.local`: `bin/vv` links to `lib/vv/bin/vv`, beside bundled frameworks and plugins. |
| Windows x86_64      | [package-windows.sh](../scripts/package-windows.sh) stages dependencies, smoke-tests the bundle and builds a portable executable with NSIS.              |

Run `scripts/package-windows.sh --help` from MINGW64 for local packaging options. End-user package selection and installation are in the [README](../README.md#install).

The [Docker workflow](../.github/workflows/docker.yml) separately builds a Linux image from the [Dockerfile](../Dockerfile) and publishes it to GHCR outside pull requests. The image builds vv against Ubuntu's Qt5/VTK packages; it does not configure GUI display forwarding or produce the release artifacts above.
