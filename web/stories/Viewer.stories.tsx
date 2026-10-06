import { useCallback, useEffect, useRef, useState, type RefObject } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Lut, ScalarBar, Viewer, type ViewerHandle, type ViewerProps } from "../src";
import { chamber, voltageFrame } from "../demo/fixtures";

const surface = chamber(48, 24);
const values = new Float32Array(surface.vertices.length / 3);
voltageFrame(surface, values, 0);
const lut = Lut.rainbow(-80, 40);
const scalar = { values, lut };
const cells = Float32Array.from(
  { length: surface.indices.length / 3 },
  (_, i) => -80 + (120 * i) / (surface.indices.length / 3),
);

function Scene({
  viewerRef,
  ...args
}: ViewerProps & { viewerRef?: RefObject<ViewerHandle | null> }) {
  const ref = useRef<ViewerHandle>(null);
  const [status, setStatus] = useState("loading");
  const [probe, setProbe] = useState("Move over the surface to probe");
  return (
    <section style={{ display: "grid", gap: 12 }}>
      <div style={{ display: "flex", gap: 12, flexWrap: "wrap", alignItems: "center" }}>
        <button type="button" onClick={() => (viewerRef ?? ref).current?.resetCamera()}>
          Reset camera
        </button>
        <output aria-label="Renderer">{status}</output>
        <output aria-label="Probe">{probe}</output>
      </div>
      <Viewer
        {...args}
        ref={viewerRef ?? ref}
        style={{ height: "min(70vh, 640px)", minHeight: 240 }}
        onReady={setStatus}
        onError={(error) => setStatus(error.message)}
        onProbe={(hit) => setProbe(hit ? `Cell ${hit.cell}: ${hit.scalar.toFixed(2)}` : "No hit")}
      />
    </section>
  );
}

const meta = {
  title: "Viewer",
  component: Viewer,
  render: (args) => <Scene {...args} />,
  args: {
    surface,
    backend: "auto",
    maxPixelRatio: 1,
    opacity: 1,
    wireframe: false,
    points: false,
    pointDiameter: 3,
  },
  argTypes: {
    backend: { control: "select", options: ["auto", "webgl", "software"] },
    opacity: { control: { type: "range", min: 0, max: 1, step: 0.05 } },
    surface: { control: false },
    scalar: { control: false },
    ref: { control: false },
  },
} satisfies Meta<typeof Viewer>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Plain: Story = {};
export const PointField: Story = { args: { scalar } };
export const CellField: Story = { args: { scalar: { values: cells, lut, association: "cell" } } };
export const Cyclic: Story = { args: { scalar: { values, lut: Lut.cyclic(-80, 40) } } };
export const Categorical: Story = {
  args: {
    scalar: {
      values: Float32Array.from(values, (v) => (v < -40 ? 2 : v < 0 ? 7 : 42)),
      lut: Lut.categorical([2, 7, 42]),
    },
  },
};
export const MissingValues: Story = {
  args: { scalar: { values: Float32Array.from(values, (v, i) => (i % 7 === 0 ? NaN : v)), lut } },
};
export const Saturation: Story = { args: { scalar: { ...scalar, thresholds: [-50, 10] } } };
export const SpatialClipping: Story = {
  args: {
    scalar,
    clippingPlanes: [
      [1, 0, 0, 0],
      [0, 1, 0, 0.4],
    ],
  },
};
export const Wireframe: Story = { args: { scalar, wireframe: true } };
export const PointCloud: Story = {
  args: { surface: { ...surface, indices: new Uint32Array() }, scalar, points: true },
};
export const Transparent: Story = { args: { scalar, opacity: 0.4 } };
export const ShellSides: Story = {
  args: {
    surface: {
      name: "Front face (left), back face (right)",
      vertices: [-1.5, -1, 0, -0.2, -1, 0, -0.85, 1, 0, 0.2, -1, 0, 1.5, -1, 0, 0.85, 1, 0],
      indices: [0, 1, 2, 3, 5, 4],
      color: [0.72, 0.78, 0.82],
    },
    scalar: { values: new Float32Array(6).fill(-20), lut },
  },
};
export const Software: Story = { args: { scalar, backend: "software" } };
export const Annotations: Story = {
  args: {
    scalar,
    annotations: [{ position: [0, 0, 1], color: [1, 1, 0], diameter: 0.15, value: "A" }],
    label: { position: [0, 0, 1], content: "Probe A — synthetic surface" },
  },
};
export const Empty: Story = {
  args: { surface: { name: "Empty", vertices: [], indices: [], color: [1, 1, 1] } },
};
export const InvalidGeometry: Story = {
  args: { surface: { ...surface, indices: [0, 1, surface.vertices.length] } },
};
export const LargeMesh: Story = { args: { surface: chamber(512, 256) } };

function Simulation(args: ViewerProps) {
  const frame = useRef(0);
  const viewer = useRef<ViewerHandle>(null);
  const frameLabel = useRef<HTMLOutputElement>(null);
  const [playing, setPlaying] = useState(false);
  const [thresholds, setThresholds] = useState([-80, 40]);
  const [buffer] = useState(() => new Float32Array(values));
  const showFrame = useCallback(
    (next: number) => {
      frame.current = next;
      voltageFrame(surface, buffer, next);
      viewer.current?.setScalarValues(buffer);
      if (frameLabel.current) frameLabel.current.textContent = String(Math.floor(next));
    },
    [buffer],
  );
  useEffect(() => {
    if (!playing) return;
    let request = 0;
    let previous: number | undefined;
    const tick = (now: number) => {
      // Preserve simulation speed while interpolating at the display refresh rate.
      // Cap elapsed time so returning from a background tab does not jump ahead.
      if (previous !== undefined) showFrame(frame.current + Math.min(now - previous, 100) / 100);
      previous = now;
      request = requestAnimationFrame(tick);
    };
    request = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(request);
  }, [playing, showFrame]);
  return (
    <section style={{ display: "grid", gap: 12 }}>
      <div style={{ display: "flex", gap: 12, flexWrap: "wrap" }}>
        <button type="button" onClick={() => setPlaying(!playing)}>
          {playing ? "Pause" : "Play"}
        </button>
        <button
          type="button"
          disabled={playing}
          onClick={() => showFrame(Math.floor(frame.current) + 1)}
        >
          Next frame
        </button>
        <output ref={frameLabel} aria-label="Frame">
          0
        </output>
      </div>
      <div style={{ display: "grid", gridTemplateColumns: "minmax(0, 1fr) 110px", gap: 12 }}>
        <Scene {...args} viewerRef={viewer} scalar={{ values: buffer, lut, thresholds }} />
        <div style={{ height: 260 }}>
          <ScalarBar lut={lut} thresholds={thresholds} onChange={setThresholds} />
        </div>
      </div>
    </section>
  );
}
export const Streaming: Story = { render: (args) => <Simulation {...args} /> };
