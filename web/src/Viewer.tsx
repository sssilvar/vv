import {
  useCallback,
  useEffect,
  useId,
  useImperativeHandle,
  useRef,
  useState,
  type CSSProperties,
  type Ref,
  type ReactNode,
} from "react";
import { ViewerEngine } from "./engine";
import type { Lut } from "./lut";
import type {
  CameraState,
  PickResult,
  ClipPlane,
  NumericValues,
  SpatialAnnotation,
  Surface,
  Vec3,
  ViewerStatistics,
} from "./types";

export interface ScalarField {
  values: NumericValues;
  association?: "point" | "cell";
  lut: Lut;
  thresholds?: readonly number[];
  /** Increment after mutating a simulation frame in place. */
  revision?: number;
}

export interface ViewerHandle {
  resetCamera(): void;
  cameraState(): CameraState | undefined;
  setCameraState(state: CameraState): void;
  /** Latest frame wins; values must remain valid until the next animation frame. */
  setScalarValues(values: NumericValues, association?: "point" | "cell"): void;
  project(position: Vec3): Vec3 | undefined;
  statistics(): ViewerStatistics | undefined;
}

export interface ViewerProps {
  ref?: Ref<ViewerHandle>;
  surface: Surface;
  points?: boolean;
  scalar?: ScalarField;
  opacity?: number;
  wireframe?: boolean;
  pointDiameter?: number;
  clippingPlanes?: readonly ClipPlane[];
  annotations?: readonly SpatialAnnotation[];
  backend?: "auto" | "webgl" | "software";
  maxPixelRatio?: number;
  className?: string;
  style?: CSSProperties;
  onReady?: (backend: "webgl" | "software") => void;
  onError?: (error: Error) => void;
  onAnnotationHover?: (annotation: SpatialAnnotation | undefined) => void;
  onAnnotationClick?: (annotation: SpatialAnnotation | undefined) => void;
  onCellHover?: (cell: number | undefined) => void;
  onProbe?: (hit: PickResult | undefined) => void;
  onCameraChange?: () => void;
  focusPosition?: Vec3;
  focusRevision?: number;
  label?: { position: Vec3; content: ReactNode };
}

const emptyPlanes: readonly ClipPlane[] = [];
const emptyAnnotations: readonly SpatialAnnotation[] = [];
const emptyValues: NumericValues = [];

export function Viewer({
  ref,
  surface,
  points = false,
  scalar,
  opacity = 1,
  wireframe = false,
  pointDiameter = 2,
  clippingPlanes = emptyPlanes,
  annotations = emptyAnnotations,
  backend = "auto",
  maxPixelRatio = 1.5,
  className,
  style,
  onReady,
  onError,
  onAnnotationHover,
  onAnnotationClick,
  onProbe,
  onCellHover,
  onCameraChange,
  focusPosition,
  focusRevision,
  label,
}: ViewerProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const engineRef = useRef<ViewerEngine | undefined>(undefined);
  const frameRef = useRef(0);
  const savedCamera = useRef<CameraState | undefined>(undefined);
  const pendingScalar = useRef<{ values: NumericValues; cell: boolean } | undefined>(undefined);
  const readyPending = useRef(false);
  const [engine, setEngine] = useState<ViewerEngine>();
  const [error, setError] = useState<string>();
  const [actualBackend, setActualBackend] = useState<string>("loading");
  const [canvasGeneration, setCanvasGeneration] = useState(0);
  const id = `vv-${useId().replace(/[^\w-]/g, "")}-${canvasGeneration}`;
  const callbacks = useRef({
    onReady,
    onError,
    onAnnotationHover,
    onAnnotationClick,
    onProbe,
    onCellHover,
    onCameraChange,
  });
  callbacks.current = {
    onReady,
    onError,
    onAnnotationHover,
    onAnnotationClick,
    onProbe,
    onCellHover,
    onCameraChange,
  };
  const annotationsRef = useRef(annotations);
  annotationsRef.current = annotations;
  const labelRef = useRef<HTMLDivElement>(null);
  const labelPosition = useRef(label?.position);
  labelPosition.current = label?.position;

  const fail = useCallback((failure: unknown) => {
    const error = failure instanceof Error ? failure : new Error(String(failure));
    setError(error.message);
    callbacks.current.onError?.(error);
  }, []);
  const schedule = useCallback(() => {
    if (frameRef.current) return;
    frameRef.current = requestAnimationFrame(() => {
      frameRef.current = 0;
      try {
        const active = engineRef.current;
        if (!active) return;
        if (pendingScalar.current) {
          const frame = pendingScalar.current;
          pendingScalar.current = undefined;
          active.scalar(frame.values, frame.cell);
        }
        active.render();
        if (readyPending.current) {
          readyPending.current = false;
          callbacks.current.onReady?.(active.backend);
        }
        const canvas = canvasRef.current;
        const overlay = labelRef.current;
        if (canvas && overlay && labelPosition.current) {
          const projected = engineRef.current?.project(labelPosition.current);
          if (projected) {
            overlay.style.display = projected[2] >= 0 && projected[2] <= 1 ? "block" : "none";
            overlay.style.left = `${(projected[0] * canvas.clientWidth) / canvas.width}px`;
            overlay.style.top = `${(projected[1] * canvas.clientHeight) / canvas.height}px`;
          }
        }
      } catch (failure: unknown) {
        fail(failure);
      }
    });
  }, [fail]);

  useImperativeHandle(
    ref,
    () => ({
      cameraState() {
        return engineRef.current?.cameraState();
      },
      setCameraState(state) {
        engineRef.current?.setCameraState(state);
        schedule();
      },
      setScalarValues(values, association = "point") {
        pendingScalar.current = { values, cell: association === "cell" };
        schedule();
      },
      resetCamera() {
        engineRef.current?.camera(0, 0, 0, 0, 0, true);
        schedule();
      },
      project(position) {
        const canvas = canvasRef.current;
        const point = engineRef.current?.project(position);
        return canvas && point
          ? [
              (point[0] * canvas.clientWidth) / canvas.width,
              (point[1] * canvas.clientHeight) / canvas.height,
              point[2],
            ]
          : undefined;
      },
      statistics() {
        return engineRef.current?.statistics();
      },
    }),
    [schedule],
  );

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    let cancelled = false;
    let created: ViewerEngine | undefined;
    const preferred = backend === "auto" && canvasGeneration > 0 ? "software" : backend;
    void ViewerEngine.create(canvas, preferred)
      .then((instance) => {
        if (cancelled) {
          instance.dispose();
          return;
        }
        created = instance;
        engineRef.current = instance;
        setEngine(instance);
        setActualBackend(instance.backend);
        readyPending.current = true;
        setError(undefined);
      })
      .catch((failure: unknown) => {
        if (!cancelled) fail(failure);
      });
    return () => {
      cancelled = true;
      if (frameRef.current) cancelAnimationFrame(frameRef.current);
      frameRef.current = 0;
      if (created) savedCamera.current = created.cameraState();
      engineRef.current = undefined;
      created?.dispose();
    };
  }, [backend, canvasGeneration, fail]);

  useEffect(() => {
    if (!engine || engine !== engineRef.current) return;
    try {
      engine.surface(surface, points);
      if (savedCamera.current) {
        engine.setCameraState(savedCamera.current);
        savedCamera.current = undefined;
      }
      schedule();
    } catch (failure: unknown) {
      fail(failure);
    }
  }, [engine, surface.vertices, surface.indices, points, schedule, fail]);

  useEffect(() => {
    if (!engine || engine !== engineRef.current) return;
    pendingScalar.current = {
      values: scalar?.values ?? emptyValues,
      cell: scalar?.association === "cell",
    };
    schedule();
  }, [
    engine,
    surface.vertices,
    surface.indices,
    points,
    scalar?.values,
    scalar?.association,
    scalar?.revision,
    schedule,
    fail,
  ]);

  useEffect(() => {
    if (!engine || engine !== engineRef.current || !scalar) return;
    try {
      engine.lut(scalar.lut, scalar.thresholds ?? []);
      schedule();
    } catch (failure: unknown) {
      fail(failure);
    }
  }, [
    engine,
    surface.vertices,
    surface.indices,
    scalar?.lut,
    scalar?.thresholds,
    scalar?.association,
    schedule,
    fail,
  ]);

  useEffect(() => {
    if (!engine || engine !== engineRef.current) return;
    try {
      engine.style(surface.color, opacity, wireframe, pointDiameter);
      engine.planes(clippingPlanes);
      schedule();
    } catch (failure: unknown) {
      fail(failure);
    }
  }, [
    engine,
    surface.vertices,
    surface.indices,
    surface.color,
    opacity,
    wireframe,
    pointDiameter,
    clippingPlanes,
    schedule,
    fail,
  ]);

  useEffect(() => {
    if (!engine || engine !== engineRef.current) return;
    try {
      engine.annotations(annotations);
      schedule();
    } catch (failure: unknown) {
      fail(failure);
    }
  }, [engine, annotations, schedule, fail]);

  useEffect(() => {
    if (engine && focusPosition && focusRevision !== undefined) {
      engine.reveal(focusPosition);
      schedule();
    }
  }, [engine, focusRevision, schedule]);

  useEffect(() => {
    schedule();
  }, [label, schedule]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !engine || engine !== engineRef.current) return;
    const resize = () => {
      const bounds = canvas.getBoundingClientRect();
      if (bounds.width <= 0 || bounds.height <= 0) return;
      const ratio = Math.min(
        window.devicePixelRatio || 1,
        Math.max(0.5, Math.min(2, maxPixelRatio)),
        4096 / bounds.width,
        4096 / bounds.height,
        Math.sqrt(8_388_608 / (bounds.width * bounds.height)),
      );
      try {
        engine.resize(
          Math.max(1, Math.floor(bounds.width * ratio)),
          Math.max(1, Math.floor(bounds.height * ratio)),
        );
        schedule();
      } catch (failure: unknown) {
        fail(failure);
      }
    };
    const observer = new ResizeObserver(resize);
    observer.observe(canvas);
    window.addEventListener("resize", resize);
    resize();
    return () => {
      observer.disconnect();
      window.removeEventListener("resize", resize);
    };
  }, [engine, maxPixelRatio, schedule, fail]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    let drag:
      | {
          id: number;
          x: number;
          y: number;
          startX: number;
          startY: number;
          button: number;
          moved: boolean;
        }
      | undefined;
    let hoverFrame = 0;
    let hoverX = 0,
      hoverY = 0;
    const pick = (x: number, y: number) => {
      const rect = canvas.getBoundingClientRect();
      return engineRef.current?.pick(
        ((x - rect.left) * canvas.width) / rect.width,
        ((y - rect.top) * canvas.height) / rect.height,
      );
    };
    const down = (event: PointerEvent) => {
      if (drag) return;
      canvas.focus();
      canvas.setPointerCapture(event.pointerId);
      drag = {
        id: event.pointerId,
        x: event.clientX,
        y: event.clientY,
        startX: event.clientX,
        startY: event.clientY,
        button: event.button,
        moved: false,
      };
      callbacks.current.onAnnotationHover?.(undefined);
      callbacks.current.onCellHover?.(undefined);
      callbacks.current.onProbe?.(undefined);
    };
    const move = (event: PointerEvent) => {
      hoverX = event.clientX;
      hoverY = event.clientY;
      if (drag && drag.id === event.pointerId) {
        const dx = event.clientX - drag.x,
          dy = event.clientY - drag.y;
        drag.x = event.clientX;
        drag.y = event.clientY;
        drag.moved ||= Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) > 3;
        if (drag.button === 2 || event.shiftKey)
          engineRef.current?.camera(0, 0, dx / canvas.clientWidth, dy / canvas.clientHeight);
        else if (drag.button === 1)
          engineRef.current?.camera(0, 0, 0, 0, (-dy / canvas.clientHeight) * 4);
        else
          engineRef.current?.camera(
            (-dx / canvas.clientWidth) * 180,
            (dy / canvas.clientHeight) * 180,
          );
        schedule();
        callbacks.current.onCameraChange?.();
      } else if (
        !hoverFrame &&
        (callbacks.current.onCellHover ||
          callbacks.current.onAnnotationHover ||
          callbacks.current.onProbe)
      ) {
        hoverFrame = requestAnimationFrame(() => {
          hoverFrame = 0;
          const hit = pick(hoverX, hoverY);
          callbacks.current.onProbe?.(hit && hit.cell >= 0 ? hit : undefined);
          callbacks.current.onCellHover?.(hit && hit.cell >= 0 ? hit.cell : undefined);
          callbacks.current.onAnnotationHover?.(
            hit && hit.annotation >= 0 ? annotationsRef.current[hit.annotation] : undefined,
          );
        });
      }
    };
    const up = (event: PointerEvent) => {
      if (!drag || drag.id !== event.pointerId) return;
      if (!drag.moved && event.type === "pointerup") {
        const hit = pick(event.clientX, event.clientY);
        callbacks.current.onAnnotationClick?.(
          hit && hit.annotation >= 0 ? annotationsRef.current[hit.annotation] : undefined,
        );
      }
      drag = undefined;
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    };
    const reset = () => {
      if (drag && canvas.hasPointerCapture(drag.id)) canvas.releasePointerCapture(drag.id);
      drag = undefined;
      if (hoverFrame) cancelAnimationFrame(hoverFrame);
      hoverFrame = 0;
    };
    const wheel = (event: WheelEvent) => {
      event.preventDefault();
      const delta =
        event.deltaY *
        (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? canvas.clientHeight : 1);
      engineRef.current?.camera(0, 0, 0, 0, -delta * 0.002);
      callbacks.current.onCameraChange?.();
      schedule();
    };
    const contextMenu = (event: Event) => event.preventDefault();
    const leave = () => {
      if (hoverFrame) cancelAnimationFrame(hoverFrame);
      hoverFrame = 0;
      callbacks.current.onAnnotationHover?.(undefined);
      callbacks.current.onCellHover?.(undefined);
      callbacks.current.onProbe?.(undefined);
    };
    const lost = (event: Event) => {
      event.preventDefault();
      reset();
      if (backend === "auto") {
        setEngine(undefined);
        setCanvasGeneration((generation) => generation + 1);
      } else fail(new Error("vv: WebGL context lost"));
    };
    canvas.addEventListener("pointerdown", down);
    canvas.addEventListener("pointermove", move);
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointercancel", up);
    canvas.addEventListener("lostpointercapture", reset);
    canvas.addEventListener("pointerleave", leave);
    canvas.addEventListener("wheel", wheel, { passive: false });
    canvas.addEventListener("contextmenu", contextMenu);
    canvas.addEventListener("webglcontextlost", lost);
    window.addEventListener("blur", reset);
    document.addEventListener("visibilitychange", reset);
    return () => {
      reset();
      canvas.removeEventListener("pointerdown", down);
      canvas.removeEventListener("pointermove", move);
      canvas.removeEventListener("pointerup", up);
      canvas.removeEventListener("pointercancel", up);
      canvas.removeEventListener("lostpointercapture", reset);
      canvas.removeEventListener("pointerleave", leave);
      canvas.removeEventListener("wheel", wheel);
      canvas.removeEventListener("contextmenu", contextMenu);
      canvas.removeEventListener("webglcontextlost", lost);
      window.removeEventListener("blur", reset);
      document.removeEventListener("visibilitychange", reset);
    };
  }, [backend, canvasGeneration, schedule, fail]);

  return (
    <div
      className={className}
      style={{ position: "relative", width: "100%", height: "100%", ...style }}
    >
      <canvas
        key={`${backend}-${canvasGeneration}`}
        id={id}
        ref={canvasRef}
        aria-label={`3D view of ${surface.name}`}
        tabIndex={0}
        data-backend={actualBackend}
        style={{
          display: "block",
          width: "100%",
          height: "100%",
          touchAction: "none",
          outline: "none",
        }}
      />
      {error && (
        <div
          role="alert"
          style={{ position: "absolute", inset: 8, color: "#ffb4b4", pointerEvents: "none" }}
        >
          {error}
        </div>
      )}
      {label && (
        <div
          ref={labelRef}
          style={{
            position: "absolute",
            pointerEvents: "none",
            transform: "translate(-50%, -120%)",
          }}
        >
          {label.content}
        </div>
      )}
    </div>
  );
}
