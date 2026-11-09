"""Opt-in visibility, sort-threshold and additive-scene probes; no native build."""
from pathlib import Path
import argparse
import csv
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("common", ROOT / "scripts/benchmarks/material-batching/Run.py")
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)
SCOPES = ("cpu_render_ms", "collect_ms", "sort_ms", "upload_ms", "submit_ms")


def workloads():
    cases = [(n, v, order, static, 1, 1) for n in (100, 2048, 4095, 4096, 4097, 8192)
             for v in (0, 10, 100) for order in (0, 1, 2) for static in (0, 1)]
    return cases + [(8192, 100, 2, 0, scenes, views) for scenes in (1, 4, 16) for views in (1, 2)]


def export(output):
    context = json.loads((output / "context.json").read_text())
    if context["binariesAfter"] != context["binaries"] or context["exitCode"] != 0:
        raise ValueError("Capture failed or binaries changed")
    xml = ET.parse(output / "tests.xml").getroot()
    tests = [t for t in xml.findall(".//TestCase") if t.get("skipped") != "true"]
    if (len(tests) != 1 or tests[0].get("name") != "GPU material batching benchmark preserves visible work and transparent order"
            or xml.find("OverallResultsAsserts").get("failures") != "0"
            or int(xml.find("OverallResultsAsserts").get("successes", 0)) == 0):
        raise ValueError("Missing executed native validation")
    marker = (output / "sprite-matrix.complete.txt").read_text().splitlines()
    if len(marker) != 5 or "Release" not in marker[0] or int(marker[3]) != context["workers"]:
        raise ValueError("Wrong configuration or missing completion marker")
    with (output / "sprite-matrix.csv").open(newline="") as f:
        rows = list(csv.DictReader(f))
    groups = {}
    for row in rows:
        groups.setdefault((int(row["case"]), int(row["repeat"])), []).append(row)
    matrix = workloads()
    if set(groups) != {(i, r) for i in range(len(matrix)) for r in range(context["repeats"])}:
        raise ValueError("Incomplete sprite matrix")
    results = []
    boundary = ("Sum of CPU RenderScenesWithVP calls across every requested view; includes collect, sort, combined scratch copies, upload and submit encoding. "
                "Existing scope columns sum every invocation, never just the last scene. Clear, BeginFrame, queue drain, pixel readback, image validation and export excluded. "
                "Each sample drains GPU work through readback; no GPU execution time, presented frame time or Editor panel measurement.")
    for (index, repeat), values in sorted(groups.items()):
        n, percent, order, static, scenes, views = matrix[index]
        visible = n * percent // 100
        expected = dict(entities=n, visible=visible, order=order, is_static=static, scenes=scenes, views=views,
                        collect_calls=scenes*views, sort_calls=scenes*views if visible >= 2 else 0,
                        draw_calls=scenes*views if visible else 0)
        if len(values) != context["samples"] or [int(r["sample"]) for r in values] != list(range(context["samples"])):
            raise ValueError("Invalid sample sequence")
        if any(any(int(row[key]) != value for key, value in expected.items()) for row in values):
            raise ValueError("Work, scope or draw count mismatch")
        screenshot = output / f"sprite-{index}-r{repeat}.ppm"
        if not screenshot.is_file():
            raise ValueError("Missing validated image")
        results.append({"case": index, "repeat": repeat, "configuration": expected,
                        "timings": {scope: common.distribution([float(r[scope]) for r in values], boundary) for scope in SCOPES},
                        "image": {"file": screenshot.name, "sha256": common.digest(screenshot)}})
    common.write_json(output / "summary.json", {"status": "validated-descriptive", "engine": marker[0], "backend": marker[1], "gpu": marker[2],
        "workersRequested": context["workers"], "boundary": boundary, "cases": results,
        "rawSha256": common.digest(output / "sprite-matrix.csv"),
        "limitations": "Concurrent workloads are recorded in context. Build-time source provenance and individual run epochs/warmup durations are not yet captured; not an eligible strict CI baseline. Input order labels describe authored keys; static-grid/worker traversal may reorder them before sort."})
    lines = ["# Sprite collection and scene/view matrix", "", marker[0], "", boundary, "",
             context["conditions"], "", "Three authored key orders: 0 ascending, 1 descending, 2 deterministic permutation; keys include negative values. All scopes sum scene/view calls. Views are separate equal-sized 1280×720 render targets with identical cameras; they are compared byte for byte. No editor UI is running in this probe.", "",
             "| Case | Entities / visible | Order / static | Scenes / views | Render median range ms | Collect median range | Sort median range | Largest render P95 |", "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for index, work in enumerate(matrix):
        selected = [r for r in results if r["case"] == index]
        ranges = [f"{min(r['timings'][s]['median'] for r in selected):.5f}–{max(r['timings'][s]['median'] for r in selected):.5f}" for s in SCOPES[:3]]
        n, percent, order, static, scenes, views = work
        lines.append(f"| {index} | {n} / {n*percent//100} | {order} / {static} | {scenes} / {views} | {' | '.join(ranges)} | {max(r['timings']['cpu_render_ms']['p95'] for r in selected):.5f} |")
    (output / "measurement.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    host = context["host"].copy()
    candidates = host.pop("gpuCandidates", [])
    drivers = [g["DriverVersion"] for g in candidates if " ".join(g["Name"].lower().split()) == " ".join(marker[2].lower().split())]
    host.update(gpu=marker[2], graphicsBackend=marker[1], gpuDriver=drivers[0] if len(drivers) == 1 else None)
    unavailable = {"build.sourceFingerprint": "Build-time source fingerprint was not frozen by this capture; sourceAtCapture is separate"}
    metadata = {"schemaVersion": 1, "status": "incomplete", "build": {
        "engineVersion": marker[0], "engineCommit": context["sourceAtCapture"]["engineCommit"], "dirty": True,
        "sourceFingerprint": None, "compiler": marker[4], "nativeConfiguration": "Release", "managedConfiguration": None,
        "architecture": "x86_64", "moduleProfile": "full", "binaries": context["binaries"]},
        "host": host, "packages": [], "runs": [], "unavailable": unavailable}
    csv_rows = []
    for result in results:
        index, repeat, config = result["case"], result["repeat"], result["configuration"]
        values = groups[index, repeat]
        raw_path = output / f"sprite-{index}-r{repeat}.samples.csv"
        with raw_path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, list(values[0]))
            writer.writeheader()
            writer.writerows(values)
        timing = result["timings"]["cpu_render_ms"]
        config = config | {"workersRequested": context["workers"], "samples": context["samples"], "warmups": context["warmups"],
                           "fixtureVersion": 2, "conditions": context["conditions"]}
        entry = {"csvRow": len(csv_rows), "runId": f"sprite-{index}-r{repeat}", "repeat": repeat,
            "startedAt": None, "wallDurationSeconds": None,
            "workload": {"scene": f"sprite-{index}", "sceneSha256": hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest(),
                "projectSourceFingerprint": context["harnessSha256"], "configuration": config, "seed": 0,
                "entities": config["entities"], "materials": 1, "lights": 0, "bones": 0, "visibleEntities": config["visible"]},
            "presentation": {"widthPixels": 1280, "heightPixels": 720, "vsync": False, "targetFps": 0,
                             "renderScale": 1, "windowMode": "offscreen equal-sized targets, explicit readback drain"},
            "warmup": {"iterations": context["warmups"], "seconds": None},
            "timings": {"cpuWork": timing, "gpu": None, "frameInterval": None, "setup": None, "commandRecording": None, "commandPlayback": None},
            "memory": {"native": None, "managed": None, "gpu": None},
            "diagnostics": {"scopeSums": result["timings"], "image": result["image"]},
            "rawSamples": [{"file": raw_path.name, "sha256": common.digest(raw_path), "timingScope": "cpuWork", "unit": "ms"}]}
        def missing(value, prefix):
            if isinstance(value, dict):
                for key, item in value.items():
                    missing(item, f"{prefix}.{key}")
            elif value is None:
                unavailable[prefix] = "Not collected or applicable to isolated native CPU rendering scopes"
        missing(entry, f"runs[{len(csv_rows)}]")
        metadata["runs"].append(entry)
        row = dict.fromkeys(common.HEADER, "")
        row.update(benchmark=f"sprite-{index}", entities=config["entities"], workers=context["workers"], samples=timing["count"],
                   mean_ms=timing["mean"], median_ms=timing["median"], p95_ms=timing["p95"], min_ms=timing["min"], max_ms=timing["max"],
                   width=1280, height=720, vsync=False, target_fps=0, engine=marker[0], engine_configuration="Release",
                   graphics_api=marker[1], renderer=marker[2], warmup_iterations=context["warmups"], timing_scope=boundary)
        csv_rows.append(row)
    with (output / "results.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, common.HEADER)
        writer.writeheader()
        writer.writerows(csv_rows)
    metadata["csv"] = {"file": "results.csv", "sha256": common.digest(output / "results.csv")}
    common.write_json(output / "results.metadata.json", metadata)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe")
    parser.add_argument("--workers", type=int, choices=(0, 2, 4, 8), default=4)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--warmups", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--conditions", required=True, help="Observed host load and capture limitations")
    parser.add_argument("--export-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if args.export_only:
        export(output)
        return
    if any(n < 1 or n > 10000 for n in (args.samples, args.warmups, args.repeats)):
        parser.error("Sample settings must be between 1 and 10000")
    binary = args.binary.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=False)
    files = [binary, *binary.parent.glob("*.dll")]
    hashes = lambda: {p.name: common.digest(p) for p in files}
    context = {"binaries": hashes(), "sourceAtCapture": common.identity(), "host": common.host_info(),
               "conditions": args.conditions, "samples": args.samples, "repeats": args.repeats, "warmups": args.warmups, "workers": args.workers,
               "harnessSha256": common.digest(ROOT / "Tests/Funke-Engine-Tests/src/MaterialBatchingBenchmarkTests.cpp")}
    common.write_json(output / "context.json", context)
    env = os.environ | {"FUNKE_BATCH_BENCH": "1", "FUNKE_SPRITE_MATRIX": "1", "FUNKE_BATCH_OUTPUT": str(output),
          "FUNKE_SPRITE_WORKERS": str(args.workers), "FUNKE_BATCH_SAMPLES": str(args.samples), "FUNKE_BATCH_REPEATS": str(args.repeats),
          "FUNKE_BATCH_WARMUPS": str(args.warmups), "FUNKE_USER_DATA_ROOT": str(output / "user-data")}
    command = [str(binary), "--test-case=GPU material batching benchmark*", "--no-skip", "--reporters=xml", f"--out={output / 'tests.xml'}"]
    begin = time.monotonic()
    with (output / "run.log").open("wb") as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800, creationflags=subprocess.CREATE_NO_WINDOW)
    context.update(exitCode=result.returncode, wallSeconds=time.monotonic()-begin, binariesAfter=hashes())
    common.write_json(output / "context.json", context)
    if re.search(r"WebGPU \[(Validation|OutOfMemory|Internal)\]|Device lost", (output / "run.log").read_text(errors="replace")):
        raise RuntimeError("Graphics validation failed")
    export(output)
    print(f"Validated: {output / 'measurement.md'}")


if __name__ == "__main__":
    main()
