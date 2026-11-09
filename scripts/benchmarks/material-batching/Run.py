"""Opt-in GPU harness and FunkeBench v1 export; builds are deliberately external."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
HEADER = "timestamp,benchmark,entities,jobs,animate,samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,fps,entities_per_second,setup_ms,recording_mean_ms,playback_mean_ms,workers,width,height,vsync,target_fps,engine,engine_configuration,scripts_configuration,graphics_api,renderer,warmup_iterations,warmup_seconds,requested_seconds,captured_seconds,timing_scope".split(",")
CPU_BOUNDARY = "Existing Renderer2D.Submit scope: pass setup, material resolution, bindings, DrawIndexed encoding, pass end and query resolve encoding; excludes collection/sort/upload, queue submission, presentation, readback and pacing."
GPU_BOUNDARY = "GPU timestamp pair at beginning/end of renderer2d-sprites pass; excludes clear, uploads, query resolve/readback and presentation. One pass is drained before the next is submitted."
MODES = ("duplicates-grouped", "shared", "distinct-grouped", "duplicates-interleaved")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def git(*args: str, cwd: Path = ROOT) -> bytes:
    return subprocess.check_output(["git", "-C", str(cwd), *args])


def identity() -> dict:
    changed = git("diff", "HEAD", "--binary")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
    fingerprint = hashlib.sha256(changed)
    for name in sorted(filter(None, untracked)):
        path = ROOT / name
        if path.is_file():
            fingerprint.update(name.encode() + b"\0" + bytes.fromhex(digest(path)))
    submodules = git("submodule", "status", "--recursive")
    fingerprint.update(submodules)
    for line in submodules.decode().splitlines():
        path = ROOT / line.strip().split()[1]
        fingerprint.update(git("diff", "HEAD", "--binary", cwd=path))
        for name in filter(None, git("ls-files", "--others", "--exclude-standard", "-z", cwd=path).decode().split("\0")):
            file = path / name
            if file.is_file():
                fingerprint.update(str(file.relative_to(ROOT)).encode() + bytes.fromhex(digest(file)))
    return {"engineCommit": git("rev-parse", "HEAD").decode().strip(),
            "dirty": bool(git("status", "--porcelain")), "sourceFingerprint": fingerprint.hexdigest(),
            "submodules": submodules.decode().splitlines()}


def host_info() -> dict:
    host = {"os": platform.system(), "osVersion": platform.version(), "cpu": platform.processor(),
            "logicalProcessors": os.cpu_count(), "ramBytes": None, "gpuCandidates": []}
    if os.name == "nt":
        command = "[ordered]@{ram=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;cpu=(Get-CimInstance Win32_Processor).Name;gpus=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)} | ConvertTo-Json -Depth 4 -Compress"
        data = json.loads(subprocess.check_output(["powershell", "-NoProfile", "-Command", command],
                                                 creationflags=subprocess.CREATE_NO_WINDOW))
        host.update(ramBytes=data["ram"], cpu=data["cpu"], gpuCandidates=data["gpus"])
    return host


def distribution(values: list[float], boundary: str, unit="ms") -> dict:
    if not values or any(not math.isfinite(x) or x < 0 for x in values):
        raise ValueError("Samples must be nonempty, finite and nonnegative")
    ordered = sorted(values)
    return {"unit": unit, "count": len(values), "mean": statistics.mean(values),
            "median": statistics.median(values), "p95": ordered[math.ceil(.95 * len(values)) - 1],
            "p99": ordered[math.ceil(.99 * len(values)) - 1], "min": ordered[0], "max": ordered[-1],
            "stddev": statistics.stdev(values) if len(values) > 1 else 0, "boundaries": boundary}


def load_samples(path: Path, run: dict, timestamps: bool) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    if len(rows) != run["samples"]:
        raise ValueError("Missing or extra raw samples")
    expected = 1 if run["mode"] == "shared" else (10000 if run["mode"] == "duplicates-interleaved" and run["materials"] > 1 else run["materials"])
    for index, row in enumerate(rows):
        if int(row["sample"]) != index or int(row["visible_sprites"]) != 10000 or int(row["draw_calls"]) != expected:
            raise ValueError("Raw sample identity, visibility or draw count mismatch")
        row["cpu_submit_ms"] = float(row["cpu_submit_ms"])
        row["gpu_ms"] = float(row["gpu_ms"]) if row["gpu_ms"] else None
        if timestamps != (row["gpu_ms"] is not None):
            raise ValueError("Timestamp availability does not match raw samples")
    return rows


def export(output: Path) -> dict:
    capture = json.loads((output / "capture.json").read_text(encoding="utf-8-sig"))
    context = json.loads((output / "context.json").read_text(encoding="utf-8-sig"))
    if not capture["transparencyAndMutationPassed"]:
        raise ValueError("Transparency/mutation regression did not pass")
    expected = {(r, m, mode) for r in range(context["repeats"]) for m in (1, 10, 100, 1000) for mode in MODES}
    actual = [(r["repeat"], r["requestedMaterials"], r["mode"]) for r in capture["runs"]]
    if set(actual) != expected or len(actual) != len(expected):
        raise ValueError("Incomplete or duplicate material/repetition matrix")
    if capture["nativeConfiguration"] != context["configuration"]:
        raise ValueError("Requested build configuration does not match the executable")
    unavailable = {}
    build = {key: context["source"][key] for key in ("engineCommit", "dirty", "sourceFingerprint")}
    build["materialResolution"] = context.get("materialResolution", "historical-unrecorded")
    build.update(engineVersion=capture["engineVersion"], nativeConfiguration=capture["nativeConfiguration"],
                 managedConfiguration=None, moduleProfile="standard engine; Minimal application; Renderer2D; profiler on; no packages/jobs/scripting",
                 compiler=capture["compiler"], architecture=context["architecture"], binaries=context["binaries"])
    unavailable["build.managedConfiguration"] = "No managed assembly is used"
    host = dict(context["host"])
    host.update(gpu=capture["gpu"], graphicsBackend=capture["graphicsBackend"], gpuDriver=None)
    normalize = lambda value: " ".join(value.lower().split())
    drivers = [g["DriverVersion"] for g in host.pop("gpuCandidates") if normalize(g["Name"]) == normalize(capture["gpu"])]
    if len(drivers) == 1:
        host["gpuDriver"] = drivers[0]
    for key in ("gpuDriver", "ramBytes", "cpu"):
        if not host[key]:
            unavailable[f"host.{key}"] = "Collector could not identify this value unambiguously"
    if not build["compiler"]:
        unavailable["build.compiler"] = "Compiler did not provide a version macro"
    metadata = {"schemaVersion": 1, "status": "complete", "build": build, "host": host, "packages": [],
                "runs": [], "unavailable": unavailable, "capture": {"file": "capture.json", "sha256": digest(output / "capture.json")}}
    csv_rows = []
    for index, run in enumerate(capture["runs"]):
        name = run["runId"]
        rows = load_samples(output / f"{name}.samples.csv", run, capture["timestampQueries"])
        cpu = distribution([r["cpu_submit_ms"] for r in rows], CPU_BOUNDARY)
        gpu = distribution([r["gpu_ms"] for r in rows], GPU_BOUNDARY) if capture["timestampQueries"] else None
        started = dt.datetime.fromtimestamp(run["startedAtEpoch"], dt.timezone.utc).isoformat()
        configuration = {"mode": run["mode"], "requestedMaterials": run["requestedMaterials"],
                         "grid": [100, 100], "spriteWorldSize": [.016, .016], "viewBounds": [-1, -1, 1, 1],
                         "sort": "unique ascending SortingOrder by grid index", "texture": "explicit white RGBA8 1x1 point clamp",
                         "shaderSha256": digest(output / "probe.wgsl"), "postProcessing": False, "antiAliasing": "Off",
                         "submission": "serial offscreen passes; drain timestamp after each pass; no overlap; no vsync or presentation",
                         "cache": "warm; reused GUID range and renderer caches across cases; case order rotated by repeat",
                         "samples": run["samples"], "warmups": run["warmupIterations"]}
        workload_hash = hashlib.sha256(json.dumps(configuration, sort_keys=True).encode()).hexdigest()
        timing = {"cpuWork": cpu, "gpu": gpu, "frameInterval": None,
                  "setup": distribution([run["setupMs"]], "Material assignment and detached scene/entity construction; before warmup"),
                  "commandRecording": None, "commandPlayback": None}
        entry = {"csvRow": index, "runId": name, "repeat": run["repeat"], "startedAt": started,
                 "wallDurationSeconds": run["wallDurationSeconds"],
                 "workload": {"projectSourceFingerprint": context["harnessSourceFingerprint"],
                              "scene": "Procedural 100x100 detached sprite grid", "sceneSha256": workload_hash,
                              "configuration": configuration, "seed": 0, "entities": 10000,
                              "materials": run["materials"], "lights": 0, "bones": 0, "visibleEntities": 10000},
                 "presentation": {"widthPixels": 1280, "heightPixels": 720, "vsync": False, "targetFps": 0,
                                  "renderScale": 1, "windowMode": "offscreen RGBA8; background 160x120 host window"},
                 "warmup": {"iterations": run["warmupIterations"], "seconds": run["warmupSeconds"]},
                 "timings": timing, "memory": dict(native=None, managed=None, gpu=None),
                 "drawCalls": distribution([int(r["draw_calls"]) for r in rows], "Actual Renderer2D DrawIndexed counter per pass", "draws"),
                 "rawSamples": [{"file": f"{name}.samples.csv", "sha256": digest(output / f"{name}.samples.csv"),
                                 "timingScope": scope, "unit": "ms"} for scope in (["cpu_submit_ms", "gpu_ms"] if gpu else ["cpu_submit_ms"])],
                 "screenshot": {"file": f"{name}.ppm", "sha256": digest(output / f"{name}.ppm")}}
        prefix = f"runs[{index}]"
        for scope in ("frameInterval", "commandRecording", "commandPlayback"):
            unavailable[f"{prefix}.timings.{scope}"] = "Not measured by the isolated sprite-pass harness"
        for kind in ("native", "managed", "gpu"):
            unavailable[f"{prefix}.memory.{kind}"] = "No memory collector in this harness"
        if gpu is None:
            unavailable[f"{prefix}.timings.gpu"] = "Selected WebGPU device does not support timestamp queries"
        metadata["runs"].append(entry)
        row = dict.fromkeys(HEADER, "")
        row.update(timestamp=started, benchmark=f"MaterialBatching/{name}", entities=10000, jobs="False", animate="False",
                   samples=cpu["count"], mean_ms=cpu["mean"], median_ms=cpu["median"], p95_ms=cpu["p95"],
                   min_ms=cpu["min"], max_ms=cpu["max"], setup_ms=run["setupMs"], workers=0, width=1280, height=720,
                   vsync="False", target_fps=0, engine=capture["engineVersion"], engine_configuration=capture["nativeConfiguration"],
                   graphics_api=capture["graphicsBackend"], renderer=capture["gpu"], warmup_iterations=run["warmupIterations"],
                   warmup_seconds=run["warmupSeconds"], timing_scope="Renderer2D.Submit CPU only")
        csv_rows.append(row)
    with (output / "results.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, HEADER)
        writer.writeheader()
        writer.writerows(csv_rows)
    metadata["csv"] = {"file": "results.csv", "sha256": digest(output / "results.csv")}
    if not capture["timestampQueries"] or not host["gpuDriver"] or not build["compiler"]:
        metadata["status"] = "incomplete"
    write_json(output / "results.metadata.json", metadata)
    report(output, metadata)
    return metadata


def report(output: Path, metadata: dict) -> None:
    host, build = metadata["host"], metadata["build"]
    lines = ["# Material batching measurement", "", f"Commit `{build['engineCommit']}`; dirty `{build['dirty']}`; source fingerprint `{build['sourceFingerprint']}`.",
             f"{host['gpu']} / {host['gpuDriver']} / {host['graphicsBackend']}; {host['cpu']}; {host['os']} {host['osVersion']}.",
             f"{build['nativeConfiguration']} / {build['compiler']}; 10,000 visible sprites; 1280 × 720 RGBA8; offscreen; profiler enabled.", "",
             "Each row pools all repetitions; raw samples, per-repeat distributions and images remain in the sidecar. Units are ms; ± is sample standard deviation. CPU-submit and GPU-pass scopes exclude waits/readback. These are serial isolated passes, not application FPS.", "",
             "| Mode | Requested / referenced materials | Draws | CPU mean ± SD / P95 | GPU mean ± SD / P95 | Repeat mean range CPU / GPU |", "|---|---:|---:|---:|---:|---:|"]
    for mode in MODES:
        for count in (1, 10, 100, 1000):
            runs = [r for r in metadata["runs"] if r["workload"]["configuration"]["mode"] == mode and r["workload"]["configuration"]["requestedMaterials"] == count]
            values = [[], []]
            for run in runs:
                with (output / run["rawSamples"][0]["file"]).open(newline="") as file:
                    for row in csv.DictReader(file):
                        values[0].append(float(row["cpu_submit_ms"]))
                        if row["gpu_ms"]:
                            values[1].append(float(row["gpu_ms"]))
            summaries, ranges = [], []
            for scope, data in zip(("cpuWork", "gpu"), values):
                if data:
                    d = distribution(data, "pooled")
                    summaries.append(f"{d['mean']:.4f} ± {d['stddev']:.4f} / {d['p95']:.4f}")
                    means = [r["timings"][scope]["mean"] for r in runs]
                    ranges.append(f"{min(means):.4f}–{max(means):.4f}")
                else:
                    summaries.append("unavailable")
                    ranges.append("unavailable")
            lines.append(f"| {mode} | {count} / {runs[0]['workload']['materials']} | {runs[0]['drawCalls']['mean']:g} | {summaries[0]} | {summaries[1]} | {' / '.join(ranges)} |")
    lines += ["", "For a 16.67 ms budget per measured scope, requiring every repetition's P95 to fit:", ""]
    for mode in MODES:
        groups = [r for r in metadata["runs"] if r["workload"]["configuration"]["mode"] == mode]
        for scope in ("cpuWork", "gpu"):
            if any(r["timings"][scope] is None for r in groups):
                lines.append(f"- {mode}, {scope}: unavailable.")
                continue
            passing = [count for count in (1, 10, 100, 1000) if all(r["timings"][scope]["p95"] <= 16.67
                       for r in groups if r["workload"]["configuration"]["requestedMaterials"] == count)]
            failing = sorted({r["workload"]["configuration"]["requestedMaterials"] for r in groups if r["timings"][scope]["p95"] > 16.67})
            lines.append(f"- {mode}, {scope}: passing requested counts {passing}; over budget {failing}.")
    lines += ["", "These are observed limits for this device, workload and isolated scope. The tested material range ends at 1,000; passing it is only a lower bound on capacity. Interleaving can produce 10,000 draws even with only 10 materials. No full-frame or general FPS guarantee follows from these measurements.", ""]
    (output / "measurement.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe")
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--warmups", type=int, default=20)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--export-only", action="store_true")
    parser.add_argument("--reference-material-resolution", action="store_true",
                        help="Disable the pass-local resolution cache for a same-binary comparison")
    args = parser.parse_args()
    output = args.output.resolve()
    if args.export_only:
        export(output)
        return
    if any(n < 1 or n > 10000 for n in (args.warmups, args.samples, args.repeats)):
        parser.error("Sample settings must be between 1 and 10000")
    exe = args.exe.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=False)
    assets = exe.parent / "FunkeAssets"
    if not assets.exists() and not (exe.parent.parent / "FunkeAssets").exists():
        shutil.copytree(ROOT / "Funke-Runtime/FunkeAssets", assets)
    binaries = {p.name: digest(p) for p in [exe, *exe.parent.glob("*.dll")]}
    sources = [Path(__file__), ROOT / "Tests/Funke-Engine-Tests/src/MaterialBatchingBenchmarkTests.cpp"]
    context = {"source": identity(), "host": host_info(), "binaries": binaries,
               "architecture": platform.machine(), "configuration": args.configuration,
               "warmups": args.warmups, "samples": args.samples, "repeats": args.repeats,
               "harnessSourceFingerprint": hashlib.sha256("".join(digest(p) for p in sources).encode()).hexdigest(),
               "materialResolution": "reference" if args.reference_material_resolution else "pass-cache"}
    write_json(output / "context.json", context)
    env = dict(os.environ, FUNKE_BATCH_BENCH="1", FUNKE_BATCH_OUTPUT=str(output), FUNKE_BATCH_WARMUPS=str(args.warmups),
               FUNKE_BATCH_SAMPLES=str(args.samples), FUNKE_BATCH_REPEATS=str(args.repeats))
    env["FUNKE_DISABLE_SPRITE_PASS_CACHE"] = "1" if args.reference_material_resolution else "0"
    command = [str(exe), "--test-case=GPU material batching benchmark*", "--no-skip", "--no-colors"]
    started = time.monotonic()
    with (output / "run.log").open("w", encoding="utf-8") as log:
        process = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0, timeout=900)
    write_json(output / "exit.json", {"command": command, "exitCode": process.returncode, "wallSeconds": time.monotonic() - started})
    log = (output / "run.log").read_text(encoding="utf-8", errors="replace")
    if process.returncode or not (output / "capture.json").exists() or re.search(r"WebGPU \[(Validation|OutOfMemory|Internal)\]|Device lost", log):
        raise RuntimeError(f"Benchmark failed; retained output: {output}")
    if any(digest(exe.parent / name) != value for name, value in binaries.items()):
        raise RuntimeError("A binary changed during capture; result is not comparable")
    metadata = export(output)
    print(f"{metadata['status']}: {output / 'measurement.md'}")


if __name__ == "__main__":
    main()
