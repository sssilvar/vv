import createModule, { type WasmModule } from "./wasm/vv.js";
import wasmUrl from "./wasm/vv.wasm?url&no-inline";
import type {
  CameraState,
  PickResult,
  ClipPlane,
  SpatialAnnotation,
  Surface,
  NumericValues,
  Vec3,
  ViewerStatistics,
} from "./types";
import type { Lut } from "./lut";

export { Lut } from "./lut";
export type {
  CameraState,
  PickResult,
  Surface,
  NumericValues,
  Indices,
  Vec3,
  ClipPlane,
  SpatialAnnotation,
  ViewerStatistics,
} from "./types";

let modulePromise: Promise<WasmModule> | undefined;
function loadModule(): Promise<WasmModule> {
  modulePromise ??= createModule({ locateFile: () => wasmUrl }).catch((error: unknown) => {
    modulePromise = undefined;
    throw error;
  });
  return modulePromise;
}

function check(ok: number, operation: string): void {
  if (!ok) throw new Error(`vv: ${operation} failed (invalid data or resource limit)`);
}

/** The staging allocation is reused across frames; WASM retains its own scene buffers. */
export class ViewerEngine {
  private scalarUploadMs = 0;
  private renderMs = 0;
  private scalarBytes = 0;
  private staging = 0;
  private capacity = 0;
  private disposed = false;
  private width = 0;
  private height = 0;
  private image: ImageData | undefined;

  private constructor(
    private readonly module: WasmModule,
    private readonly id: number,
    readonly backend: "webgl" | "software",
    private readonly context: CanvasRenderingContext2D | null,
  ) {}

  static async create(
    canvas: HTMLCanvasElement,
    backend: "auto" | "webgl" | "software",
  ): Promise<ViewerEngine> {
    const module = await loadModule();
    let software = backend === "software";
    if (backend === "auto") {
      // Probe a separate canvas: a failed GL context must not lock the target out of 2D.
      const probe = document.createElement("canvas");
      const gl = probe.getContext("webgl2", { failIfMajorPerformanceCaveat: false });
      software = !gl;
      gl?.getExtension("WEBGL_lose_context")?.loseContext();
    }
    const context = software ? canvas.getContext("2d", { alpha: false }) : null;
    if (software && !context) throw new Error("vv: Canvas 2D unavailable");
    const selector = new TextEncoder().encode(`#${canvas.id}\0`);
    const pointer = module._malloc(selector.length);
    if (!pointer) throw new Error("vv: allocation failed");
    let id: number;
    try {
      module.HEAPU8.set(selector, pointer);
      id = module._vv_create(Number(software), pointer);
    } finally {
      module._free(pointer);
    }
    check(id, "create viewer");
    return new ViewerEngine(module, id, software ? "software" : "webgl", context);
  }

  private reserve(bytes: number): number {
    if (this.disposed) throw new Error("vv: viewer is disposed");
    if (!Number.isSafeInteger(bytes) || bytes < 0 || bytes > 256 * 1024 * 1024)
      throw new RangeError("vv: staging buffer exceeds 256 MiB");
    if (bytes > this.capacity) {
      const capacity = Math.max(bytes, Math.min(256 * 1024 * 1024, this.capacity * 2));
      const pointer = this.module._malloc(capacity);
      if (!pointer) throw new Error("vv: allocation failed");
      if (this.staging) this.module._free(this.staging);
      this.staging = pointer;
      this.capacity = capacity;
    }
    return this.staging;
  }

  surface(surface: Surface, points: boolean): void {
    const { vertices, indices } = surface;
    if (vertices.length > 12_000_000 || indices.length > 24_000_000)
      throw new RangeError("vv: geometry limit exceeded");
    for (const i of Array.isArray(indices) ? indices : []) {
      if (!Number.isInteger(i) || i < 0 || i >= vertices.length / 3)
        throw new RangeError("vv: invalid vertex index");
    }
    const pointer = this.reserve((vertices.length + indices.length) * 4);
    this.module.HEAPF32.set(vertices, pointer / 4);
    this.module.HEAPU32.set(indices, pointer / 4 + vertices.length);
    check(
      this.module._vv_surface(
        this.id,
        pointer,
        vertices.length,
        pointer + vertices.length * 4,
        indices.length,
        Number(points),
      ),
      "surface",
    );
    // Geometry is uploaded rarely; do not retain its large transfer buffer for playback.
    if (this.capacity > 1024 * 1024) {
      this.module._free(this.staging);
      this.staging = 0;
      this.capacity = 0;
    }
  }

  scalar(values: NumericValues, cell: boolean): void {
    if (this.disposed) throw new Error("vv: viewer is disposed");
    const start = performance.now();
    if (values.length) {
      const pointer = this.module._vv_scalar_buffer(this.id, values.length, Number(cell));
      check(pointer, "scalar buffer");
      // Fetch the heap after allocation: memory growth invalidates previous views.
      this.module.HEAPF32.set(values, pointer / 4);
    }
    check(this.module._vv_scalar_commit(this.id, values.length, Number(cell)), "scalar");
    this.scalarUploadMs = performance.now() - start;
    this.scalarBytes += values.length * 4;
  }

  lut(lut: Lut, thresholds: readonly number[]): void {
    const { rgba, stops } = lut.mapping(thresholds);
    const pointer = this.reserve(rgba.byteLength + stops.byteLength);
    this.module.HEAPF64.set(stops, pointer / 8);
    this.module.HEAPF32.set(rgba, (pointer + stops.byteLength) / 4);
    check(
      this.module._vv_lut(
        this.id,
        pointer + stops.byteLength,
        stops.length,
        pointer,
        lut.min,
        lut.max,
        Number(lut.interpolation),
      ),
      "LUT",
    );
  }

  style(color: Vec3, opacity: number, wireframe: boolean, diameter: number): void {
    check(this.module._vv_style(this.id, ...color, opacity, Number(wireframe), diameter), "style");
  }

  planes(planes: readonly ClipPlane[]): void {
    const pointer = this.reserve(planes.length * 32);
    for (let i = 0; i < planes.length; ++i) this.module.HEAPF64.set(planes[i], pointer / 8 + i * 4);
    check(this.module._vv_planes(this.id, pointer, planes.length), "clipping planes");
  }

  annotations(annotations: readonly SpatialAnnotation[]): void {
    const pointer = this.reserve(annotations.length * 56);
    for (let i = 0; i < annotations.length; ++i) {
      const item = annotations[i];
      this.module.HEAPF64.set(item.position, pointer / 8 + i * 7);
      this.module.HEAPF64.set(item.color, pointer / 8 + i * 7 + 3);
      this.module.HEAPF64[pointer / 8 + i * 7 + 6] = item.diameter;
    }
    check(this.module._vv_annotations(this.id, pointer, annotations.length), "annotations");
  }

  resize(width: number, height: number): void {
    if (width === this.width && height === this.height) return;
    check(this.module._vv_resize(this.id, width, height), "resize");
    this.width = width;
    this.height = height;
    if (this.context) {
      this.context.canvas.width = width;
      this.context.canvas.height = height;
      this.image = this.context.createImageData(width, height);
    }
  }

  render(): void {
    const start = performance.now();
    check(this.module._vv_render(this.id), "render");
    if (this.context && this.image) {
      const pointer = this.module._vv_pixels(this.id);
      if (pointer) {
        this.image.data.set(
          this.module.HEAPU8.subarray(pointer, pointer + this.width * this.height * 4),
        );
        this.context.putImageData(this.image, 0, 0);
      }
    }
    this.renderMs = performance.now() - start;
  }

  cameraState(): CameraState {
    const pointer = this.module._vv_camera_state(this.id) / 8;
    const heap = this.module.HEAPF64;
    return {
      position: [heap[pointer], heap[pointer + 1], heap[pointer + 2]],
      focalPoint: [heap[pointer + 3], heap[pointer + 4], heap[pointer + 5]],
      viewUp: [heap[pointer + 6], heap[pointer + 7], heap[pointer + 8]],
      viewAngle: heap[pointer + 9],
    };
  }

  setCameraState(state: CameraState): void {
    const pointer = this.reserve(80);
    this.module.HEAPF64.set(
      [...state.position, ...state.focalPoint, ...state.viewUp, state.viewAngle],
      pointer / 8,
    );
    check(this.module._vv_set_camera_state(this.id, pointer), "camera state");
  }

  camera(rx = 0, ry = 0, px = 0, py = 0, zoom = 0, reset = false): void {
    this.module._vv_camera(this.id, rx, ry, px, py, zoom, Number(reset));
  }
  reveal(position: Vec3): void {
    this.module._vv_reveal(this.id, ...position);
  }

  project(position: Vec3): Vec3 {
    const pointer = this.module._vv_project(this.id, ...position) / 8;
    return [
      this.module.HEAPF64[pointer],
      this.module.HEAPF64[pointer + 1],
      this.module.HEAPF64[pointer + 2],
    ];
  }

  pick(x: number, y: number): PickResult {
    const pointer = this.module._vv_pick(this.id, x, y) / 8;
    return {
      cell: this.module.HEAPF64[pointer],
      annotation: this.module.HEAPF64[pointer + 1],
      scalar: this.module.HEAPF64[pointer + 2],
    };
  }

  statistics(): ViewerStatistics {
    const pointer = this.module._vv_stats(this.id) / 8;
    return {
      geometryUploads: this.module.HEAPF64[pointer],
      scalarUploads: this.module.HEAPF64[pointer + 1],
      renders: this.module.HEAPF64[pointer + 2],
      backend: this.backend,
      scalarUploadMs: this.scalarUploadMs,
      renderMs: this.renderMs,
      scalarBytes: this.scalarBytes,
      stagingBytes: this.capacity,
      wasmHeapBytes: this.module.HEAPU8.byteLength,
    };
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.module._vv_destroy(this.id);
    if (this.staging) this.module._free(this.staging);
    this.staging = 0;
    this.image = undefined;
  }
}
