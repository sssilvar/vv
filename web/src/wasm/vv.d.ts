export interface WasmModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  HEAPF64: Float64Array;
  _malloc(bytes: number): number;
  _free(pointer: number): void;
  _vv_create(software: number, selector: number): number;
  _vv_destroy(id: number): void;
  _vv_surface(
    id: number,
    positions: number,
    coordinates: number,
    indices: number,
    count: number,
    points: number,
  ): number;
  _vv_scalar(id: number, values: number, count: number, cell: number): number;
  _vv_scalar_buffer(id: number, count: number, cell: number): number;
  _vv_scalar_commit(id: number, count: number, cell: number): number;
  _vv_camera_state(id: number): number;
  _vv_set_camera_state(id: number, state: number): number;
  _vv_lut(
    id: number,
    colors: number,
    count: number,
    stops: number,
    min: number,
    max: number,
    interpolate: number,
  ): number;
  _vv_style(
    id: number,
    r: number,
    g: number,
    b: number,
    opacity: number,
    wireframe: number,
    diameter: number,
  ): number;
  _vv_planes(id: number, planes: number, count: number): number;
  _vv_annotations(id: number, annotations: number, count: number): number;
  _vv_resize(id: number, width: number, height: number): number;
  _vv_render(id: number): number;
  _vv_pixels(id: number): number;
  _vv_camera(
    id: number,
    rx: number,
    ry: number,
    px: number,
    py: number,
    zoom: number,
    reset: number,
  ): void;
  _vv_reveal(id: number, x: number, y: number, z: number): void;
  _vv_project(id: number, x: number, y: number, z: number): number;
  _vv_pick(id: number, x: number, y: number): number;
  _vv_stats(id: number): number;
}

export default function createModule(options: {
  locateFile: (path: string) => string;
}): Promise<WasmModule>;
