import { expect, test } from "@playwright/test";
import { Lut } from "../src/lut";

test("clip endpoints preserve saturation colors within a fixed simulation range", () => {
  const lut = Lut.rainbow(-80, 40);

  const mapped = lut.mapping([-30, 10]);

  expect([...mapped.stops]).toEqual([-80, -30, -22, -14, -6, 2, 10, 10]);
  expect(mapped.rgba.slice(0, 4)).toEqual(mapped.rgba.slice(4, 8));
  expect(lut.min).toBe(-80);
  expect(lut.max).toBe(40);
});

test("Sandboxer shared segment boundaries retain one color per categorical label", () => {
  const lut = new Lut(
    [
      ["#ff0000", "#00ff00"],
      ["#00ff00", "#0000ff"],
    ],
    1,
    3,
    false,
  );

  const mapped = lut.mapping([2]);

  expect([...mapped.stops]).toEqual([1, 2, 3]);
  expect(mapped.rgba.length).toBe(12);
});

test("nonconsecutive labels and a constant field are valid categorical domains", () => {
  const labels = Lut.categorical([-1, 7, 100]);
  const constant = Lut.categorical([7]);

  expect([...labels.mapping().stops]).toEqual([-1, 7, 100]);
  expect(labels.getColor(7)).toEqual([
    1,
    expect.closeTo(127 / 255, 5),
    expect.closeTo(14 / 255, 5),
  ]);
  expect([...constant.mapping().stops]).toEqual([7]);
  expect(
    Lut.rainbow(7, 7)
      .mapping([7, 7])
      .stops.every((value) => value === 7),
  ).toBe(true);
});

test("malformed LUTs and nonfinite clipping inputs are rejected", () => {
  expect(() => new Lut([], 0, 1)).toThrow();
  expect(() => new Lut([["#gg0000"]], 0, 1)).toThrow();
  expect(() => Lut.categorical([7, 7])).toThrow();
  expect(() => Lut.rainbow(0, 1).mapping([0.8, 0.2])).toThrow();
  expect(() => Lut.rainbow(0, 1).mapping([0, Infinity])).toThrow();
});

test("cyclic palettes honor both saturation endpoints", () => {
  const lut = Lut.cyclic(-80, 40);

  const mapping = lut.mapping([-30, 10]);

  expect(mapping.stops[0]).toBe(-30);
  expect(mapping.stops[mapping.stops.length - 1]).toBe(10);
  expect(mapping.rgba.slice(0, 4)).toEqual(mapping.rgba.slice(-4));
  expect(lut.min).toBe(-80);
  expect(lut.max).toBe(40);
});

test("fractional clipping limits keep duplicate boundary stops ordered", () => {
  const lut = Lut.rainbow(-80, 40);

  const { stops } = lut.mapping([-79.92799279927993, 40]);

  expect(stops[stops.length - 2]).toBe(40);
  expect([...stops].every((value, i) => i === 0 || value >= stops[i - 1])).toBe(true);
});
