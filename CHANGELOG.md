# [Unreleased]

### Added

- Cross-platform viewer integration tests, median CPU interaction benchmarks and OpenGL frame benchmarks with driver metadata, p50/p95 timings and nonempty-frame checks.

- Optional regression tests for CLI validation, mesh color metadata, stdin I/O failures and build-helper safety, plus ASan/UBSan builds on macOS/Linux.
- A non-mutating CMake `format-check` target and a clang-tidy correctness gate in CI.
- Qt Test checks for the overlay widgets, which build as a VTK-free `vv_ui` library and run on Qt's offscreen platform.
- Click a category's swatch in the legend, or right-click its row, to change its color, reset it to its palette color or pick the field's palette; with `-e` the whole column follows.
- A tool rail on the left of the view: Layers (**L**), Vector glyphs (**V**), Reset view (**R**) and Save snapshot, which writes the view and its legends to a PNG.
- A pager at the bottom of the view shows the active scalar field and steps through the fields; **Ctrl+Space** (**Option+Space** on macOS) steps back.
- A Color panel on the tool rail (**C**) picks the active continuous field's colormap from sequential (rainbow, viridis, magma, inferno, plasma, cividis, turbo), diverging (coolwarm, RdBu) and cyclic (hue wheel, twilight) maps, optionally reversed, or the active categorical field's palette and category colors; the legend follows and each field keeps its choice.
- Categorical fields take a palette: Default, the file's own colors (a legacy `.vtk` `LOOKUP_TABLE` attached to the field, or JSON mesh painter colors) or else ten colors chosen for the dark view, then Tab10, Tab20, Okabe-Ito, Set2, Paired and Dark2. A recolored category makes the field's palette Custom, which stays available after picking another palette.
- The Color panel's Range section: a histogram of the active field with draggable clip handles kept in step with the legend, Min and Max fields, Use data range, and Outside range to draw values past the clip window in the end colors (Clamp) or gray, kept per field.
- A Style panel on the tool rail (**S**): Solid (**1**), Solid with edges (**2**), Wireframe (**3**), X-ray (**X**, with an opacity slider) or Glass (**G**, with a density slider), for every mesh in the single view and the facet grid. X-ray draws the surfaces translucent in their colors, blended in depth order. Glass draws a see-through shell with smooth shading, a white fresnel rim, glossy highlights and white outlines along open borders.
- Pinned probes can be released with Escape or the card’s close button, keeping Probe enabled for hover readings.
- Hovered and pinned probes show corresponding readings across matching mesh files and exploded field views, using each view’s own field and cell IDs.
- Probe on the tool rail (**P**): hover a mesh to read the field's value under the pointer, interpolated inside the cell for point fields, with the cell id and, with several files or parts, the hovered one's name; each `-e` panel reads its own field.
- Annotate on the tool rail (**B**) enters and leaves annotation while vv runs, keeping the painted labels and showing the field again on leaving; `--annotate` starts with it on. While annotating, **1** to **9** pick the label, a strip at the bottom lists the keys, and closing vv or pressing **Q** with unsaved labels asks to save or discard them.
- **Esc** closes the open rail panel, releases a pinned probe, or leaves annotation, in that order.
- A press outside the open rail panel and the rail closes the panel and still does its own job, e.g. a drag on the view rotates it.
- Click the field pager's name for a list of every field with its kind, narrowed as you type; **↑**, **↓** and **Return** or a click pick one, **Esc** closes it.
- A command palette (**⌘K**, **Ctrl+K** on Linux and Windows) searches the fields, the active field's colormaps or palettes and the commands with their shortcuts, highlighting what matches; commands that do not apply are greyed out.
- With `-e`, fields that do not fit at a usable panel size show a page at a time (up to 9 of one file, or a row per file with as many field columns as fit), with a pager at the bottom (`Fields 1–7 of 13`) whose list jumps to any field's page; **Space**, **Shift+Space** and **Ctrl+Space** (**Option+Space** on macOS) step through the pages, and clip ranges, category colors and the camera stay as pages change.
- Click a mesh with Probe on to pin it: a marker and the card stay at the point, and in a time series the playback bar's timeline traces the shown field there across every frame, filled a few frames at a time while vv stays responsive.

### Changed

- Continuous legend values use range-aware rounding with at least two significant digits for nonzero bounds and trimmed trailing zeros; typed bounds keep their meaningful digits until dragged or reset, and hover and editing retain precise values.

- Window titles lead with the filename, summarize comparisons with the first filename and remaining count, and name standard input explicitly; single-file windows expose the full path through native document metadata.
- Popup menus use inset rounded highlights, aligned Lucide checkmarks and submenu chevrons, and consistent row and separator spacing; selected colormaps and palettes remain highlighted.
- Facet columns compute shared analysis once, and coloring edits and bulk visibility actions render once after applying all changes.
- PR CI runs platform tests and benchmarks in parallel after formatting, with full advisory clang-tidy reserved for manual runs.

- Mesh panels and single-file scalar facets adapt their column count to the window size, maximizing usable panel space; multi-file scalar matrices keep their aligned columns.

- Mesh parsing and loading now build as a Qt-free `vv_mesh` static library shared by the viewer and the tests; compiler warnings and sanitizer flags come from one `vv_options` target.
- Malformed mesh painter scalars in JSON files are reported on stderr instead of being dropped silently.
- Update the CMake fallback dependencies to cxxopts 3.3.1 and nlohmann/json 3.12.0.
- Retain temporary release artifacts and Docker build records for 3 days; published release downloads remain available.
- The colorbar is now a legend card in the bottom-right corner: the field name, whether it holds points or cells, then one row per category in ascending order or a horizontal bar whose handles show the clip values. Long names are shortened in the middle, with the full name in a tooltip.
- The parts list becomes the Layers panel, opened from the rail instead of always covering the top-left of the view; a file with a single part is one row instead of a file row with a part row repeating its name.
- The Layers panel's rows have eye toggles instead of checkboxes, a letter badge per file and a colored dot per part, with a file's parts behind a chevron; **Option**-click (**Alt**-click) an eye or right-click a row to show only that file or part, and Show all brings everything back. The same badge identifies each file's mesh panels, and its letter starts the file's name on the probe card.
- The field pager is a capsule matching the rail and legend: single chevrons around the field's name, its kind (`Points · continuous`) under it, and its position in a badge at the right (`1 / 12`).
- **c** opens the Color panel instead of toggling the cyclic colormap; the hue wheel is one of its cyclic maps.
- VTK's own single-letter keys (**w** wireframe, **s** surface, **3** stereo, **p** pick, **f** fly to and others) no longer change the view behind vv's controls; the Style panel replaces **w** and **s**.
- The annotation bar moves to the top of the view in the rail's style: Rotate, Paint and Erase tools (holding **A** or **E** still paints or erases), numbered label swatches, the brush size in rings, Undo, Save as… and a close button; it wraps onto more rows in a narrow window.
- `--annotate` is ignored, with a warning, for a time series, whose frames would replace the labels.
- With `-e`, each panel's legend fits its panel instead of covering much of its mesh: the full card in a large panel, a compact card (name and kind on one line, the bar between its clip values or the categories as chips) in a medium one, and a strip of the bar or the swatches alone in a small one, with the name in its tooltip; a `+N` chip lists the categories that do not fit.
- Where a snapshot or the labels were saved and which field the vector glyphs show appear for a moment in a notice at the top of the view, instead of in a status bar that stayed under the view as an empty strip.
- The playback bar becomes a transport along the bottom of the view in the overlays' style: the field pager at its left, previous and next frame around a round play button, a timeline with the time to 4 significant digits and `frame n of N` over a seekable track with round time ticks, then the speed menu and Loop. In a time series **Space** plays and pauses, **←** and **→** step a frame and **Shift+Space** steps to the next field.

### Fixed

- Continuous legends keep small nonzero bounds readable after clamping and size their value columns independently, growing within their panel when needed.

- Double-clicking a continuous legend's clip value edits it in place; its handles remain drag controls.

- Focused color-bound editors no longer call destroyed widget state during teardown.
- Pinned probes remain visible across exploded field pages and refresh category swatches after palette edits.
- Playback refreshes only its changed mesh, preserving reusable spatial indices for static comparisons.

- Closing or saving during an annotation stroke finalizes it before checking unsaved labels, so the first stroke cannot bypass the save prompt.
- Hidden meshes no longer leave their pin marker or probe card visible; showing the mesh restores its retained pin.
- Overflow category chips open the existing color editor for hidden categories, including in the facet grid.

- Tool panels stay above facet legends and use an opaque surface so legends do not obscure their controls.
- Mesh comparisons use the same colored file badges as Layers, with filenames on the first visible facet of each row, instead of repeated VTK titles.

- Glass reduces transparent rendering passes during camera drags and restores full quality on release, improving interactive frame cost.

- Categorical facet legends switch to chips within a height budget, preserving space for the mesh.
- Continuous legends open a colormap menu from their gradient or context menu, including in the expanded grid, where the whole field column follows and keeps its choice across pages.

- Reject empty, malformed and nonfinite `--range` values before Qt initialization or file loading.
- Ignore mesh color metadata with component counts other than three to prevent a stack buffer overflow.
- Detect stdin read failures and buffered output-close failures, removing incomplete temporary files.
- Pass build-helper arguments without shell evaluation, validate option values and reject unsafe cleanup directories.
- Make formatting checks independent of unrelated Git changes and reuse CMake's formatter discovery.
- Detect Xcode's clang-format through xcrun on macOS when normal discovery fails, making the CMake `format` target available.
- The mouse wheel scrolls a long parts list instead of being ignored over it.
- Load every `SCALARS`, `VECTORS` and other attribute block of legacy `.vtk` files, so fields after the first per point or cell association appear in the field pager.
- Annotation no longer paints unrelated cells when the brush lands on vector glyphs or on another part of the file.
- With several files, the legend of a categorical field the first file lacks shows that field's categories and colors instead of reading another field's table, which could show wrong colors or crash.
- A field holding infinite values is colored over the range of its finite values instead of an infinite range.
- Cell fields of an XDMF time series change with the frames instead of keeping the first frame's values.
- A legacy `.vtk` field with its own `LOOKUP_TABLE` is drawn in the colors its legend shows, instead of the mesh taking the table's colors over vv's.

### Removed

- Unused direct fmt dependency from the build and package installation lists.

# [2.0.0] - 2026-09-25

### Added

- STL surface meshes (`.stl`, ASCII and binary).
- Several mesh files open side by side, one labelled panel per file with a
  shared camera; **Space** cycles the union of their scalars, one color range
  per field across files (`vv foo.vtk bar.stl`, `vv dir/*.vtk`).

### Changed

- `-e` with several files lays panels out as a matrix: a labelled row per file,
  a column per scalar with one shared color range and clip range; a file lacking
  a column's field shows plain geometry instead of being left out. Files without
  any fields (e.g. STL) now appear too.
- `--annotate` is ignored (with a warning) when more than one file is given.
- macOS releases ship a self-contained `.tar.gz` that extracts into `~/.local`
  (`bin/vv` plus bundled libraries under `lib/vv`) instead of `vv.app`; the
  broken bare-binary `-bin.tar.gz` is no longer published for macOS.
- `build.sh --install` copies the `vv` binary to `~/.local/bin` on macOS too.

### Removed

- macOS app bundle, Finder "Open with" file associations and the Quick Look
  generator.
- `-T/--thumbnail` offscreen PNG rendering.
- Intel (x86_64) macOS release builds: Homebrew no longer ships Intel bottles, so
  the dependencies would compile from source for hours. 1.5.0 is the last Intel
  macOS release; Intel Macs can still build from source.
- File picker shown when launched without a mesh file; `vv` now requires one
  on every platform.

# [1.5.0] - 2026-08-18

### Added

- `-r/--range min,max`: pin the color range of continuous scalars, in the single
  view and in every facet of `--explode`.
- Cyclic colormap (full hue wheel, no false seam at the wrap) for fields whose
  name contains `phase`; **c** toggles it for any continuous field.
- Vector-field glyphs: **v** cycles 3-component point/cell arrays, drawn as a
  short segment per tuple oriented along the vector (e.g. fibre orientation).

### Changed

- Nodal scalars are interpolated before mapping, so a wavefront gets a smooth
  colormap-correct gradient; cell scalars and categorical LUTs stay flat.
- Space cycles single-component arrays only; multi-component arrays are vector
  fields now (**v**) instead of being colored by their magnitude.

# [1.4.0] - 2026-08-07

### Added

- XDMF parser (`.xdmf` / `.xmf`) for solver outputs with HDF5 heavy data:
  static topology/geometry plus a temporal collection of node/cell attributes.
  Frames stream straight into the rendered array (no geometry re-read) and are
  cached in memory under a 512 MB budget, so looped playback is zero-I/O.
- Annotation mode (`--annotate`): paint a per-cell `label` array with a surface
  brush, undo, and save.
- Playback bar: previous/next frame buttons and a speed drop-up (1x/2x/4x/8x/12x).

### Changed

- Playback is clock-driven: each tick jumps to the frame the elapsed time calls
  for, dropping frames instead of queueing them so high speeds stay real-time.
- `TemporalSource` is now an interface with VTKHDF and XDMF backends; parsers
  expose their time series through `MeshParser::temporal()`.
- Mesh JSON accepts a `surfaces` key.

# [1.3.0] - 2026-07-07

### Changed

- Windows release packaging now produces a single portable `.exe`
  (`scripts/package-windows.sh` + `scripts/vv-portable.nsi`) instead of a
  `.zip`, self-extracting to `%LOCALAPPDATA%\vv\` on first run.

# [1.2.0] - 2026-06-13

### Added

- Cell-data scalar support: Space now cycles point- **and** cell-data fields
  (cell fields labelled `… (cells)`), in both the single view and the
  `--explode` facet grid.
- VTKHDF parser with temporal (time-series) playback: a media bar with
  play/pause, scrub slider, speed multiplier, and loop; color range fixed
  across the animation so the colormap stays stable.
- `build.sh`: single entry point to configure, build, run static analysis
  (`--cppcheck`, `--clang-tidy`, `--analyze`), check formatting, and install.
- `.clang-tidy` and `.cppcheck-suppressions`: high-signal analysis configs
  that filter Qt `moc`/autogen false positives; bug-class findings gated.
- Vector-drawn playback icons (no dependency on system Unicode media glyphs).

### Changed

- `main()` refactored into a `ViewerWindow` `QMainWindow`; `main_qt.cpp`
  retains only CLI/platform setup.
- Release workflow now ships self-contained bundles (Linux AppImage, macOS
  `vv.app` via `macdeployqt`, Windows zip via `windeployqt`) plus bare
  binaries. CI trimmed to a fast cached Linux quality gate.
- `install.sh` replaced by `./build.sh --install`.

### Fixed

- Hardened mesh loading: `std::filesystem` existence/removal, atomic temp-file
  creation, LS-DYNA `*INCLUDE` cycle guard, FreeSurfer header/size/index
  validation, and caught `cxxopts` parse errors.
- Legacy VTK unstructured grids (e.g. ParaView-clipped meshes) now read.

# [1.1.0] - 2026-04-24

### Added

- Interactive Qt frontend replacing the old headless VTK window (`main_qt.cpp`).
- Parts tree overlay widget: toggle per-part and per-group visibility with tri-state checkboxes.
- Color bar overlay widget rendered directly on the VTK canvas.
- Exploded facet-grid view (`--explode`): multiple inputs displayed as a grid of viewports, each with its own color bar and clip range.
- Scalar cycling with Space bar (cycles through all point-data arrays including a "none" state).
- LS-DYNA mesh parser (`LSDynaMeshParser`) supporting keyword-format `.k` / `.key` files.
- Windows build support via vcpkg and MSVC (`win-x64-release` / `win-x64-debug` CMake presets).
- Automatic Qt plugin deployment on Windows post-build (vcpkg tree copy or `windeployqt` fallback).
- GitHub Actions release workflow producing signed Windows and Linux binaries.
- GitHub Actions Docker workflow.
- Cached vcpkg dependencies in CI.
- `clang-format` target and `.clang-format` configuration (LLVM base, 100-column limit).
- CMake configure log now prints detected VTK version on success.

### Fixed

- Missing `#include <vector>` in `MeshParser.h`, required by VTK 9.1 on Linux where VTK headers no longer pull it in transitively.
- CMake VTK-not-found error now mentions `libvtk9-qt-dev` (required for `GUISupportQt` on Ubuntu/Debian).

# [1.0.5] - 2025-10-29

### Added

- Multi-mesh support in exploded view mode.
- CI/CD pipeline.
- FreeSurfer surface mesh parser (`FSurfMeshParser`).
- CARTO mesh parser (`CartoMeshParser`).
- Build date embedded in version string.
- Install script (`install.sh`) copying the binary to `~/.local/bin`.
- Versioning via `version.h.in`.
