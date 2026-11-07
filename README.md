# Simple vtk viewer

## Requirements

### System Dependencies

For Debian/Ubuntu distributions:

```sh
sudo apt install -y curl zip unzip tar pkg-config cmake ninja-build libvtk9-dev libxmu-dev libxi-dev libgl-dev libxt-dev libglvnd-dev libopengl-dev libglx-dev libegl-dev
```

`cxxopts` and `nlohmann_json` are fetched automatically by CMake when not installed. Set `VV_FETCH_DEPS=OFF` to require installed packages.
`VTK` must be available on the system so `find_package(VTK)` can resolve it.

## Build and run

The quickest path on macOS/Linux is the helper script, which configures, builds,
and (optionally) runs static analysis and installs:

```sh
./build.sh                 # Release build into ./build
./build.sh -t Debug        # Debug build
./build.sh --analyze       # build, then cppcheck + clang-tidy
./build.sh --install       # build, then install the binary
./build.sh --help          # all options
```

`build.sh` always emits `build/compile_commands.json` for tooling.

### Using CMake presets directly

Presets require CMake 3.25 or later; manual configuration requires CMake 3.14 or later. Choose the appropriate preset for your platform and configuration (e.g., `macos-debug`, `macos-release`, `linux-debug`, `linux-release`, `win-x64-debug`, `win-x64-release`).

```sh
PRESET=your-preset
cmake --preset=$PRESET
cmake --build --preset=$PRESET
```

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set in the presets. Pass CMake `-D...` options to the configure step (`cmake --preset=... -DNAME=VALUE`), not the build step (`cmake --build ...`).

The project builds a single Qt-based executable, `vv`, from a Qt-free `vv_mesh` static library (parsers and loading), a VTK-free `vv_ui` static library (the overlay widgets) and the viewer sources.

```sh
./build/vv /path/to/mesh.json
```

Required UI dependencies are Qt6 Widgets and `VTK::GUISupportQt`.

### Several files

```sh
vv foo.vtk bar.stl       # side by side, one panel per file
vv /data/case/*.vtk      # every matching file (the shell expands the glob)
vv -e foo.vtk bar.vtp    # exploded: a row per file, a column per scalar
```

Several files open in a grid, one panel per file, identified by the same letter badge as Layers, all sharing one camera so rotation and zoom stay in step and meshes in a common coordinate frame keep their relative positions. The column count adapts to the window size to maximize usable panel space, preferring fewer empty cells when layouts fit equally well. **Space** cycles the union of the files' scalars: a field present in several files uses one color range and one legend, and a file without it is drawn in its plain part color.

With `-e`, each file gets a row and each scalar a column, so the same field lines up vertically across files with one color range per column; dragging a column's clip range applies to the whole column. Each panel has its file's colored letter badge, with the filename beside the first visible badge in each row. A panel whose file lacks that column's field is drawn plain with a `No field` note. A single file uses an adaptive grid of its fields. Click a continuous legend's gradient, or right-click its card, to choose or reverse its colormap for the whole column. Each panel's legend fits its panel: the full card in a large panel; a compact card in a medium one, with the field's name and kind on one line and the bar between its clip values or the categories as chips; and a strip of the bar between its values, or of the swatches, in a small one, with the name in its tooltip. Categorical cards switch to chips when their rows would take too much height. A `+N` chip stands for the categories that do not fit; hover to list them or click to edit a hidden category. `--annotate` needs a single file.

When the fields do not all fit with each panel at least about 280 × 220 points, the grid shows a page of them at a time: up to 9 fields of a single file, or with several files a row per file and as many field columns as fit across. A pager at the bottom says which fields show (`Fields 1–7 of 13`) and steps through the pages, as do **Space** and **Shift+Space** forward and **Ctrl+Space** (**Option+Space** on macOS) back; click its name for the list of every field and pick one to show its page. Each field keeps its clip range and category colors, and the camera its view, from page to page.

### Tool rail

The rail on the left edge of the view holds the viewer's tools; hover a button for its name and shortcut.

- **Layers** (**L**) lists the files, each with a letter badge also shown on its panels and beside its name on the probe card; a file with several parts lists them under it, behind a chevron. Click an eye to hide or show a file or part; **Option**-click it (**Alt**-click on Linux and Windows) to show only that one, and again to show everything. Right-click a row for Show only this and Show all, which is also at the top of the panel.
- **Color** (**C**) opens the colors of the active field. For a continuous field, the colormaps, grouped Sequential, Diverging and Cyclic; click one to apply it, or tick Reverse to run it from the high end to the low end. Its Range section shows a histogram of the field's values with the clip window's handles, which move with the legend's, plus Min and Max fields, Use data range, and Outside range: Clamp keeps the colormap's end colors past the window, Gray draws those values gray. For a categorical field, the palettes, each tile showing the colors the field's first categories take in it, and the categories: click one to pick its color as from the legend.
- **Style** (**S**) picks how every mesh is drawn, in the single view and in each facet: Solid (**1**), Solid with edges (**2**) and Wireframe (**3**), or the see-through X-ray (**X**) and Glass (**G**). X-ray draws the surfaces translucent in their field or part colors, blended in depth order; its Opacity slider sets how much. Glass is a see-through shell with smooth shading, a bright rim where the surface turns away, glossy highlights and white outlines along open borders, so inner and back surfaces show through; its Density slider sets how opaque the shell is away from the rim. Each slider shows with its style and keeps its value. The digits pick a style while the panel is open; **X** and **G** toggle X-ray and Glass from anywhere, back to the previous opaque style. The style stays as fields change and frames play.
- **Probe** (**P**) reads the field under the pointer: hover a mesh for a card with the active field's value there (interpolated inside the cell for point fields, with the legend's swatch for a category, `no value` for NaN), the cell id, and the file or part name when there are several. Matching locations show a card in each visible mesh view, including across files and `-e` field panels; each card reads that view’s own field and cell. Matching uses world coordinates within numerical tolerance, not cell numbers alone. The pointer turns into a crosshair, and Probe stays on while a panel is open. Click a mesh to pin the probe there: a white marker stays on the point and the card stays beside it, following the camera, the frames and the field shown; press **Esc**, click the card’s **×**, click empty space or turn Probe off to unpin. **Esc** closes an open panel first.
- **Annotate** (**B**) paints integer labels on the mesh's cells; see [Annotate](#annotate).
- **Vector glyphs** (**V**) steps through the 3-component arrays drawn as glyphs, then off, naming each in a brief notice at the top of the view.
- **Reset view** (**R**) returns to the opening camera.
- **Save snapshot** writes the view, mesh identities and legends to a PNG, leaving out the rail and other controls; a brief notice at the top of the view says where.

Only one of the Layers, Color and Style panels is open at a time. **Esc** closes it, and so does a press anywhere outside it and the rail, which still does its own job: a drag on the view rotates, a click on the pager steps. A button that does not apply, such as Layers or Color with `-e`, or Vector glyphs without 3-component arrays, is dimmed and its tooltip says why.

### Command palette

Press **⌘K** (**Ctrl+K** on Linux and Windows) to search everything at once: the fields, the colormaps of the active continuous field or the palettes of the active categorical field, and the commands, i.e. the rail's tools, X-ray and Glass, each with its shortcut. Matches are grouped and their matching letters highlighted; **↑**, **↓** and **Return** or a click run one, **Esc** or **⌘K** again closes the palette. A field shows as with **Space**, a colormap or palette applies as from the Color panel, and a command that does not apply is greyed out with its tooltip saying why.

### Annotate

Press **B**, or the rail's Annotate button, to paint integer labels on the mesh's cells, e.g. to mark regions; `vv -a mesh.vtk` (`--annotate`) starts with it on. The labels go into a per-cell `label` array (a file's own integer `label` array is edited in place), and the mesh shows them instead of the field, unlabeled cells (0) in gray. The bar at the top of the view holds the tools:

- **Rotate**, **Paint** and **Erase** set what a left drag does. Holding **A** paints and holding **E** erases whichever tool is set, and releasing the key goes back to it; with Paint or Erase, a right drag erases.
- The swatches **1** to **9** pick the label, as do the keys **1** to **9**.
- The brush paints the cell under the pointer plus as many rings of neighboring cells as the bar shows; rings follow the surface, so paint never jumps to a nearby sheet. **+** and **−** resize it, and a faded overlay shows what a click would paint.
- **Undo** (**Ctrl+Z**, **⌘Z** on macOS) reverts the last stroke, up to 50 strokes back.
- **Save as…** writes the mesh with its labels to `.vtp` (polydata) or `.vtu` (unstructured grid).
- The close button, **Esc** (once no panel or probe pin is active) or **B** leaves annotation and shows the field again. The labels stay, so entering again continues where you left off.

A strip at the bottom of the view lists the keys. While annotating, the digits pick labels instead of styles, **Space** keeps the labels on view and Color is unavailable; Probe reads the labels while Rotate is set. Closing vv or pressing **Q** with unsaved labels asks whether to save them, discard them or stay. Annotation needs a single file outside `-e`, and only the file's first part takes labels. It is unavailable for a time series, whose frames replace the mesh's arrays.

### Time series

A time series (temporal VTKHDF or XDMF) adds a playback bar along the bottom of the view: the field pager at its left, then previous frame, play or pause and next frame, the timeline, a speed menu from 1× to 12× and Loop, which stops at the last frame when off. The timeline shows the frame's time and `frame n of N` over a track with round time ticks; click or drag the track to seek. Pin a point with the probe and the timeline traces the shown field there across every frame (`vm at cell 5027`), in the accent up to the playhead and gray after it, scaled to the legend's range; the trace fills in a few frames at a time, coarse to fine, while playback and the camera stay responsive, and starts again for another field or point. **Space** plays and pauses, **←** and **→** step a frame, and **Shift+Space** steps to the next field (**Ctrl+Space**, **Option+Space** on macOS, to the previous one). In a narrow window the ticks go first, then the frame number.

### Scalar fields

Press **Space** to cycle through the available scalar fields (and back to plain geometry), or **Ctrl+Space** (**Option+Space** on macOS) to step back; in a time series, where **Space** plays, **Shift+Space** steps forward. The pager at the bottom of the view, at the left of the playback bar in a time series, shows the active field, its kind under it as the legend words it (e.g. `Points · continuous`) and its position in a badge (e.g. `1 / 12`), and its previous and next buttons step the same way. Click the field's name for a list of every field: type to narrow it, then **↑**, **↓** and **Return** or a click pick one, and **Esc** closes it. Both **point-data** and **cell-data** scalars are supported; the pager and the legend say Points or Cells under the field name. Nodal fields are interpolated across each triangle, cell fields stay flat per triangle. Categorical integer fields (2–20 distinct values) get a color per category from a palette; continuous fields get a clip range: drag a handle on the legend's bar, double-click a clip value to edit it, or set it in the Color panel's Range section. Legend values round to the data range's scale while retaining at least two significant digits for nonzero bounds, with precise values on hover and when editing. Typed bounds retain their meaningful digits until dragged or reset with Use data range. Use `-e/--explode` to show every field at once in a facet grid sharing one camera.

A categorical field starts on the Default palette: the colors the file gives its categories, or else ten colors chosen to read on the dark view. File colors come from a legacy `.vtk` `LOOKUP_TABLE` section attached to the field (VTK attaches it to the first `SCALARS` block of the point or cell data only), where each category takes the entry its value indexes when every value is a whole number inside the table, and the entries in order otherwise, and from a JSON mesh painter's colors. The Color panel, the legend's Palette menu and the command palette also offer Tab10, Tab20, Okabe-Ito (colorblind-safe), Set2, Paired and Dark2; past a palette's last color, its colors come round again, lighter, then darker.

Click a category's swatch in the legend, or right-click its row, to pick another color for it, e.g. when two categories are hard to tell apart, or to reset it to its palette color; "More colors…" opens a full color picker. A picked color makes the field's palette Custom: the palette's colors with yours over them. Picking another palette keeps them under Custom until you recolor a category again, which starts a new Custom over the palette shown. Palette and colors stay for that field until vv exits; with `-e` they apply to the whole column. `-C/--common-cat-lut` gives every categorical field the union of their categories, so a value takes the same color in every field on the same palette; with it, Default draws vv's ten colors rather than a file's table, which belongs to one field.

`-r/--range min,max` pins the color range of every continuous field, in the
single view and in every facet, e.g. `--range -80,40` for a transmembrane
potential in mV. Without it, a time series uses the range sampled across the
whole animation and a static file the field's own range; either way the range is
fixed for the whole animation, never per frame.

Press **c**, or the rail's Color button, to pick the colormap of the active continuous field: rainbow (the default), viridis, magma, inferno, plasma, cividis and turbo; the diverging coolwarm and RdBu (blue low, red high); and the cyclic hue wheel and twilight, whose two ends share a color. The legend follows, and the choice stays for that field until vv exits. Fields whose name contains `phase` start on the hue wheel. A wrapping field usually also wants its cycle length pinned, e.g. `--range 0,260`.

Press **v**, or the rail's Vector glyphs button, to cycle vector-field glyphs: every 3-component point or cell array is drawn as a short segment per tuple, oriented along the vector (fibre orientation, and any other direction field).

## Quality checks

Strict warnings are enabled by default and treated as errors. For local checks, configure and build the preset you use:

```sh
cmake --preset=linux-debug
cmake --build --preset=linux-debug
```

Run formatter through CMake when `clang-format` is installed. CMake searches for it normally, then tries `xcrun --find clang-format` on macOS. Set `CLANG_FORMAT_EXE` to use a specific executable.

```sh
cmake --build --preset=linux-debug --target format
cmake --build --preset=linux-debug --target format-check
```

`format-check` reports formatting errors without changing files or inspecting unrelated Git changes. The formatter used by CI is authoritative; use `CLANG_FORMAT_EXE` to select the same version locally when results differ.

Enable regression tests explicitly. The overlay widget tests use the Qt Test module, which ships with Qt's base package, and run on Qt's offscreen platform, so they need no display:

```sh
cmake -S . -B build/tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
(cd build/tests && ctest --output-on-failure)
```

On macOS/Linux with GCC or Clang, add `-DVV_ENABLE_SANITIZERS=ON` to the configure command to check for memory errors and undefined behavior while running the tests or viewer. Sanitizers are disabled by default.

### Performance checks

PRs run warning-clean builds and regression tests on Linux, macOS and Windows, including the actual viewer's annotation, probe, visibility and column-coloring wiring. Tests assert one render submission per column edit or bulk visibility action, independently of the machine's speed. Formatting gates the platform jobs first; broad advisory clang-tidy runs stay off the ordinary PR path.

Each platform records median CPU setup/interaction timings and attempts an OpenGL frame benchmark. Linux requires the software OpenGL run under Xvfb. Native runners without an OpenGL 3.2 context report rendering as unavailable, rather than recording substitute timings. Benchmark artifacts and job summaries identify the commit, OS, architecture, Qt/VTK versions and graphics driver. Frame measurements cover nested transparent shells in one and four viewports, Solid, X-ray and Glass, both at rest and during rotation; they include style changes and their first frame, wait for GPU completion and reject empty frames.

With tests enabled, run the same bounded workloads locally:

```sh
mkdir -p benchmark-results
QT_QPA_PLATFORM=offscreen build/tests/vv_viewer_tests benchmarkFacetSetup -iterations 5 -median 5 -o benchmark-results/cpu-setup.xml,xml -o -,txt
QT_QPA_PLATFORM=offscreen build/tests/vv_viewer_tests benchmarkColumnInteraction -iterations 1000 -median 5 -o benchmark-results/cpu-interaction.xml,xml -o -,txt
build/tests/vv_render_benchmark --require-rendering --output benchmark-results/render.json
python3 scripts/benchmark-summary.py benchmark-results
```

Compare timings with previous commits on the same OS and renderer. Shared runners and software graphics are useful regression evidence, but their frame timings are not end-to-end input latency or guarantees for a user's GPU. CI keeps the measurements as seven-day artifacts; correctness, nonempty rendering and bounded render submissions are gates, while hardware-dependent timing values remain observations.

### Static analysis

`build.sh` wraps cppcheck and clang-tidy with the right flags and a curated
suppressions list (`.cppcheck-suppressions`) that filters out Qt `moc`/autogen
false positives:

```sh
./build.sh --cppcheck           # cppcheck only
./build.sh --clang-tidy         # the full .clang-tidy configuration
./build.sh --clang-tidy-check   # the correctness subset enforced in CI
./build.sh --analyze            # cppcheck and the full clang-tidy run
./build.sh --format-check       # fail if clang-format would change anything
```

CI gates cppcheck and a small clang-tidy subset (static analyzer plus selected bugprone checks) whose findings are stable across clang-tidy releases. The full `.clang-tidy` configuration, including modernize and readability checks, runs advisory on manually dispatched CI runs because new releases add checks; run `--clang-tidy` locally before relying on it. `--clean` only removes a CMake build directory configured from this source tree and refuses the source directory or its ancestors.

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

This copies the `vv` binary to `~/.local/bin` (override with `INSTALL_DIR`).
Make sure `~/.local/bin` is in your PATH.

## Releases / packaging

Tagged pushes (`v*`) trigger `.github/workflows/release.yml`, which builds
**self-contained** packages with Qt and VTK included, so end users need nothing
preinstalled:

- **Linux** — `.AppImage` (via `linuxdeploy` + the Qt plugin)
- **macOS** (Apple Silicon) — `vv-<version>-aarch64-apple-darwin.tar.gz` (via `macdeployqt`), laid
  out for `~/.local`: `bin/vv` links to `lib/vv/bin/vv`, next to the bundled Qt/VTK
  libraries. Install with `tar -xzf vv-*-apple-darwin.tar.gz -C ~/.local`. Intel
  Macs are not packaged (Homebrew no longer ships Intel bottles); build from source. The
  binaries are not notarized: if macOS refuses to run them (e.g. after extracting
  with Finder), run `xattr -dr com.apple.quarantine ~/.local/lib/vv`.
- **Windows** — portable `.exe` (via `scripts/package-windows.sh`; unpacks to `%LOCALAPPDATA%\vv\` on first run)

A bare Linux binary (`-bin.tar.gz`) is also published for users who already
have the runtime from a package manager.
