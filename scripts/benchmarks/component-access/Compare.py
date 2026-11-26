"""Alternate frozen before/after binaries and retain raw samples and hashes."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--suite", choices=["access", "structural", "queue"], required=True)
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--after", type=Path, help="Override the frozen after directory")
    parser.add_argument("--output", type=Path, help="Use a fresh result directory")
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error("pairs must be positive")
    root = args.root.resolve()
    out = args.output.resolve() if args.output else root / (args.suite + "-comparison")
    out.mkdir(exist_ok=False)
    directories = {mode: root / (f"access-{mode}" if args.suite == "access" else f"{mode}-native") for mode in ["before", "after"]}
    if args.after:
        directories["after"] = args.after.resolve()
    binaries = {str(file): digest(file) for directory in directories.values() for file in directory.iterdir() if file.suffix in [".dll", ".exe"]}
    report = {"status": "incomplete", "suite": args.suite, "binaries": binaries, "harnessSha256": digest(Path(__file__)), "runs": [], "results": []}
    manifest = out / "comparison.json"
    def save():
        manifest.write_text(json.dumps(report, indent=2), encoding="utf-8")
    save()
    grouped = {}
    for pair in range(args.pairs):
        for mode in (["before", "after"] if pair % 2 == 0 else ["after", "before"]):
            for count in ([1000, 10000, 100000] if args.suite == "access" else [0]):
                name = f"{mode}-{pair}-{count}"
                raw = out / (name + (".json" if args.suite == "access" else ".csv"))
                env = dict(os.environ, DOTNET_TieredCompilation="0", DOTNET_ReadyToRun="0")
                if args.suite == "access":
                    command = ["dotnet", str(directories[mode] / "Funke-ScriptCore-Tests.dll"), str(raw), str(count), "31"]
                else:
                    variable, case = ("FUNKE_ECB_STRUCTURAL_BENCH_OUTPUT", "ECB structural playback benchmark*") if args.suite == "structural" else ("FUNKE_ASSET_QUEUE_BENCH_OUTPUT", "Async asset pending queue benchmark*")
                    env[variable] = str(raw)
                    command = [str(directories[mode] / "Funke-Engine-Tests.exe"), "--test-case=" + case, "--no-colors"]
                start = time.monotonic()
                with (out / (name + ".log")).open("w", encoding="utf-8") as log:
                    result = subprocess.run(command, cwd=root.parents[1], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                report["runs"].append({"mode": mode, "pair": pair, "count": count, "exitCode": result.returncode, "seconds": time.monotonic() - start, "raw": raw.name})
                save()
                if result.returncode != 0:
                    raise RuntimeError(f"Failed: {name}")
                report["runs"][-1]["sha256"] = digest(raw)
                rows = json.loads(raw.read_text())["results"] if args.suite == "access" else list(csv.DictReader(raw.open()))
                expected = {"access": 124, "structural": 288, "queue": 93}[args.suite]
                if len(rows) != expected:
                    raise RuntimeError(f"Incomplete samples in {name}: {len(rows)} != {expected}")
                process = {}
                for row in rows:
                    if args.suite == "access":
                        key, value = (row["mode"], int(row["count"])), float(row["ms"])
                    elif args.suite == "structural":
                        key, value = (row["operation"], int(row["population"]), int(row["affected"]), int(row["mode"])), float(row["playback_ms"])
                    else:
                        key, value = (int(row["requests"]),), float(row["pump_ms"])
                    process.setdefault(key, []).append(value)
                for key, values in process.items():
                    grouped.setdefault(key, {}).setdefault(mode, []).append(statistics.median(values))
                print(name, f"{time.monotonic()-start:.2f}s", flush=True)
    for key, variants in grouped.items():
        before, after = (statistics.median(variants[mode]) for mode in ["before", "after"])
        report["results"].append({"case": key, "before_ms": before, "after_ms": after, "reduction_percent": (1 - after / before) * 100, "process_medians": variants})
    if any(digest(Path(path)) != value for path, value in binaries.items()):
        raise RuntimeError("A frozen binary changed during measurement")
    report["status"] = "passed"
    save()


if __name__ == "__main__":
    main()
