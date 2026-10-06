# vv WebAssembly visualiser

`@sssilvar/vv-wasm` embeds vv's C++/VTK rendering in React. React is the only runtime
peer dependency for the React entry point. Non-React hosts can import
`ViewerEngine` and `Lut` from `@sssilvar/vv-wasm/engine` without loading React. Qt, mesh parsers, Three.js, R3F and WebGPU are not required.

## Build

Install Emscripten, CMake, Ninja, Git, Python 3.10+ and pnpm. On macOS:

```sh
brew install emscripten cmake ninja
cd web
pnpm install --frozen-lockfile
pnpm build
pnpm dev
```

The first build fetches VTK 9.7.0 at the commit pinned in
`scripts/build-wasm.sh` and compiles the required rendering modules. Later builds
reuse `.cache/vtk-wasm-build`. Set `VV_BUILD_JOBS` to bound compilation parallelism
(default 4). Set `VV_VTK_WASM_DIR` to an already configured compatible VTK WASM
build or installation. Windows builds can run the script from Git Bash after
activating Emscripten; Linux uses the same script.

The package contains React and standalone engine entry points, the generated
runtime, `vv.wasm`, and declarations. Keep all emitted JS/WASM assets together. Serve WASM as `application/wasm`; production servers should
compress it with Brotli or gzip and cache the generated asset. The JS wrapper and
runtime are about 122 KB before compression; the WASM binary is about 10 MB
(2.8 MB gzip). The binary is an asset rather than an embedded base64 string.

## React API

```tsx
import { useMemo, useState } from "react";
import { Viewer, Lut, ScalarBar, type Surface } from "@sssilvar/vv-wasm";

function SimulationView({ surface, values, revision }: {
  surface: Surface;
  values: Float32Array;
  revision: number;
}) {
  const lut = useMemo(() => Lut.rainbow(-80, 40), []);
  const [thresholds, setThresholds] = useState([-80, 40]);

  return <div style={{ position: "relative", height: 500 }}>
    <Viewer surface={surface} scalar={{ values, revision, lut, thresholds }} />
    <aside style={{ position: "absolute", left: 12, top: 12, height: 220 }}>
      <ScalarBar lut={lut} thresholds={thresholds} onChange={setThresholds} />
    </aside>
  </div>;
}
```

- Vertices are flat xyz coordinates; triangle indices address those vertices.
  `Float32Array`, `Uint16Array` and `Uint32Array` are accepted directly. Supplied
  input buffers remain caller-owned; geometry is copied into owned scene storage. Scalar uploads write once into a
  reusable VTK back buffer; validation commits it by swapping buffers, without
  a second scalar copy. Invalid frames leave the displayed field intact.
  Keep geometry array identities stable across scalar frames.
- Scalars contain exactly one value per vertex or triangle. Set
  `association: "cell"` for triangle fields. NaN means missing data and uses gray;
  infinities and invalid geometry are rejected. Coordinates use the host's units.
- Increment `revision` when updating a scalar buffer in place. Only scalar data
  crosses the WASM boundary for that frame. The topology, normals, locator,
  camera and LUT persist. Multiple pending frames coalesce into one upload per
  animation frame; only the newest frame is displayed. Keep a fixed LUT range over a time series.
- `Lut.rainbow`, `Lut.cyclic`, custom segmented LUTs and `Lut.categorical` are
  available. Categories can use sorted, nonconsecutive labels. LUT colors accept
  hex RGB/RGBA and comma-form rgb/rgba. LUT changes should use a new descriptor.
- The scalar bar changes saturation endpoints inside the fixed range, matching
  vv's color-range clipping behavior. Drag the vertical handles, use arrow keys,
  or double-click a limit to enter its value. Spatial clipping is separate: supply up to
  six `clippingPlanes` as `[nx, ny, nz, d]`; points with `n·position + d >= 0` stay
  visible. This clips the surface without rebuilding topology or adding caps.
- `points`, `wireframe`, `opacity`, `annotations` and tag hover/click callbacks
  cover Sandboxer's existing visual features. An annotation diameter is in world
  units; `pointDiameter` is in framebuffer pixels. A `label` is positioned over
  its world coordinate by the C++ camera. `focusPosition` with a changed
  `focusRevision` turns the camera to a tag.
- Camera controls rotate on left drag, pan on right/Shift drag and zoom on the
  wheel or middle drag. Reset restores the front view, upright orientation and
  fitted distance. A viewer ref exposes reset, camera state, projection in CSS pixels, streaming
  updates and upload/render statistics. `onProbe` reports the intersected cell
  and interpolated scalar value. Camera state survives context-loss fallback. Inputs are scoped to the canvas; blur, cancellation and
  unmount release captured pointers and pending animation frames.

## Rendering and ownership

`src/visualization/Scene` owns VTK geometry, scalar arrays, a lazily built picking locator,
camera and annotations. It uses a direct `vtkPolyDataMapper` without the desktop
panel/interactor machinery. Scalar mapper configuration and LUT construction live
in `ScalarVizUtils`, shared with the desktop renderer. `WasmApi` exposes a small C ABI;
the TSX adapter manages loading, buffer transfers, resize, interaction and React
lifecycle. Rendering happens once per requested animation frame and stops when
the scene is idle. Each canvas owns an independent C++ scene and render window;
all scenes share a single WASM module.

Mesh back faces render at 45% brightness in both backends, preserving scalar hues
and opacity. Supply consistently wound triangles (counterclockwise when viewed
from outside) for this cue to identify the inner shell. Winding is preserved;
the viewer does not infer or repair an anatomical inside/outside orientation.
An upper-left key light, weaker opposing fill and subtle white highlights provide
curvature cues. Lights follow the camera; software shading interpolates vertex
lighting, while WebGL uses per-fragment Phong shading. These are surface-lighting
cues, not cast shadows or ambient occlusion.
Wireframe edges use unlit colors without the back-face tint for clear visibility.

`backend="auto"` uses WebGL 2, including integrated Intel graphics and browser
software GL drivers. It switches to the C++ rasterizer when WebGL is unavailable
or the context is lost. WebGL uses FXAA for edge antialiasing. `backend="software"` forces Canvas 2D presentation of a
C++ framebuffer without GPU APIs. The software path implements frustum and
plane clipping, perspective-correct scalar interpolation, depth, lighting,
wireframe, points, tag picking and four layers of transparency. Layers beyond
the nearest four are discarded; this bounds memory and work. Tags are projected
disks in software and instanced spheres on the GPU.

No WASM threads or cross-origin isolation headers are required. Memory is capped
at 1 GiB per module, 256 MiB per staging buffer, 16 scenes, 4 million vertices,
8 million triangles, 100,000 annotations and 8,388,608 framebuffer pixels. The
wrapper caps pixel ratio and framebuffer dimensions. Large meshes render more
slowly in software; forcing software does not promise GPU throughput.

The current browser contract is a triangle surface or point cloud with streamed
scalar arrays. Volume extraction, solver formats, temporal file parsing and
annotation editing remain desktop/host responsibilities.

## Sandboxer integration

The companion Sandboxer change replaces its R3F mesh subtree with one `Viewer`,
uses `@sssilvar/vv-wasm` for `Surface`, `SpatialAnnotation`, `Lut` and `LutSlider`, and
retains its data-loading and inference code. This is a data-contract replacement,
not a binary-compatible implementation of all anatomy-gl/R3F exports.

Build this package before running `pnpm install` in Sandboxer. Its local
dependency is `file:../../personal/vv/web`. For Vite development add:

```tsx
optimizeDeps: { exclude: ["@sssilvar/vv-wasm"] }
```

This preserves relative WASM asset URLs instead of relocating them into Vite's
dependency-prebundle directory. After rebuilding a local `file:` package, run
`pnpm update @sssilvar/vv-wasm --offline` in Sandboxer to refresh its copy.

## Validation

```sh
cmake -S . -B build/scene-native -G Ninja -DVV_VISUALIZATION_ONLY=ON
cmake --build build/scene-native
ctest --test-dir build/scene-native --output-on-failure

cd web
pnpm typecheck
pnpm format:check
pnpm lint
pnpm exec playwright install chromium firefox webkit
pnpm test:e2e
```

Playwright checks WebGL and software rendering in Chromium, Firefox and WebKit,
scalar and spatial clipping, cell fields, tag picking, camera controls, idle
rendering, context loss, teardown and scalar updates on a 261,120-triangle
fixture. A Chromium-only test disables WebGL at browser launch. These checks
verify browser behavior; Intel hardware and Windows/Linux GPU drivers still
need testing on those machines.

## Simulation streaming

Keep the surface, LUT and global range stable. Pass `scalar.revision` when updating
an existing buffer through React. A producer running independently of React can
call `viewerRef.current?.setScalarValues(values, "point")`; it keeps only the
latest pending frame and schedules a render. The caller must retain the buffer
until that animation frame has run. This method updates an already configured
scalar field; its LUT stays unchanged. Cell fields use `"cell"`.

`statistics()` reports `scalarUploadMs`, `renderMs`, transferred `scalarBytes`,
`stagingBytes`, and module-wide `wasmHeapBytes`. Timings measure CPU submission,
not GPU completion. `geometryUploads` and `scalarUploads` count committed API
updates, not driver buffer allocations. Large geometry transfer scratch space is
released after upload. VTK reuses two scalar buffers during steady playback.

For a reproducible hardware benchmark (with an available GPU):

```sh
VV_GPU_BENCHMARK=1 pnpm exec playwright test --config playwright.config.tsx --project chromium-gpu
```

The benchmark prints the actual renderer, median/p95 CPU times and heap growth,
and compares batched direct transfers with the staging-copy API. The default
Chromium correctness suite deliberately uses SwiftShader; do not treat its frame
times as hardware performance. Browser tests additionally exercise coalescing and
camera restoration. Native pixel checks cover categorical clipping invariance and
transparent-field annotation picking.

The demo includes a welded synthetic chamber, analytical voltage playback, a
fixed mV range, point/cell fields, scalar probing and camera presets. It is synthetic
validation data, not a patient-specific dataset. Start with `pnpm dev`; add `?large`
for the 261,120-triangle fixture or `?backend=software` for the CPU fallback.

Sandboxer's `MeshViewer` now accepts an optional `simulation` prop with `meshName`,
`name`, `Float32Array` values, `revision`, `association`, fixed `range`, and `unit`.
It preserves geometry while frames change. `/__mesh-simulation` is a development
harness for its production component; Playwright supplies its mesh API fixture.
Loading a solver's file format remains the caller's responsibility.

## Storybook

After `pnpm build`, run `pnpm storybook` and open http://localhost:6006.
`pnpm build:storybook` produces a static catalog; `pnpm test:storybook` verifies
that build with Chromium. The catalog uses the real WASM renderer and shared
synthetic geometry. Viewer stories cover point/cell fields, cyclic/categorical
palettes, missing values, saturation, spatial clipping, wireframe, point clouds,
transparency, software rendering, annotations, streaming and large geometry.
Empty and invalid geometry intentionally show the viewer's error state.
Scalar-bar stories cover keyboard-editable, read-only, constant, segmented and
formatted ranges. Backend and display controls are available in the controls panel.

## npm release candidates

The package is currently **UNLICENSED**. Install a published candidate with
`pnpm add @sssilvar/vv-wasm@rc` (and React 19 when using the React entry point).

The `Web package` workflow checks types, formatting, lint, all three browser
engines, the static Storybook, and the packed archive. Chromium runs headless
on Linux with SwiftShader; Firefox and WebKit run on macOS with native graphics.
The build and browser jobs are separate, allowing failed tests to be retried
without recompiling VTK.
It retains the package,
Storybook and failure diagnostics for three days. Builds use pinned actions,
Node, pnpm and Emscripten, with no privileged PR jobs or release build caches.

To release, update `web/package.json` to `0.1.0-rc.N`, commit, then push the
matching `npm-v0.1.0-rc.N` tag. `npm-publish.yml` builds and tests the archive once,
then publishes that archive with provenance and the `rc` dist-tag. The tag prefix
is separate from desktop release tags. For a combined prerelease, also update
the native version in `CMakeLists.txt` and push `vMAJOR.MINOR.PATCH-rc.N` instead.
That workflow builds the Linux, macOS and Windows app downloads, publishes the
npm RC, and attaches both distributions plus `release-versions.json` to one
GitHub prerelease. The `npm` GitHub environment permits only
RC tags; only the publish job can access its `NPM_TOKEN` bootstrap secret.

For token-free releases, configure an npm trusted publisher on the package:
GitHub owner `sssilvar`, repository `vv`, workflow `npm-publish.yml`, environment
`npm`, with direct publishing allowed. Complete a successful OIDC release before
revoking the bootstrap token and deleting `NPM_TOKEN`. Then require 2FA and
disallow traditional tokens in npm package publishing settings. A new trusted
publisher must be used within npm's activation window (currently two days).
Never commit a token or pass one through shell arguments or chat.
