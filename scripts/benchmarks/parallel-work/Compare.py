"""Compare three alternating pairs; require identical particle states and render images."""
import argparse
import csv
import json
from pathlib import Path
import statistics


def load(path):
    context = json.loads((path / "context.json").read_text())
    if context["status"] != "complete":
        raise ValueError(f"Incomplete run: {path}")
    rows = list(csv.DictReader((path / "parallel.samples.csv").open()))
    return context, rows


def compare(before, after, output):
    if len(before) != len(after) or len(before) < 3:
        raise ValueError("At least three matched pairs are required")
    pairs = [(load(a), load(b)) for a, b in zip(before, after)]
    for (a, _), (b, _) in pairs:
        for key in ["modelSha256", "samples", "workers", "images", "particleState"]:
            if a[key] != b[key]:
                raise ValueError(f"Mismatched {key}")
    result = {"before": list(map(str, before)), "after": list(map(str, after)),
              "imagesEqual": True, "particleStatesEqual": True, "cases": {}}
    for case in pairs[0][0][0]["mediansMs"]:
        a = [p[0][0]["mediansMs"][case] for p in pairs]
        b = [p[1][0]["mediansMs"][case] for p in pairs]
        before_ms, after_ms = statistics.median(a), statistics.median(b)
        stats = {"beforeMs": before_ms, "afterMs": after_ms,
                 "reductionPercent": (1 - after_ms / before_ms) * 100,
                 "beforeRunMedians": a, "afterRunMedians": b,
                 "fasterPairs": sum(y < x for x, y in zip(a, b))}
        for label, index in [("before", 0), ("after", 1)]:
            samples = sorted(float(row["cpu_ms"]) for pair in pairs for row in pair[index][1]
                             if row["case"] + ":" + row["count"] == case)
            stats[label + "P95Ms"] = samples[int((len(samples) - 1) * .95)]
        result["cases"][case] = stats
    output.write_text(json.dumps(result, indent=2))
    print("case | before ms | after ms | reduction | faster pairs")
    for case, values in result["cases"].items():
        print(f"{case} | {values['beforeMs']:.4f} | {values['afterMs']:.4f} | "
              f"{values['reductionPercent']:.1f}% | {values['fasterPairs']}/{len(pairs)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, nargs="+", required=True)
    parser.add_argument("--after", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compare(args.before, args.after, args.output)
