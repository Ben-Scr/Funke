"""Compare compatible FunkeBench v1 captures without treating missing evidence as a pass."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def number(value):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError(f"Invalid measurement: {value!r}")
    return value


def checked_file(root, item):
    path = (root / item["file"]).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("Evidence path leaves capture directory")
    if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise ValueError(f"Evidence hash mismatch: {path}")
    return path


def load_capture(path, scope, min_samples, min_repeats):
    path = Path(path)
    data = read_json(path)
    if data.get("schemaVersion") != 1 or data.get("status") != "complete":
        raise ValueError("A complete v1 capture is required")
    build = data["build"]
    if not build.get("engineCommit") or build.get("dirty") and not build.get("sourceFingerprint"):
        raise ValueError("Missing build identity")
    for key in ("compiler", "architecture", "nativeConfiguration", "moduleProfile"):
        if not build.get(key):
            raise ValueError(f"Missing build.{key}")
    for key in ("os", "osVersion", "cpu", "logicalProcessors"):
        if not data["host"].get(key):
            raise ValueError(f"Missing host.{key}")
    csv_path = checked_file(path.parent, data["csv"])
    with csv_path.open(newline="", encoding="utf-8-sig") as file:
        csv_rows = list(csv.DictReader(file))
    runs = data["runs"]
    if not runs or len(csv_rows) != len(runs) or sorted(r["csvRow"] for r in runs) != list(range(len(runs))):
        raise ValueError("CSV row mapping is incomplete or duplicated")
    ids = set()
    groups = {}
    columns = {"cpuWork": {"cpuWork": "cpu_ms", "cpu_submit_ms": "cpu_submit_ms", "cpu_render_ms": "cpu_render_ms"}, "gpu": {"gpu_ms": "gpu_ms"}}
    for run in runs:
        if run["runId"] in ids:
            raise ValueError("Duplicate run identity")
        ids.add(run["runId"])
        timing = run["timings"].get(scope)
        if timing is None or timing.get("unit") != "ms" or not timing.get("boundaries"):
            raise ValueError(f"Missing or invalid {scope} measurement")
        candidates = [r for r in run["rawSamples"] if r["timingScope"] in columns[scope]]
        if len(candidates) != 1 or candidates[0]["unit"] != "ms":
            raise ValueError("Missing or ambiguous raw timing scope")
        source = candidates[0]
        with checked_file(path.parent, source).open(newline="", encoding="utf-8-sig") as file:
            raw = list(csv.DictReader(file))
        values = sorted(number(float(r[columns[scope][source["timingScope"]]])) for r in raw)
        if len(values) < min_samples or timing["count"] != len(values):
            raise ValueError("Insufficient or inconsistent sample count")
        if [int(r["sample"]) for r in raw] != list(range(len(values))):
            raise ValueError("Missing or duplicate sample indices")
        expected = {"median": statistics.median(values), "mean": statistics.mean(values),
            "min": min(values), "max": max(values),
            "p95": values[math.ceil(.95 * len(values)) - 1], "p99": values[math.ceil(.99 * len(values)) - 1]}
        if any(not math.isclose(number(timing[k]), v, rel_tol=1e-8, abs_tol=1e-9) for k, v in expected.items()):
            raise ValueError("Reported distribution does not match hashed raw samples")
        workload = run["workload"]
        if not workload.get("sceneSha256") or not workload.get("projectSourceFingerprint"):
            raise ValueError("Missing workload identity")
        key = json.dumps({"workload": workload, "presentation": run["presentation"],
            "boundary": timing["boundaries"], "warmupIterations": run["warmup"]["iterations"]}, sort_keys=True)
        group = groups.setdefault(key, [])
        if any(previous["repeat"] == run["repeat"] for previous in group):
            raise ValueError("Duplicate repetition")
        group.append(run)
    if any(len(group) < min_repeats for group in groups.values()):
        raise ValueError("Insufficient independent repetitions")
    identity = {"host": data["host"], "build": {k: build.get(k) for k in
        ("compiler", "architecture", "nativeConfiguration", "managedConfiguration", "moduleProfile")}, "packages": data["packages"]}
    return identity, groups


def compare(before_path, after_path, scope, relative, absolute_ms, min_samples=30, min_repeats=3):
    number(relative)
    number(absolute_ms)
    if min_samples < 2 or min_repeats < 3:
        raise ValueError("Require at least two samples and three repetitions")
    before_identity, before = load_capture(before_path, scope, min_samples, min_repeats)
    after_identity, after = load_capture(after_path, scope, min_samples, min_repeats)
    if before_identity != after_identity:
        raise ValueError("Incompatible host, toolchain, configuration or package identity")
    if before.keys() != after.keys():
        raise ValueError("Incompatible or incomplete workload matrix")
    rows = []
    for key, old in before.items():
        new = after[key]
        a = [r["timings"][scope]["median"] for r in old]
        b = [r["timings"][scope]["median"] for r in new]
        threshold = max(absolute_ms, max(a) * relative)
        regression = min(b) - max(a) > threshold
        rows.append({"workload": old[0]["workload"], "scope": scope,
            "beforeMedianMs": statistics.median(a), "afterMedianMs": statistics.median(b),
            "differenceMs": statistics.median(b) - statistics.median(a),
            "relativeChange": statistics.median(b) / statistics.median(a) - 1 if statistics.median(a) else None,
            "beforeRepeatRangeMs": [min(a), max(a)], "afterRepeatRangeMs": [min(b), max(b)],
            "thresholdMs": threshold, "regression": regression,
            "beforeP95Ms": [r["timings"][scope]["p95"] for r in old],
            "afterP95Ms": [r["timings"][scope]["p95"] for r in new]})
    return {"status": "regression" if any(r["regression"] for r in rows) else "passed",
        "criterion": "Every candidate repetition median exceeds every baseline repetition median by both tolerances; tail distributions reported separately.",
        "relativeTolerance": relative, "absoluteToleranceMs": absolute_ms, "rows": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--scope", choices=("cpuWork", "gpu"), default="cpuWork")
    parser.add_argument("--relative", type=float, required=True)
    parser.add_argument("--absolute-ms", type=float, required=True)
    parser.add_argument("--min-samples", type=int, default=30)
    parser.add_argument("--min-repeats", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compare(args.before, args.after, args.scope, args.relative, args.absolute_ms, args.min_samples, args.min_repeats)
    except FileNotFoundError as error:
        result = {"status": "not_run", "reason": str(error)}
    except (ValueError, KeyError, TypeError, OSError) as error:
        result = {"status": "invalid", "reason": str(error)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as file:
        json.dump(result, file, indent=2, allow_nan=False)
        file.write("\n")
    print(result["status"])
    return {"passed": 0, "regression": 1, "invalid": 2, "not_run": 3}[result["status"]]


if __name__ == "__main__":
    sys.exit(main())
