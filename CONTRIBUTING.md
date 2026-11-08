# Contributing

[README](README.md) · [Usage](docs/usage.md) · [Building](docs/building.md)

## Development workflow

This guide applies to human and AI contributors. Start with [platform setup](docs/building.md#platform-setup), then use a separate development build with tests enabled. Run commands below from the repository root.

Keep changes focused, preserve public behavior and fix the underlying cause of failures. Add a regression test for a changed contract at the lowest layer that exercises it. Use Conventional Commits for commit subjects and record noteworthy changes under Unreleased in [CHANGELOG.md](CHANGELOG.md), following Keep a Changelog.

## Change boundaries and contracts

| Target       | Responsibility and boundary                                                       | Relevant CTest checks           |
| ------------ | --------------------------------------------------------------------------------- | ------------------------------- |
| `vv_mesh`    | Parsers, mesh loading and temporal sources. Keep it Qt-free.                      | `mesh-metadata`, `stdin-errors` |
| `vv_ui`      | Overlay widgets and their interactions. Keep it VTK-free.                         | `ui`                            |
| `vv_viewer`  | Mesh rendering and the window's integration of loading, widgets and viewer state. | `panel-layout`, `viewer`        |
| `vv`         | Command-line parsing and platform/application setup.                              | `cli`                           |
| Build helper | Configuration, analysis and installation entry point.                             | `build-helper`                  |

The Unix-only `stdin-errors` and `build-helper` checks are not registered on Windows. Tests and target wiring live in [tests/CMakeLists.txt](tests/CMakeLists.txt); shared warning and sanitizer flags come from `vv_options` in [CMakeLists.txt](CMakeLists.txt).

New parsers must preserve recognition precedence: [buildParsers](src/MeshLoading.cpp) picks the first matching parser and does not retry another after parsing fails. Avoid broad recognition that captures another format. Stdin loses the source extension and directory; [stdin-errors](tests/StdinTests.cpp) protects copy error handling and temporary-file cleanup. Update the [input reference](docs/usage.md#inputs-and-command-line) when adding or changing a parser.

Temporal sources update the existing rendered dataset through [TemporalSource](src/include/TemporalSource.h). Keep frame work limited to the active array, and preserve sampled range semantics. Viewer integration tests cover annotation save prompts, probe correspondence, visibility and column-color changes; bulk changes and column edits must preserve their single-render-submission behavior.

## Tests and checks

Configure a dedicated build with tests enabled; strict warnings and warnings-as-errors default to on:

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/dev
(cd build/dev && ctest --output-on-failure)
```

Apply the platform-specific prefix/toolchain settings from the [build guide](docs/building.md#platform-setup) to this build directory as needed. These tests are bounded by CTest timeouts. Widget and viewer tests default to Qt's offscreen platform and do not require a display; OpenGL frame benchmarks run separately.

For ASan/UBSan on macOS/Linux with GCC or Clang, add `-DVV_ENABLE_SANITIZERS=ON` to the configure command and rebuild before running CTest. Sanitizer findings terminate the affected test instead of recovering. CI runs this configuration in a separate Linux job.

Linux sanitizer CI configures `-DVV_TEST_QPA_PLATFORM=xcb` and runs `xvfb-run -a ./scripts/test-sanitizers.sh BUILD_DIR`, keeping leak detection enabled with full allocation backtraces (`ASAN_OPTIONS=fast_unwind_on_malloc=0`). Install Xvfb, xauth and Openbox for this run; the helper waits for Openbox to manage windows so native focus tests work, and stops it when CTest exits. The viewer check allows 120 seconds for allocation tracing; ordinary builds keep its 30-second timeout. Qt 5.15.3's offscreen backend retains screen objects at shutdown even in a standalone Qt program; xcb cleans up those objects.

These tests exercise raster widgets and count VTK render submissions without creating an OpenGL context. The helper sets `QT_XCB_GL_INTEGRATION=none` to avoid Qt initializing Mesa while choosing visuals for raster windows. The separate required frame benchmark exercises real OpenGL rendering.

CI sets `LSAN_OPTIONS=suppressions=$PWD/tests/lsan-fontconfig.supp:print_suppressions=1` for a single [initialization-only exception](tests/lsan-fontconfig.supp). Fontconfig 2.13.1 retains 288 bytes after a standalone `FcInit()` call; its `FcFini()` cleanup resets global state shared with other clients. The anchored `^FcInit$` rule excludes allocations made inside that initialization function, including any other initialization-time library leaks. Later font matching and application callbacks remain checked. Sanitizer test logs retain suppression counts even on successful CI runs. Inspect complete allocation paths before changing exceptions; library-name patterns also match application allocations whose stacks pass through those libraries.

### Formatting

Install `clang-format` before configuring, then run:

```sh
cmake --build build/dev --target format
cmake --build build/dev --target format-check
```

`format` updates C++ sources; `format-check` reports differences without changing files or inspecting unrelated Git changes. CMake searches for `clang-format` on `PATH`, then through `xcrun` on macOS. Set `-DCLANG_FORMAT_EXE=/path/to/clang-format` at configuration if needed. Use the formatter version reported by CI when local output differs.

### Static analysis

With cppcheck, clang-tidy and Clazy installed, use the helper against the development build:

```sh
./build.sh -d build/dev -t Debug --no-build --cppcheck-check --clang-tidy-check
./build.sh -d build/dev -t Debug --no-build --clazy
./build.sh -d build/dev -t Debug --no-build --clang-tidy
./build.sh -d build/dev -t Debug --no-build --unused-functions
```

The helper configures a compilation database even with `--no-build`. Its cppcheck flags and [.cppcheck-suppressions](.cppcheck-suppressions) account for Qt moc/autogen false positives; use the helper rather than maintaining a second analysis command. `--cppcheck-check` prints findings from [.cppcheck-advisory](.cppcheck-advisory) without failing on them; `--cppcheck` treats all unsuppressed findings as errors. On macOS, the helper also finds clang-tidy in an installed Homebrew LLVM toolchain and supplies the SDK path to both Clang-based analyzers. Tests are included when the build has `BUILD_TESTING=ON`.

[CI](.github/workflows/build.yml) gates formatting, strict builds, regression tests, sanitizers, cppcheck and the selected clang-tidy correctness checks. Clazy's initial explicit Qt correctness set is advisory on every run. The full [.clang-tidy](.clang-tidy) configuration and the unused-function audit are advisory in manually dispatched runs. Review unused-function findings against other platforms, Qt versions, tests and generated Qt callers before removing code; the audit covers only the configured build.

## Performance checks

With `BUILD_TESTING=ON`, CMake builds `vv_render_benchmark` but does not register it with CTest. CTest discovers all viewer test methods; CPU benchmarks skip unless `VV_RUN_BENCHMARKS=1` is set. Run CPU and frame workloads explicitly:

```sh
mkdir -p benchmark-results
QT_QPA_PLATFORM=offscreen VV_RUN_BENCHMARKS=1 build/dev/tests/vv_viewer_tests benchmarkFacetSetup \
  -iterations 5 -median 5 -o benchmark-results/cpu-setup.xml,xml -o -,txt
QT_QPA_PLATFORM=offscreen VV_RUN_BENCHMARKS=1 build/dev/tests/vv_viewer_tests benchmarkColumnInteraction \
  -iterations 1000 -median 5 -o benchmark-results/cpu-interaction.xml,xml -o -,txt
build/dev/tests/vv_render_benchmark --require-rendering --output benchmark-results/render.json
python3 scripts/benchmark-summary.py benchmark-results
```

Use a native display for the frame benchmark, not Qt's offscreen plugin. On Linux, CI requires software OpenGL under Xvfb; with Xvfb and Mesa installed, replace the frame command with:

```sh
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build/dev/tests/vv_render_benchmark --require-rendering --output benchmark-results/render.json
```

The examples use a POSIX shell; Windows CI runs them in MSYS2. Native platforms without OpenGL 3.2 report rendering as unavailable when `--require-rendering` is omitted. They do not substitute CPU measurements for rendered frames.

macOS CI uses `--smoke` because hosted runners use Apple's software OpenGL renderer. This keeps all style, rotation and viewport scenarios, sample counts and nonempty-frame checks, using a 300 × 200 viewport and lower sphere resolution. Local runs use the full 900 × 600 workload by default; JSON results and CI summaries identify the workload, dimensions and mesh size. Compare timings only for matching workloads and renderers.

Frame workloads cover nested transparent shells in one and four viewports, Solid/X-ray/Glass, rest and rotation, plus style changes and their first frame. They wait for GPU completion and reject empty frames. Results include commit, platform, Qt/VTK and driver metadata.

Compare timings on the same OS and renderer. Correctness, nonempty rendering and bounded render submissions are gates; hardware-dependent timing values are observations, not end-to-end input latency or guarantees for another GPU.

## Documentation changes

- A change to a CLI flag, shortcut, CMake option or parser updates the document that owns it: [usage](docs/usage.md) for viewer behavior, [building](docs/building.md) for setup, and this guide for contribution checks and contracts.
- Keep the README focused on first use. Link to the detailed reference instead of repeating full option, shortcut or setup lists. `vv --help` and `./build.sh --help` define their command-line interfaces.
- Describe current behavior in the guides and release history in the changelog. Preserve meaningful limits and failure behavior when shortening prose.
- Use relative links, concise sentences and one physical line per prose paragraph or list item. Check links and commands against source and workflows; distinguish inspection from execution when reporting validation.
