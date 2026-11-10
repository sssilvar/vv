# vv

A Qt/VTK mesh viewer for inspecting scalar and vector fields, comparing meshes, playing time series and painting cell labels.

Opens VTK, STL, XDMF, mesh JSON, EnSite DIF, CARTO, FreeSurfer, LS-DYNA and NIfTI files. See [supported inputs](docs/usage.md#inputs-and-command-line) for format limits.

## Install

Download a package from [Releases](https://github.com/sssilvar/vv/releases). Packages include Qt and VTK.

### Linux (x86_64)

Download the `.AppImage`, make it executable and pass a mesh file:

```sh
chmod +x vv-*.AppImage
./vv-*.AppImage mesh.vtk
```

The `-bin.tar.gz` download contains only the binary and requires installed runtime libraries.

### macOS (Apple Silicon)

Extract the archive into `~/.local`:

```sh
mkdir -p ~/.local
tar -xzf vv-*-aarch64-apple-darwin.tar.gz -C ~/.local
~/.local/bin/vv mesh.vtk
```

Add `~/.local/bin` to your `PATH` to run `vv` from any directory. Packages are not notarized; if macOS blocks a downloaded copy, remove its quarantine attribute:

```sh
xattr -dr com.apple.quarantine ~/.local/lib/vv
```

Intel Macs can [build from source](docs/building.md#macos).

### Windows (x86_64)

Download the portable `.exe` and pass it a mesh file from a terminal. It extracts its runtime into `%LOCALAPPDATA%\vv\` on first launch; no separate Qt or VTK installation is needed.

## Quick start

```sh
vv mesh.vtk                   # Inspect one mesh
vv before.vtk after.vtk       # Compare files with a shared camera
vv -e before.vtk after.vtp    # Compare fields in aligned columns
vv -o atrium.vtk cath.vtk     # Overlay files in one view
vv ct.nii.gz seg.nii.gz       # Slice views of a volume and its labels
vv --help                    # Command-line options
```

Press **Space** to cycle fields, or to play/pause a time series. **Shift+Space** selects the next field during playback. The tool rail provides colors, display styles, probing, annotation and snapshots.

## Documentation

- [Usage](docs/usage.md): inputs, fields, comparisons, controls and shortcuts.
- [Building](docs/building.md): dependencies, platform setup, source installation and packaging.
- [Contributing](CONTRIBUTING.md): change boundaries, tests, checks and benchmarks for human and AI contributors.
- [Changelog](CHANGELOG.md): release history.
