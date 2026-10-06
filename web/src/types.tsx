export type NumericValues = readonly number[] | Float32Array | Float64Array;
export type Indices = readonly number[] | Uint16Array | Uint32Array;
export type Vec3 = [number, number, number];
export type ClipPlane = [number, number, number, number];

export interface Surface {
  name: string;
  vertices: NumericValues;
  indices: Indices;
  normals?: NumericValues;
  color: Vec3;
}

export interface SpatialAnnotation {
  position: Vec3;
  color: Vec3;
  diameter: number;
  surface?: string;
  value?: string | number;
}

export interface ViewerStatistics {
  geometryUploads: number;
  scalarUploads: number;
  renders: number;
  backend: "webgl" | "software";
  /** CPU time in the most recent upload/render; render excludes GPU completion. */
  scalarUploadMs: number;
  renderMs: number;
  scalarBytes: number;
  stagingBytes: number;
  wasmHeapBytes: number;
}

export interface CameraState {
  position: Vec3;
  focalPoint: Vec3;
  viewUp: Vec3;
  viewAngle: number;
}

export interface PickResult {
  cell: number;
  annotation: number;
  scalar: number;
}
