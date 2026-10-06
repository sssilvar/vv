import { useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { Viewer, Lut, type ViewerHandle, type CameraState, type ViewerStatistics } from "../src";
import { chamber, voltageFrame } from "../demo/fixtures";
import createModule from "../src/wasm/vv.js";
import wasmUrl from "../src/wasm/vv.wasm?url&no-inline";

const surface = chamber(512, 256);
const values = new Float32Array(surface.vertices.length / 3);
voltageFrame(surface, values, 0);
const lut = Lut.rainbow(-80, 40);
const percentile = (samples: number[], fraction: number) =>
  samples.toSorted((a, b) => a - b)[Math.floor((samples.length - 1) * fraction)];

interface Benchmark {
  uploadMedianMs: number;
  uploadP95Ms: number;
  renderMedianMs: number;
  renderP95Ms: number;
  heapGrowthBytes: number;
}
interface ViewerTest {
  statistics(): ViewerStatistics | undefined;
  camera(): CameraState | undefined;
  reset(): void;
  frontPoint(): readonly number[] | undefined;
  pose(): void;
  burst(): void;
  removePeer(): void;
  benchmark(): Promise<Benchmark>;
  transferBenchmark(): Promise<{ copyMs: number; directMs: number; bytesPerFrame: number }>;
}
declare global {
  interface Window {
    viewerTest: ViewerTest;
  }
}

async function transferBenchmark() {
  const module = await createModule({ locateFile: () => wasmUrl });
  const selector = module._malloc(2);
  module.HEAPU8.set([35, 0], selector);
  const id = module._vv_create(1, selector);
  const coordinates = module._malloc(surface.vertices.length * 4);
  const indices = module._malloc(surface.indices.length * 4);
  const staging = module._malloc(values.byteLength);
  if (!id || !coordinates || !indices || !staging) throw new Error("benchmark allocation");
  try {
    module.HEAPF32.set(surface.vertices, coordinates / 4);
    module.HEAPU32.set(surface.indices, indices / 4);
    if (
      !module._vv_surface(
        id,
        coordinates,
        surface.vertices.length,
        indices,
        surface.indices.length,
        0,
      )
    )
      throw new Error("benchmark geometry");
    const timings = { copy: [] as number[], direct: [] as number[] };
    for (let i = 0; i < 32; ++i) {
      // Batch to exceed the browser clock's resolution; alternate order and discard warmup.
      for (const direct of i % 2 ? [true, false] : [false, true]) {
        const start = performance.now();
        for (let frame = 0; frame < 50; ++frame) {
          const pointer = direct ? module._vv_scalar_buffer(id, values.length, 0) : staging;
          module.HEAPF32.set(values, pointer / 4);
          const ok = direct
            ? module._vv_scalar_commit(id, values.length, 0)
            : module._vv_scalar(id, pointer, values.length, 0);
          if (!ok) throw new Error("benchmark scalar");
        }
        if (i >= 2) timings[direct ? "direct" : "copy"].push((performance.now() - start) / 50);
      }
    }
    return {
      copyMs: percentile(timings.copy, 0.5),
      directMs: percentile(timings.direct, 0.5),
      bytesPerFrame: values.byteLength,
    };
  } finally {
    module._vv_destroy(id);
    module._free(selector);
    module._free(coordinates);
    module._free(indices);
    module._free(staging);
  }
}

function Harness() {
  const ref = useRef<ViewerHandle>(null);
  const [peer, setPeer] = useState(new URLSearchParams(location.search).has("multi"));
  window.viewerTest = {
    removePeer: () => setPeer(false),
    statistics: () => ref.current?.statistics(),
    camera: () => ref.current?.cameraState(),
    reset: () => ref.current?.resetCamera(),
    frontPoint: () => ref.current?.project([0, 0, 1]),
    pose: () =>
      ref.current?.setCameraState({
        position: [3, 2, 5],
        focalPoint: [0.1, -0.1, 0],
        viewUp: [0, 1, 0],
        viewAngle: 30,
      }),
    burst() {
      for (let i = 0; i < 20; ++i) {
        voltageFrame(surface, values, i);
        ref.current?.setScalarValues(values);
      }
    },
    async benchmark() {
      const uploads: number[] = [],
        renders: number[] = [];
      let heap = 0;
      for (let i = 0; i < 90; ++i) {
        voltageFrame(surface, values, i);
        ref.current?.setScalarValues(values);
        await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
        const stats = ref.current?.statistics();
        if (!stats) throw new Error("viewer not ready");
        if (i === 15) heap = stats.wasmHeapBytes;
        if (i > 15) {
          uploads.push(stats.scalarUploadMs);
          renders.push(stats.renderMs);
        }
      }
      return {
        uploadMedianMs: percentile(uploads, 0.5),
        uploadP95Ms: percentile(uploads, 0.95),
        renderMedianMs: percentile(renders, 0.5),
        renderP95Ms: percentile(renders, 0.95),
        heapGrowthBytes: (ref.current?.statistics()?.wasmHeapBytes ?? 0) - heap,
      };
    },
    transferBenchmark,
  };
  return (
    <div style={{ display: "flex" }}>
      <div style={{ width: 800, height: 600 }}>
        <Viewer ref={ref} surface={surface} scalar={{ values, lut }} />
      </div>
      {peer && (
        <div style={{ width: 400, height: 600 }}>
          <Viewer surface={surface} />
        </div>
      )}
    </div>
  );
}
const root = document.getElementById("root");
if (!root) throw new Error("missing harness root");
createRoot(root).render(<Harness />);
