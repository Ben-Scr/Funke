"""Compare sprite submission paths in one frozen build, preserving raw GPU captures."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--warmups", type=int, default=20)
    args = parser.parse_args()
    if args.pairs < 1 or not 1 <= args.samples <= 10000 or not 1 <= args.warmups <= 10000:
        parser.error("Use positive pairs and 1..10000 samples/warmups")
    exe = args.exe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries = {str(p): sha256(p) for p in [exe, *sorted(exe.parent.glob("*.dll"))]}
    context = {"status": "incomplete", "binaries": binaries, "harnessSha256": sha256(Path(__file__)),
               "lifecycle": args.lifecycle, "samples": args.samples,
               "warmups": args.warmups, "pairs": args.pairs,
               "scope": "Renderer2D.Submit CPU; excludes collect, sort, upload, presentation and readback. Not full-frame FPS.",
               "runs": [], "imagePairsEqual": 0}
    records: dict[tuple[str, str], list[float]] = {}
    manifest = output / "comparison.json"

    def save() -> None:
        manifest.write_text(json.dumps(context, indent=2) + "\n", encoding="utf-8")

    save()
    for pair in range(args.pairs):
        directories = {}
        order = ["reference", "optimized"] if pair % 2 == 0 else ["optimized", "reference"]
        for mode in order:
            directory = output / f"{mode}-{pair}"
            directory.mkdir()
            directories[mode] = directory
            env = dict(os.environ, FUNKE_BATCH_BENCH="1", FUNKE_BATCH_OUTPUT=str(directory),
                       FUNKE_BATCH_WARMUPS=str(args.warmups), FUNKE_BATCH_SAMPLES=str(args.samples),
                       FUNKE_BATCH_REPEATS="1", FUNKE_DISABLE_SPRITE_PASS_CACHE="0",
                       FUNKE_REFERENCE_SPRITE_SUBMIT="1" if mode == "reference" else "0",
                       FUNKE_SPRITE_BUNDLES="1" if args.lifecycle else "0")
            for name in ("FUNKE_SPRITE_MATRIX", "FUNKE_SPRITE_LIFECYCLE", "FUNKE_SKIN_MATRIX"):
                env.pop(name, None)
            command = [str(exe), "--test-case=GPU material batching benchmark*", "--no-skip", "--no-colors"]
            started = time.monotonic()
            with (directory / "run.log").open("w", encoding="utf-8") as log:
                result = subprocess.run(command, cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=45, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            text = (directory / "run.log").read_text(encoding="utf-8", errors="replace")
            run = {"mode": mode, "pair": pair, "exitCode": result.returncode,
                   "seconds": time.monotonic() - started, "directory": directory.name}
            context["runs"].append(run)
            save()
            print(json.dumps(run), flush=True)
            if result.returncode or any(term in text for term in ("WebGPU Validation", "Device lost", "[Validation]")):
                raise RuntimeError(f"Capture failed: {directory / 'run.log'}")
            if any(sha256(Path(path)) != digest for path, digest in binaries.items()):
                raise RuntimeError("Benchmark binaries changed during capture")
            sample_files = sorted(directory.glob("*.samples.csv"))
            if not args.lifecycle and len(sample_files) != 16:
                raise RuntimeError("Incomplete material benchmark matrix")
            run["rawSamples"] = {path.name: sha256(path) for path in sample_files}
            for path in sample_files:
                with path.open(newline="", encoding="utf-8") as file:
                    rows = list(csv.DictReader(file))
                if len(rows) != args.samples:
                    raise RuntimeError("Incomplete sample capture")
                name = path.name.removesuffix("-r0.samples.csv")
                records.setdefault((mode, name), []).append(statistics.median(float(row["cpu_submit_ms"]) for row in rows))
        reference = {p.name: sha256(p) for p in directories["reference"].glob("*.ppm")}
        optimized = {p.name: sha256(p) for p in directories["optimized"].glob("*.ppm")}
        if not reference or reference != optimized:
            raise RuntimeError(f"Reference/optimized images differ in pair {pair}")
        context["imagePairsEqual"] += len(reference)
        save()
    context["cpuSubmit"] = []
    for name in sorted({key[1] for key in records}):
        before = statistics.median(records["reference", name])
        after = statistics.median(records["optimized", name])
        context["cpuSubmit"].append({"case": name, "referenceMs": before, "optimizedMs": after,
                                     "reductionPercent": 100 * (1 - after / before),
                                     "referenceRepeatMedians": records["reference", name],
                                     "optimizedRepeatMedians": records["optimized", name]})
    context["status"] = "passed"
    save()
    print(json.dumps({"imagePairsEqual": context["imagePairsEqual"], "cpuSubmit": context["cpuSubmit"]}), flush=True)


if __name__ == "__main__":
    main()
