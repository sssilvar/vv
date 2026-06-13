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
