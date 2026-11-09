"""Run the opt-in CPU probes and export the existing FunkeBench v1 contract."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("material_benchmark", ROOT / "scripts/benchmarks/material-batching/Run.py")
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def export(output: Path, context: dict) -> dict:
    if not context.get("binaries") or context.get("binariesAfter") != context["binaries"]:
        raise ValueError("Binary identity was not verified after capture")
    xml = ET.parse(output / "tests.xml").getroot()
    cases = [case for case in xml.findall(".//TestCase") if case.attrib.get("skipped") != "true"]
    assertions = xml.find("OverallResultsAsserts")
    if (context.get("exitCode") != 0 or len(cases) != 1
            or cases[0].attrib.get("name") != "CPU performance benchmark validates measured work"
            or assertions is None or assertions.get("failures") != "0"
            or int(assertions.get("successes", "0")) == 0 or list(xml.iter("Exception"))):
        raise ValueError("CPU probe did not execute successfully")
    with (output / "samples.csv").open(newline="", encoding="utf-8") as file:
        raw = list(csv.DictReader(file))
    groups = {}
    for row in raw:
        groups.setdefault((row["case"], int(row["repeat"])), []).append(row)
    names = {f"transform-flat-{count}-dirty-{percent}" for count in (100, 1000, 10000) for percent in (0, 1, 100)}
    names |= {f"layout-roots-{count}-{mode}" for count in (100, 1000, 5000) for mode in ("idle", "one-change")}
    names |= {f"layout-{shape}-{count}-{mode}" for shape in ("grid", "tree") for count in (100, 1000, 5000) for mode in ("idle", "one-change")}
    names |= {f"jobs-{workers}-items-{count}-mode-{mode}" for workers in (0, 2, 8) for count in (256, 4096, 65536) for mode in (0, 1, 2)}
    names |= {f"profiler-push-mode-{mode}" for mode in (0, 1, 2)}
    names |= {f"hierarchy-{dimension}d-shape-{shape}-{count}-dirty-{percent}"
              for dimension in (2, 3) for shape in (0, 1, 2) for count in (1000, 10000) for percent in (0, 1, 100)}
    names |= {f"physics-{dimension}d-{mode}-{count}-changed-{percent}"
              for dimension, counts in ((2, (100, 2048)), (3, (100, 1000))) for count in counts
              for mode in ("sync", "step", "queries") for percent in (0, 1, 100)
              if mode != "queries" or percent == 0}
    if context.get("assetRoot"):
        if context.get("assetInputsAfter") != context["assetInputs"]:
            raise ValueError("Asset inputs changed during capture")
        names |= {f"asset-io-mode-{mode}-files-{count}" for mode in range(5) for count in (1, 256)}
    names = {name for name in names if name.startswith(context.get("casePrefix", ""))}
    if not names:
        raise ValueError("Case prefix matched no workload")
    if groups.keys() != {(name, repeat) for name in names for repeat in range(context["repeats"])}:
        raise ValueError("Incomplete CPU workload matrix")
    engine = (output / "complete.txt").read_text(encoding="utf-8")
    if "Release" not in engine:
        raise ValueError("CPU benchmark requires the intended Release build")
    host = context["host"] | {"gpu": None, "gpuDriver": None, "graphicsBackend": None}
    host.pop("gpuCandidates", None)
    unavailable = {f"host.{key}": "CPU-only probe; no graphics device" for key in ("gpu", "gpuDriver", "graphicsBackend")}
    compiler_path = output / "compiler.txt"
    compiler = compiler_path.read_text(encoding="utf-8") if compiler_path.is_file() else None
    if compiler is None:
        unavailable["build.compiler"] = "Historical capture without compiler metadata"
    metadata = {
        "schemaVersion": 1, "status": "complete" if compiler and not context.get("conditions") else "incomplete",
        "build": context["source"] | {"engineVersion": engine, "nativeConfiguration": "Release",
            "managedConfiguration": None, "moduleProfile": "full", "compiler": compiler,
            "architecture": "x86_64", "binaries": context["binaries"],
            "layoutVisits": context.get("layoutVisits", "historical-unrecorded"),
            "fileStampReads": context.get("fileStampReads", "historical-unrecorded")},
        "host": host, "packages": [], "runs": [], "unavailable": unavailable,
    }
    unavailable["build.managedConfiguration"] = "No managed code in this probe"
    if context.get("conditions"):
        metadata["captureConditions"] = context["conditions"]
        unavailable["controlledComparison"] = "Descriptive capture: " + context["conditions"]
    rows = []
    for index, ((name, repeat), values) in enumerate(groups.items()):
        if len(values) != context["samples"] or [int(r["sample"]) for r in values] != list(range(context["samples"])):
            raise ValueError(f"Invalid samples for {name}/{repeat}")
        first = values[0]
        if any((r["entities"], r["workers"], r["operations"]) != (first["entities"], first["workers"], first["operations"]) for r in values):
            raise ValueError("Workload changed within a repetition")
        boundary = "One native probe invocation; includes authored mutations and the named system call; excludes fixture setup, validation, warmup and export. Jobs modes: 0 serial, 1 one chunk, 2 auto grain. Profiler modes: 0 disarmed, 1 unlimited, 2 60Hz."
        if name.startswith("physics-"):
            boundary = ("One native physics invocation. Sync: authored teleports plus editor sync (3D also hierarchy propagation). "
                        "Step: complete fixed step at 1/60 s including sync, solver, writeback and empty contact dispatch; "
                        "3D also hierarchy propagation. Queries: one ray per body, hit result aggregation included. "
                        "No contacts, scripts or rendered frames. Zero gravity, separated unit boxes, undamped unit Y velocity "
                        "for changed bodies in step cases. Setup/sleep settling, validation, warmup and export excluded. "
                        "2D setup settles 120 steps; 3D explicitly sleeps bodies. Workers denotes configured available pool, "
                        "not active tasks (Box2D dynamically selects 1 or up to 4).")
        elif name.startswith("asset-io-"):
            boundary = ("One loop over existing absolute texture paths: mode 0 File.Exists, 1 source FileStamp.Read, "
                        "2 metadata FileStamp.Read, 3 warmed ReadTextureMeta, 4 Path.CanonicalIdentity. "
                        "Includes path conversion, filesystem calls and result counting. No loading, decoding, GPU work or script dispatch. "
                        "OS cache uncontrolled; fixtures and metadata exist before setup; warmup and validation excluded.")
        timing = common.distribution([float(r["cpu_ms"]) for r in values], boundary)
        filename = f"{name}-r{repeat}.samples.csv"
        with (output / filename).open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, list(first))
            writer.writeheader()
            writer.writerows(values)
        configuration = {"case": name, "entities": int(first["entities"]), "workers": int(first["workers"]),
            "operations": int(first["operations"]), "samples": context["samples"], "warmups": context["warmups"],
            "measurement": "isolated CPU call, not frame timing", "profiler": "enabled; panel disarmed outside profiler cases",
            "fixtureVersion": 4}
        if name.startswith("asset-io-"):
            configuration["assetInputs"] = context["assetInputs"]
        workload_hash = hashlib.sha256(json.dumps(configuration, sort_keys=True).encode()).hexdigest()
        entry = {"csvRow": index, "runId": f"{name}-r{repeat}", "repeat": repeat,
            "startedAt": dt.datetime.fromtimestamp(float(first["started_epoch"]), dt.timezone.utc).isoformat() if "started_epoch" in first else None,
            "wallDurationSeconds": float(first["wall_seconds"]) if "wall_seconds" in first else None,
            "workload": {"projectSourceFingerprint": context["harnessSha256"], "scene": name, "sceneSha256": workload_hash,
                "configuration": configuration, "seed": 0, "entities": int(first["entities"]), "materials": 0,
                "lights": 0, "bones": 0, "visibleEntities": None},
            "presentation": {"widthPixels": 1920 if name.startswith("layout") else None,
                "heightPixels": 1080 if name.startswith("layout") else None, "vsync": False, "targetFps": 0,
                "renderScale": None, "windowMode": "headless; synthetic UI region for layout"},
            "warmup": {"iterations": context["warmups"], "seconds": float(first["warmup_seconds"]) if "warmup_seconds" in first else None},
            "timings": {"cpuWork": timing, "gpu": None, "frameInterval": None, "setup": None,
                "commandRecording": None, "commandPlayback": None},
            "memory": {"native": None, "managed": None, "gpu": None},
            "rawSamples": [{"file": filename, "sha256": common.digest(output / filename), "timingScope": "cpuWork", "unit": "ms"}]}
        def missing(value, prefix):
            if isinstance(value, dict):
                for key, item in value.items():
                    missing(item, f"{prefix}.{key}")
            elif value is None:
                unavailable[prefix] = "Not collected or not applicable to the isolated CPU probe; whole-process times are in context.json"
        missing(entry, f"runs[{index}]")
        metadata["runs"].append(entry)
        row = dict.fromkeys(common.HEADER, "")
        row.update(timestamp=entry["startedAt"] or "", benchmark=name, entities=first["entities"], workers=first["workers"], samples=timing["count"],
            mean_ms=timing["mean"], median_ms=timing["median"], p95_ms=timing["p95"], min_ms=timing["min"],
            max_ms=timing["max"], engine=engine, engine_configuration="Release", warmup_iterations=context["warmups"], warmup_seconds=entry["warmup"]["seconds"],
            timing_scope=boundary)
        rows.append(row)
    with (output / "results.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, common.HEADER)
        writer.writeheader()
        writer.writerows(rows)
    metadata["csv"] = {"file": "results.csv", "sha256": common.digest(output / "results.csv")}
    common.write_json(output / "results.metadata.json", metadata)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin/Release-windows-x86_64/Funke-Engine-Tests/Funke-Engine-Tests.exe")
    parser.add_argument("--samples", type=int, default=200)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=20)
    parser.add_argument("--export-only", action="store_true")
    parser.add_argument("--case-prefix", default="", help="Select case names with this prefix")
    parser.add_argument("--reference-layout-visits", action="store_true")
    parser.add_argument("--reference-file-stamp", action="store_true")
    parser.add_argument("--asset-root", type=Path, help="Opt-in directory with large.tga, small-0..255.tga and their metadata")
    parser.add_argument("--conditions", help="Record concurrent work or other uncontrolled conditions; marks descriptive capture incomplete")
    args = parser.parse_args()
    if min(args.samples, args.repeats, args.warmups) < 1:
        parser.error("sample settings must be positive")
    output, binary = args.output.resolve(), args.binary.resolve()
    if args.export_only:
        export(output, json.loads((output / "context.json").read_text(encoding="utf-8")))
        return
    output.mkdir(parents=True, exist_ok=False)
    context = {"source": common.identity(), "host": common.host_info(), "samples": args.samples,
        "warmups": args.warmups, "repeats": args.repeats,
        "casePrefix": args.case_prefix, "layoutVisits": "reference" if args.reference_layout_visits else "dense",
        "fileStampReads": "reference" if args.reference_file_stamp else "directory-entry",
        "harnessSha256": common.digest(ROOT / "Tests/Funke-Engine-Tests/src/PerformanceBenchmarkTests.cpp"),
        "binaries": {p.name: common.digest(p) for p in (binary, binary.parent / "Funke-Engine.dll")},
        "startedAt": dt.datetime.now(dt.timezone.utc).isoformat()}
    if args.conditions:
        context["conditions"] = args.conditions
    if args.asset_root:
        context["assetRoot"] = str(args.asset_root.resolve(strict=True))
        context["assetInputs"] = {p.name: common.digest(p) for p in args.asset_root.iterdir() if p.is_file()}
    common.write_json(output / "context.json", context)
    env = os.environ | {"FUNKE_CPU_BENCH_OUTPUT": str(output), "FUNKE_CPU_BENCH_SAMPLES": str(args.samples),
        "FUNKE_CPU_BENCH_REPEATS": str(args.repeats), "FUNKE_CPU_BENCH_WARMUPS": str(args.warmups),
        "FUNKE_CPU_BENCH_PREFIX": args.case_prefix, "FUNKE_REFERENCE_LAYOUT_VISITS": "1" if args.reference_layout_visits else "0"}
    env["FUNKE_REFERENCE_FILE_STAMP"] = "1" if args.reference_file_stamp else "0"
    env.pop("FUNKE_CPU_ASSET_ROOT", None)
    if args.asset_root:
        env["FUNKE_CPU_ASSET_ROOT"] = context["assetRoot"]
    start = time.monotonic()
    with (output / "process.log").open("w", encoding="utf-8") as log:
        result = subprocess.run([str(binary), "--no-skip=true", "--test-case=CPU performance benchmark validates measured work",
            "--reporters=xml", f"--out={output / 'tests.xml'}", "--no-breaks=true"], cwd=binary.parent,
            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    context["wallDurationSeconds"] = time.monotonic() - start
    context["exitCode"] = result.returncode
    context["binariesAfter"] = {name: common.digest(binary.parent / name) for name in context["binaries"]}
    if args.asset_root:
        context["assetInputsAfter"] = {p.name: common.digest(p) for p in args.asset_root.iterdir() if p.is_file()}
    common.write_json(output / "context.json", context)
    if result.returncode or not (output / "complete.txt").is_file():
        raise RuntimeError(f"CPU probe failed; inspect {output}")
    if context["binariesAfter"] != context["binaries"]:
        raise RuntimeError("A measured binary changed during capture")
    export(output, context)
    print(f"CPU measurements: {output / 'results.metadata.json'}")


if __name__ == "__main__":
    main()
