# Simple vtk viewer

## Requirements

### System Dependencies

For Debian/Ubuntu distributions:

```sh
sudo apt install -y curl zip unzip tar pkg-config cmake ninja-build libvtk9-dev libxmu-dev libxi-dev libgl-dev libxt-dev libglvnd-dev libopengl-dev libglx-dev libegl-dev
```

`fmt` and `cxxopts` are fetched automatically by CMake when not installed.
`VTK` must be available on the system so `find_package(VTK)` can resolve it.

## Build and run

The quickest path on macOS/Linux is the helper script, which configures, builds,
and (optionally) runs static analysis and installs:

```sh
./build.sh                 # Release build into ./build
./build.sh -t Debug        # Debug build
./build.sh --analyze       # build, then cppcheck + clang-tidy
./build.sh --install       # build, then install the app/binary
./build.sh --help          # all options
```

`build.sh` always emits `build/compile_commands.json` for tooling.

### Using CMake presets directly

Choose the appropriate preset for your platform and configuration (e.g., `macos-debug`, `macos-release`, `linux-debug`, `linux-release`, `win-x64-debug`, `win-x64-release`).

```sh
PRESET=your-preset
cmake --preset=$PRESET
cmake --build --preset=$PRESET
```

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set in the presets. Pass CMake `-D...` options to the configure step (`cmake --preset=... -DNAME=VALUE`), not the build step (`cmake --build ...`).

The project builds a single Qt-based executable: `vv`. On macOS, CMake packages it as an app bundle, so the binary is inside `vv.app`.

Run on macOS:

```sh
./build/vv.app/Contents/MacOS/vv /path/to/mesh.json
```

Run on Linux or Windows:

```sh
./build/vv /path/to/mesh.json
```

Required UI dependencies are Qt6 Widgets and `VTK::GUISupportQt`.

### Scalar fields

Press **Space** to cycle through the available scalar fields (and back to plain
geometry). Both **point-data** and **cell-data** scalars are supported; cell
fields are labelled `… (cells)` in the colorbar title. Categorical integer
fields (2–20 distinct values) get a discrete tab10/tab20 colormap; continuous
fields get a draggable clip range. Use `-e/--explode` to show every field at
once in a synchronized facet grid.

## Quality checks

Strict warnings are enabled by default and treated as errors. For local checks, configure and build the preset you use:

```sh
cmake --preset=linux-debug
cmake --build --preset=linux-debug
```

Run formatter through CMake when `clang-format` is installed:

```sh
cmake --build --preset=linux-debug --target format
git diff --check
```

### Static analysis

`build.sh` wraps cppcheck and clang-tidy with the right flags and a curated
suppressions list (`.cppcheck-suppressions`) that filters out Qt `moc`/autogen
false positives:

```sh
./build.sh --cppcheck      # cppcheck only
./build.sh --clang-tidy    # clang-tidy only
./build.sh --analyze       # both
./build.sh --format-check  # fail if clang-format would change anything
```

To invoke cppcheck by hand against a build directory:

```sh
cppcheck --check-level=exhaustive \
  --enable=warning,style,performance,portability \
  --suppressions-list=.cppcheck-suppressions \
  --inline-suppr -i build --library=qt \
  --project=build/compile_commands.json --error-exitcode=1
```

### For Windows

One-time installs (elevate if `winget` asks):

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Git.Git
```

#### vcpkg (clone + `PATH`)

```powershell
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
```

Add `%USERPROFILE%\vcpkg` to your **user** `Path` (Settings → Environment Variables), or for the current session only:

```powershell
$vpkg = "$env:USERPROFILE\vcpkg"
$env:Path += ";$vpkg"
```

If you change **user** `Path`, **open a new terminal** before `vcpkg` is recognized (existing windows still have the old `PATH`). To refresh `PATH` in the current PowerShell without restarting:

```powershell
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
```

#### MSVC `cl.exe` (not the same as vcpkg)

`cl` is not installed as a single global binary. The C++ toolchain sets `PATH`, `INCLUDE`, `LIB`, and related variables via **Visual Studio’s developer environment**. Putting only the folder that contains `cl.exe` on `PATH` is not enough for real builds.

Use one of these:

- **Easiest:** Start **Developer PowerShell for VS 2022** (or **x64 Native Tools Command Prompt**) from the Start menu, then run CMake from that window.
- **Same PowerShell window:** load the environment for this session (adjust if you use Build Tools and `vswhere` finds nothing—install the **Desktop development with C++** / **VC Tools** workload):

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"
```

After that, `cl`, `cmake`, `ninja`, and `cmake --preset win-x64-release` see the compiler.

Optionally add the `Enter-VsDevShell` block to your **PowerShell profile** if you want `cl` in every new shell; it increases profile load time. Do **not** add `bootstrap-vcpkg.bat` to your profile (run bootstrap only when installing or updating vcpkg).

#### Sanity checks

```powershell
cl
cmake --version
ninja --version
vcpkg version
```

On **Windows**, if you use **vcpkg**, CMake copies **`Qt6/plugins`** from the installed triplet into **`plugins/`** next to **`vv.exe`** after each link (vcpkg’s layout is not compatible with **`windeployqt`**). Without vcpkg, CMake runs **`windeployqt`** instead. The vcpkg toolchain also copies dependent DLLs beside the executable. Disable with **`VV_QT_WINDOWS_DEPLOY=OFF`** if needed. You should not need **`QT_PLUGIN_PATH`** for normal development.

## Install

```sh
./build.sh --install
```

On macOS this copies `vv.app` to `~/Applications`, symlinks the CLI into
`~/.local/bin`, and registers the Quick Look generator. On Linux it copies the
`vv` binary to `~/.local/bin`. Override locations with the `INSTALL_DIR` and
`APP_INSTALL_DIR` environment variables. Make sure `~/.local/bin` is in your PATH.

## Releases / packaging

Tagged pushes (`v*`) trigger `.github/workflows/release.yml`, which builds
**self-contained** bundles with Qt and VTK included, so end users need nothing
preinstalled:

- **Linux** — `.AppImage` (via `linuxdeploy` + the Qt plugin)
- **macOS** — zipped `vv.app` (via `macdeployqt`)
- **Windows** — `.zip` with `vv.exe`, Qt plugins, and the linked MinGW/VTK DLLs

Bare-binary archives are also published for users who already have the runtime
from a package manager.
