import { StrictMode, useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  Viewer,
  Lut,
  ScalarBar,
  type ViewerHandle,
  type ClipPlane,
  type SpatialAnnotation,
  type ViewerStatistics,
} from "../src";
import { chamber, voltageFrame } from "./fixtures";
import "./style.css";

function Demo() {
  const query = new URLSearchParams(window.location.search);
  const software = query.get("backend") === "software";
  const large = query.has("large");
  const surface = useMemo(() => chamber(large ? 512 : 128, large ? 256 : 64), [large]);
  const viewerRef = useRef<ViewerHandle>(null);
  const [field, setField] = useState("Voltage");
  const [frame, setFrame] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [cyclic, setCyclic] = useState(false);
  const [thresholds, setThresholds] = useState([-80, 40]);
  const [clipX, setClipX] = useState(false);
  const [wireframe, setWireframe] = useState(false);
  const [opacity, setOpacity] = useState(1);
  const [status, setStatus] = useState("loading");
  const [hover, setHover] = useState("");
  const [clicked, setClicked] = useState("");
  const [probe, setProbe] = useState<number>();
  const [mounted, setMounted] = useState(true);
  const [statistics, setStatistics] = useState<ViewerStatistics>();
  const frameRef = useRef(0);
  const lut = useMemo(
    () =>
      field === "Regions"
        ? Lut.categorical([1, 2])
        : cyclic
          ? Lut.cyclic(-80, 40)
          : Lut.rainbow(-80, 40),
    [field, cyclic],
  );
  const values = useMemo(() => {
    if (field === "Regions")
      return Float32Array.from({ length: surface.indices.length / 3 }, (_, i) =>
        surface.vertices[surface.indices[i * 3] * 3] < 0 ? 1 : 2,
      );
    const values = new Float32Array(surface.vertices.length / 3);
    voltageFrame(surface, values, frameRef.current);
    return values;
  }, [field, surface]);
  const showFrame = (next: number) => {
    frameRef.current = next;
    if (field === "Voltage") voltageFrame(surface, values, next);
    setFrame(next);
  };
  useEffect(() => {
    if (!playing || !mounted || field !== "Voltage") return;
    let request = 0;
    let previous = 0;
    const tick = (now: number) => {
      if (now - previous >= 1000 / 30) {
        previous = now;
        frameRef.current = (frameRef.current + 1) % 240;
        voltageFrame(surface, values, frameRef.current);
        viewerRef.current?.setScalarValues(values);
        setFrame(frameRef.current);
      }
      request = requestAnimationFrame(tick);
    };
    request = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(request);
  }, [playing, mounted, field, surface, values]);
  const planes = useMemo<ClipPlane[]>(() => (clipX ? [[1, 0, 0, 0]] : []), [clipX]);
  const annotations = useMemo<SpatialAnnotation[]>(
    () => [{ position: [0, 0, 1.03], color: [0.96, 0.97, 1], diameter: 0.11, value: "Tag A" }],
    [],
  );

  return (
    <main className="workspace">
      <div className="explorer">
        <aside className="inspector">
          <div role="toolbar" aria-label="Viewer controls" className="controls">
            <label>
              Field
              <select
                aria-label="Scalar"
                value={field}
                onChange={(e) => {
                  setPlaying(false);
                  setField(e.currentTarget.value);
                }}
              >
                <option>Voltage</option>
                <option>Regions</option>
                <option>None</option>
              </select>
            </label>
            <button
              aria-pressed={cyclic}
              disabled={field !== "Voltage"}
              onClick={() => setCyclic((v) => !v)}
            >
              Cyclic palette
            </button>
            <div className="button-pair">
              <button aria-pressed={clipX} onClick={() => setClipX((v) => !v)}>
                Clip X
              </button>
              <button aria-pressed={wireframe} onClick={() => setWireframe((v) => !v)}>
                Wireframe
              </button>
            </div>
            <label>
              Opacity <span>{Math.round(opacity * 100)}%</span>
              <input
                aria-label="Opacity"
                type="range"
                min="0"
                max="1"
                step="0.05"
                value={opacity}
                onChange={(e) => setOpacity(e.currentTarget.valueAsNumber)}
              />
            </label>
            <button onClick={() => viewerRef.current?.resetCamera()}>Reset camera</button>
          </div>
        </aside>
        <section className="viewport-panel">
          <div className="viewport">
            {mounted && (
              <Viewer
                ref={viewerRef}
                surface={surface}
                backend={software ? "software" : "auto"}
                scalar={
                  field === "None"
                    ? undefined
                    : {
                        values,
                        revision: playing ? undefined : frame,
                        association: field === "Regions" ? "cell" : "point",
                        lut,
                        thresholds,
                      }
                }
                wireframe={wireframe}
                opacity={opacity}
                clippingPlanes={planes}
                annotations={annotations}
                onReady={setStatus}
                onProbe={(hit) => setProbe(hit?.scalar)}
                onAnnotationHover={(item) => setHover(String(item?.value ?? ""))}
                onAnnotationClick={(item) => setClicked(String(item?.value ?? ""))}
              />
            )}
            {field !== "None" && (
              <aside className="legend">
                <ScalarBar lut={lut} thresholds={thresholds} onChange={setThresholds} />
              </aside>
            )}
            <div className="view-hint">Drag to rotate · Shift-drag to pan · Scroll to zoom</div>
          </div>
          <div className="timeline">
            <button
              className="play"
              disabled={field !== "Voltage"}
              onClick={() => setPlaying((v) => !v)}
            >
              {playing ? "Pause" : "Play"}
            </button>
            <button disabled={playing} onClick={() => showFrame((frame + 1) % 240)}>
              Next frame
            </button>
            <input
              aria-label="Simulation frame"
              type="range"
              min="0"
              max="239"
              value={frame}
              onChange={(e) => {
                setPlaying(false);
                showFrame(e.currentTarget.valueAsNumber);
              }}
            />
            <span>
              Frame <output data-testid="frame">{frame}</output> / 239
            </span>
          </div>
        </section>
      </div>
      <footer>
        <span>
          vv · Synthetic chamber · <output data-testid="backend">{status}</output>
        </span>
        <span>
          {probe !== undefined && Number.isFinite(probe)
            ? `${probe.toFixed(2)}${field === "Voltage" ? " mV" : ""}`
            : ""}
        </span>
        <span>
          Hover <output data-testid="hover">{hover}</output> · Selected{" "}
          <output data-testid="selected">{clicked}</output>
        </span>
        <details>
          <summary>Diagnostics</summary>
          <button onClick={() => setStatistics(viewerRef.current?.statistics())}>Statistics</button>
          <button onClick={() => setMounted((v) => !v)}>{mounted ? "Unmount" : "Mount"}</button>
          <pre data-testid="stats">{statistics ? JSON.stringify(statistics) : ""}</pre>
        </details>
      </footer>
    </main>
  );
}

const root = document.getElementById("root");
if (!root) throw new Error("Missing demo root");
createRoot(root).render(
  <StrictMode>
    <Demo />
  </StrictMode>,
);
