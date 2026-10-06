import type { Vec3 } from "./types";

function rgba(value: string): [number, number, number, number] {
  const hex = /^#([\da-f]{6}|[\da-f]{8})$/i.exec(value);
  if (hex) {
    const digits = hex[1];
    return [
      Number.parseInt(digits.slice(0, 2), 16) / 255,
      Number.parseInt(digits.slice(2, 4), 16) / 255,
      Number.parseInt(digits.slice(4, 6), 16) / 255,
      digits.length === 8 ? Number.parseInt(digits.slice(6, 8), 16) / 255 : 1,
    ];
  }
  const rgb = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*(\d*\.?\d+))?\s*\)$/.exec(value);
  if (rgb) {
    const color: [number, number, number, number] = [
      Number(rgb[1]) / 255,
      Number(rgb[2]) / 255,
      Number(rgb[3]) / 255,
      rgb[4] === undefined ? 1 : Number(rgb[4]),
    ];
    if (color.every((v) => v >= 0 && v <= 1)) return color;
  }
  throw new TypeError(`vv: unsupported color ${value}; use #rrggbb, #rrggbbaa, rgb or rgba`);
}

/** Compatible LUT descriptor; mapping and interpolation run in C++. */
export class Lut {
  constructor(
    public colors: string[][],
    public min: number,
    public max: number,
    public interpolation = true,
    public categories?: readonly number[],
  ) {
    if (
      !Number.isFinite(min) ||
      !Number.isFinite(max) ||
      min > max ||
      !colors.length ||
      colors.some((segment) => !segment.length) ||
      this.count > 256
    )
      throw new RangeError("vv: invalid LUT");
    for (const color of colors.flat()) rgba(color);
  }

  get count(): number {
    return this.colors.reduce((total, segment) => total + segment.length, 0);
  }
  clone(): Lut {
    return new Lut(
      this.colors.map((segment) => [...segment]),
      this.min,
      this.max,
      this.interpolation,
      this.categories ? [...this.categories] : undefined,
    );
  }
  invert(): void {
    this.colors = this.colors.map((segment) => [...segment].reverse()).reverse();
  }
  toArrayBuffer(): Float32Array {
    return new Float32Array(this.colors.flatMap((segment) => segment.flatMap(rgba)));
  }
  breakpointIndices(): Int32Array {
    let count = 0;
    return new Int32Array(this.colors.slice(0, -1).map((segment) => (count += segment.length)));
  }

  mapping(thresholds: readonly number[] = []): { rgba: Float32Array; stops: Float64Array } {
    if (!this.interpolation) {
      const colors: string[] = [];
      for (const segment of this.colors) {
        const sharedBoundary = colors.length > 0 && colors[colors.length - 1] === segment[0];
        colors.push(...segment.slice(sharedBoundary ? 1 : 0));
      }
      const stops = this.categories
        ? [...this.categories]
        : colors.map(
            (_, i) => this.min + (i * (this.max - this.min)) / Math.max(1, colors.length - 1),
          );
      if (
        stops.length !== colors.length ||
        stops.some((v, i) => !Number.isFinite(v) || (i > 0 && v <= stops[i - 1]))
      ) {
        throw new RangeError("vv: categorical labels must be finite, unique and sorted");
      }
      return { rgba: new Float32Array(colors.flatMap(rgba)), stops: new Float64Array(stops) };
    }
    if (
      thresholds.some((v) => !Number.isFinite(v) || v < this.min || v > this.max) ||
      thresholds.some((v, i) => i > 0 && v < thresholds[i - 1])
    )
      throw new RangeError("vv: invalid thresholds");
    const stops: number[] = [];
    if (this.colors.length === 1 && thresholds.length === 2) {
      for (let i = 0; i < this.count; ++i)
        stops.push(
          i > 0 && i === this.count - 1
            ? thresholds[1]
            : thresholds[0] + ((thresholds[1] - thresholds[0]) * i) / Math.max(1, this.count - 1),
        );
    } else if (thresholds.length === this.colors.length - 1) {
      const edges = [this.min, ...thresholds, this.max];
      for (let s = 0; s < this.colors.length; ++s) {
        const count = this.colors[s].length;
        for (let i = 0; i < count; ++i)
          stops.push(
            i > 0 && i === count - 1
              ? edges[s + 1]
              : edges[s] + ((edges[s + 1] - edges[s]) * i) / Math.max(1, count - 1),
          );
      }
    } else {
      for (let i = 0; i < this.count; ++i)
        stops.push(this.min + ((this.max - this.min) * i) / Math.max(1, this.count - 1));
    }
    return { rgba: this.toArrayBuffer(), stops: new Float64Array(stops) };
  }

  getColor(value: number): Vec3 {
    if (!this.interpolation) {
      const mapping = this.mapping();
      const index = mapping.stops.findIndex((stop) => Math.fround(stop) === Math.fround(value));
      return index < 0
        ? [0.5, 0.5, 0.5]
        : [mapping.rgba[index * 4], mapping.rgba[index * 4 + 1], mapping.rgba[index * 4 + 2]];
    }
    const colors = this.colors.flat().map(rgba);
    const ratio =
      this.max === this.min
        ? 0
        : Math.max(0, Math.min(1, (value - this.min) / (this.max - this.min)));
    const index = ratio * (colors.length - 1);
    const low = Math.floor(index);
    const high = Math.ceil(index);
    const t = index - Math.floor(index);
    const component = (j: number) => colors[low][j] * (1 - t) + colors[high][j] * t;
    return [component(0), component(1), component(2)];
  }

  static rainbow(min: number, max: number, interpolation = true): Lut {
    return new Lut(
      [
        ["#ff0000"],
        ["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#cc00ff"],
        ["#cc00ff"],
      ],
      min,
      max,
      interpolation,
    );
  }
  static cyclic(min: number, max: number): Lut {
    return new Lut(
      [["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000"]],
      min,
      max,
    );
  }
  static categorical(values: readonly number[]): Lut {
    const palette = [
      "#1f77b4",
      "#ff7f0e",
      "#2ca02c",
      "#d62728",
      "#9467bd",
      "#8c564b",
      "#e377c2",
      "#7f7f7f",
      "#bcbd22",
      "#17becf",
    ];
    if (
      !values.length ||
      values.length > 20 ||
      values.some((v, i) => !Number.isFinite(v) || (i > 0 && v <= values[i - 1]))
    ) {
      throw new RangeError("vv: categories must be finite, unique and sorted (maximum 20)");
    }
    return new Lut(
      [values.map((_, i) => palette[i % palette.length])],
      values[0],
      values[values.length - 1],
      false,
      [...values],
    );
  }
  static extractAlpha(color: string): number {
    return rgba(color)[3];
  }
}
