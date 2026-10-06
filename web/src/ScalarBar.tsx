import { useRef, useState } from "react";
import type { Lut } from "./lut";

export interface LutSliderProps {
  lut: Lut;
  thresholds: number[];
  onChange?: (thresholds: number[]) => void;
  valueFormatter?: (value: number) => string;
  step?: number;
  editableThresholds?: boolean;
  dashHelpers?: number;
}

export function LutSlider({
  lut,
  thresholds,
  onChange,
  valueFormatter = (v) => v.toPrecision(3),
  step,
  editableThresholds = true,
  dashHelpers = 0,
}: LutSliderProps) {
  const [editing, setEditing] = useState<number>();
  const barRef = useRef<HTMLDivElement>(null);
  const range = lut.max - lut.min;
  const { rgba, stops } = lut.mapping(thresholds);
  const colors = Array.from(
    stops,
    (_, i) =>
      `rgba(${rgba[i * 4] * 255}, ${rgba[i * 4 + 1] * 255}, ${rgba[i * 4 + 2] * 255}, ${rgba[i * 4 + 3]})`,
  );
  const gradient = colors
    .map((color, i) => `${color} ${range ? (100 * (stops[i] - lut.min)) / range : 0}%`)
    .join(",");
  const update = (index: number, value: number) => {
    if (!Number.isFinite(value)) return;
    const low = index ? thresholds[index - 1] : lut.min;
    const high = index + 1 < thresholds.length ? thresholds[index + 1] : lut.max;
    const next = [...thresholds];
    next[index] = Math.max(low, Math.min(high, value));
    onChange?.(next);
  };

  if (!lut.interpolation) {
    return (
      <div
        aria-label="Scalar bar"
        style={{ display: "flex", flexDirection: "column", gap: 6, color: "white", fontSize: 12 }}
      >
        {Array.from(stops, (value, i) => (
          <div key={value} style={{ display: "flex", alignItems: "center", gap: 6 }}>
            <span style={{ width: 14, height: 14, background: colors[i], borderRadius: 2 }} />
            <span>{valueFormatter(value)}</span>
          </div>
        ))}
      </div>
    );
  }

  return (
    <div
      aria-label="Scalar bar"
      style={{ display: "flex", gap: 6, height: "100%", color: "white", fontSize: 12 }}
    >
      <div style={{ position: "relative", margin: "20px 0", width: 100 }} ref={barRef}>
        <span style={{ position: "absolute", bottom: "100%", paddingBottom: 5 }}>
          {valueFormatter(lut.max)}
        </span>
        <div
          data-testid="scalar-gradient"
          style={{
            width: 14,
            height: "100%",
            background: colors.length === 1 ? colors[0] : `linear-gradient(to top, ${gradient})`,
          }}
        />
        <span style={{ position: "absolute", top: "100%", paddingTop: 5 }}>
          {valueFormatter(lut.min)}
        </span>
        {thresholds.map((value, index) => (
          <div
            key={index}
            style={{
              position: "absolute",
              left: 12,
              top: `${range ? (100 * (lut.max - value)) / range : 50}%`,
              transform: "translateY(-50%)",
              display: "flex",
              alignItems: "center",
            }}
          >
            <button
              type="button"
              role="slider"
              aria-label={
                index === 0
                  ? "Clip minimum"
                  : index === thresholds.length - 1
                    ? "Clip maximum"
                    : `Clip stop ${index + 1}`
              }
              aria-orientation="vertical"
              aria-valuemin={index ? thresholds[index - 1] : lut.min}
              aria-valuemax={index + 1 < thresholds.length ? thresholds[index + 1] : lut.max}
              aria-valuenow={value}
              disabled={!onChange || range === 0}
              onPointerDown={(event) => {
                if (event.button !== 0) return;
                event.currentTarget.setPointerCapture(event.pointerId);
              }}
              onPointerMove={(event) => {
                if (!event.currentTarget.hasPointerCapture(event.pointerId)) return;
                const rect = barRef.current?.getBoundingClientRect();
                if (rect?.height)
                  update(index, lut.max - ((event.clientY - rect.top) / rect.height) * range);
              }}
              onPointerUp={(event) => event.currentTarget.releasePointerCapture(event.pointerId)}
              onKeyDown={(event) => {
                const increment = step ?? range / 100;
                const next =
                  event.key === "ArrowUp" || event.key === "ArrowRight"
                    ? value + increment
                    : event.key === "ArrowDown" || event.key === "ArrowLeft"
                      ? value - increment
                      : event.key === "Home"
                        ? lut.min
                        : event.key === "End"
                          ? lut.max
                          : undefined;
                if (next !== undefined) {
                  event.preventDefault();
                  update(index, next);
                }
              }}
              style={{
                padding: "3px 0",
                border: 0,
                background: "transparent",
                color: "#ddd",
                cursor: "ns-resize",
                touchAction: "none",
                fontSize: 16,
              }}
            >
              ▶
            </button>
            {editableThresholds && editing === index ? (
              <input
                autoFocus
                onBlur={() => setEditing(undefined)}
                onKeyDown={(event) => {
                  if (event.key === "Enter" || event.key === "Escape") setEditing(undefined);
                }}
                aria-label={`Threshold ${index + 1}`}
                type="number"
                min={index ? thresholds[index - 1] : lut.min}
                max={index + 1 < thresholds.length ? thresholds[index + 1] : lut.max}
                step={step ?? "any"}
                value={Number(value.toPrecision(6))}
                disabled={!onChange}
                onChange={(event) => update(index, event.currentTarget.valueAsNumber)}
                style={{
                  width: 70,
                  color: "inherit",
                  background: "transparent",
                  border: 0,
                  font: "inherit",
                }}
              />
            ) : (
              <span
                aria-label={`Threshold ${index + 1}`}
                onDoubleClick={() => {
                  if (editableThresholds && onChange) setEditing(index);
                }}
                style={{ cursor: editableThresholds && onChange ? "text" : "default" }}
              >
                {valueFormatter(value)}
              </span>
            )}
          </div>
        ))}
        {Array.from({ length: Math.min(20, Math.max(0, dashHelpers)) }, (_, i) => (
          <span
            key={i}
            style={{
              position: "absolute",
              right: "100%",
              top: `${(100 * (i + 1)) / (dashHelpers + 1)}%`,
            }}
          >
            −
          </span>
        ))}
      </div>
    </div>
  );
}

export const ScalarBar = LutSlider;
