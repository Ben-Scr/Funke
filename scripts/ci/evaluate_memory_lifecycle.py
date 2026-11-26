"""Apply an explicit memory-growth budget to a completed, unchanged-binary lifecycle run.

Passing is a regression budget, not proof that the engine has no memory leaks.
The input report is never changed; forced-GC diagnostic runs are not accepted.
"""
import argparse
import json
import math
from pathlib import Path
from statistics import median

MIB = 1024 * 1024


def evaluate(report, *, minimum_cycles=120, warmup_cycles=20,
             max_growth_mib=128.0, max_slope_mib=1.0):
    errors = []
    result = {"status": "failed", "errors": errors, "budget": {
        "minimumCycles": minimum_cycles, "warmupCycles": warmup_cycles,
        "maxGrowthMiB": max_growth_mib, "maxSlopeMiBPerCycle": max_slope_mib}}
    if minimum_cycles < 20 or warmup_cycles < 0 or minimum_cycles - warmup_cycles < 20:
        raise ValueError("At least 20 measured cycles after warmup are required")
    if not all(math.isfinite(v) and v >= 0 for v in (max_growth_mib, max_slope_mib)):
        raise ValueError("Memory budgets must be finite and nonnegative")
    if report.get("status") != "passed" or report.get("exitCode") != 0:
        errors.append("Lifecycle run did not complete successfully with exit code 0")
    if report.get("mode") not in ("mixed", "play", "reload", "scene"):
        errors.append("A steady run does not verify repeated lifecycle transitions")
    if report.get("collectEvery") != 0:
        errors.append("Acceptance requires a run without explicit garbage collections")
    before, after = report.get("binariesBefore"), report.get("binariesAfter")
    if not before or before != after:
        errors.append("Missing binary hashes or binaries changed during measurement")
    cycles = report.get("requestedCycles")
    if not isinstance(cycles, int) or cycles < minimum_cycles:
        errors.append(f"At least {minimum_cycles} complete cycles are required")
        return result
    samples = report.get("samples", [])
    completed = [s for s in samples if s.get("label") == "after-cycle"]
    if [s.get("cycle") for s in completed] != list(range(1, cycles + 1)):
        errors.append("Missing, duplicate or out-of-order completed cycles")
        return result
    if report.get("mode") in ("mixed", "play"):
        for phase in ("playing", "stopped"):
            if [s.get("cycle") for s in samples if s.get("label") == phase] != list(range(1, cycles + 1)):
                errors.append(f"Missing or invalid {phase} samples")
    if report.get("mode") in ("mixed", "reload"):
        if any(s.get("revision") != s["cycle"] + 1 for s in completed):
            errors.append("Reload samples do not contain the expected code revision")
    measured = completed[warmup_cycles:]
    result["measuredCycles"] = len(measured)
    for key in ("privateBytes", "managedLiveEstimateBytes"):
        values = [s.get(key) for s in measured]
        if any(not isinstance(v, (float, int)) or not math.isfinite(v) or v < 0 for v in values):
            errors.append(f"Missing or invalid {key} measurements")
            continue
        values = [v / MIB for v in values]
        count = len(values)
        midpoint = (count - 1) / 2
        mean = sum(values) / count
        slope = sum((i - midpoint) * (v - mean) for i, v in enumerate(values)) / sum(
            (i - midpoint) ** 2 for i in range(count))
        growth = median(values[-10:]) - median(values[:10])
        result[key] = {"firstWindowMedianMiB": median(values[:10]),
                       "lastWindowMedianMiB": median(values[-10:]),
                       "growthMiB": growth, "slopeMiBPerCycle": slope,
                       "peakMiB": max(values)}
        if growth > max_growth_mib or slope > max_slope_mib:
            errors.append(f"{key} exceeds the growth or slope budget")
    if not errors:
        result["status"] = "passed"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--minimum-cycles", type=int, default=120)
    parser.add_argument("--warmup-cycles", type=int, default=20)
    parser.add_argument("--max-growth-mib", type=float, default=128)
    parser.add_argument("--max-slope-mib", type=float, default=1)
    args = parser.parse_args()
    if args.output and args.output.resolve() == args.report.resolve():
        parser.error("Output must not overwrite the measurement report")
    try:
        result = evaluate(json.loads(args.report.read_text(encoding="utf-8-sig")),
                          minimum_cycles=args.minimum_cycles, warmup_cycles=args.warmup_cycles,
                          max_growth_mib=args.max_growth_mib, max_slope_mib=args.max_slope_mib)
    except (OSError, ValueError, TypeError) as error:
        parser.error(str(error))
    result["sourceReport"] = str(args.report.resolve())
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
