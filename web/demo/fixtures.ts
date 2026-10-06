import type { Surface } from "../src";

/** Closed, welded synthetic chamber; no duplicated seam or degenerate polar faces. */
export function chamber(segments: number, rings: number): Surface {
  const vertices = new Float32Array(((rings - 1) * segments + 2) * 3);
  const indices = new Uint32Array(segments * (rings - 1) * 6);
  vertices.set([0, 1.35, 0]);
  for (let ring = 1; ring < rings; ++ring) {
    const latitude = (Math.PI * ring) / rings;
    const y = Math.cos(latitude);
    for (let segment = 0; segment < segments; ++segment) {
      const longitude = (Math.PI * 2 * segment) / segments;
      const offset = (1 + (ring - 1) * segments + segment) * 3;
      vertices[offset] = 0.8 * Math.sin(latitude) * Math.cos(longitude) * (1 + 0.15 * y);
      vertices[offset + 1] = 1.35 * y;
      vertices[offset + 2] = Math.sin(latitude) * Math.sin(longitude) * (1 + 0.1 * y);
    }
  }
  const bottom = vertices.length / 3 - 1;
  vertices.set([0, -1.35, 0], bottom * 3);
  let offset = 0;
  const face = (a: number, b: number, c: number) => {
    indices.set([a, b, c], offset);
    offset += 3;
  };
  for (let segment = 0; segment < segments; ++segment) {
    const next = (segment + 1) % segments;
    face(0, 1 + next, 1 + segment);
    for (let ring = 0; ring < rings - 2; ++ring) {
      const a = 1 + ring * segments + segment;
      const b = 1 + ring * segments + next;
      face(a, b, a + segments);
      face(b, b + segments, a + segments);
    }
    face(bottom, 1 + (rings - 2) * segments + segment, 1 + (rings - 2) * segments + next);
  }
  return { name: "Synthetic chamber", vertices, indices, color: [0.72, 0.78, 0.82] };
}

export function voltageFrame(surface: Surface, values: Float32Array, frame: number): void {
  for (let i = 0; i < values.length; ++i) {
    const y = surface.vertices[i * 3 + 1];
    const x = surface.vertices[i * 3];
    values[i] = -20 + 60 * Math.sin(y * 2 + x * 0.5 - frame * 0.08);
  }
}
