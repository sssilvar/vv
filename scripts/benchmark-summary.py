import json
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def summarize(directory: Path) -> str:
    lines = [f"Benchmark commit: `{os.environ.get('GITHUB_SHA', 'local')}`", ""]
    for path in sorted(directory.glob("cpu-*.xml")):
        root = ET.parse(path).getroot()
        lines.extend([
            f"### {path.stem}",
            "",
            "| Workload | Median ms per operation |",
            "| --- | ---: |",
        ])
        for function in root.findall("TestFunction"):
            for sample in function.findall("BenchmarkResult"):
                if sample.get("metric") not in {"walltime", "WalltimeMilliseconds"}:
                    raise ValueError(f"Unsupported metric in {path}: {sample.attrib}")
                label = function.get("name", "")
                if tag := sample.get("tag"):
                    label += f": {tag}"
                lines.append(f"| {label} | {float(sample.attrib['value']):.3f} |")
        lines.append("")

    render = directory / "render.json"
    if render.exists():
        data = json.loads(render.read_text(encoding="utf-8"))
        lines.extend([
            "### OpenGL frames",
            "",
            f"{data['os']}, {data['architecture']}, Qt {data['qt']}, VTK {data['vtk']}.",
            "",
        ])
        if data["status"] != "measured":
            lines.append(f"Rendering benchmark: **{data['status']}**. {data.get('reason', '')}")
        else:
            lines.extend([
                f"Driver: {data['renderer']} ({data['driver']}). "
                f"Workload: **{data.get('workload', 'full')}**. "
                f"Viewport: {data['width']} × {data['height']}. "
                f"Mesh: {data['points_per_mesh']:,} points and "
                f"{data['triangles_per_mesh']:,} triangles per file.",
                "",
                "| Files | Style | Moving | Style change ms | p50 ms | p95 ms | Depth peeling |",
                "| ---: | --- | --- | ---: | ---: | ---: | --- |",
            ])
            for row in data["measurements"]:
                lines.append(
                    f"| {row['files']} | {row['style']} | {row['moving']} | "
                    f"{row['style_change_ms']:.2f} | {row['p50_ms']:.2f} | "
                    f"{row['p95_ms']:.2f} | {row['depth_peeling']} |"
                )
    lines.extend([
        "",
        "Timings are observations on this runner, not hardware-independent latency guarantees. "
        "Correctness and one-render-per-action assertions are separate gates.",
        "",
    ])
    return "\n".join(lines)


if __name__ == "__main__":
    report = summarize(Path(sys.argv[1]))
    destination = os.environ.get("GITHUB_STEP_SUMMARY")
    if destination:
        with open(destination, "a", encoding="utf-8") as output:
            output.write(report)
    else:
        print(report)
