from __future__ import annotations

import argparse
import json
from pathlib import Path


def summarize_lcov(text: str, commit: str) -> dict:
    files = []
    totals = {"linesFound": 0, "linesHit": 0, "functionsFound": 0, "functionsHit": 0}
    fields = {"LF": "linesFound", "LH": "linesHit", "FNF": "functionsFound", "FNH": "functionsHit"}
    for block in text.split("end_of_record"):
        record = {}
        for line in block.splitlines():
            key, _, value = line.partition(":")
            if key == "SF":
                record["path"] = value.replace("\\", "/")
            elif key in fields:
                record[fields[key]] = int(value)
        if "path" not in record:
            continue
        path = record["path"]
        if not any(part.startswith("Funke-") for part in path.split("/")) or any(
                part in {"External", "vendor", "Tests"} for part in path.split("/")):
            raise ValueError(f"Unexpected source in filtered coverage: {path}")
        for found, hit in [("linesFound", "linesHit"), ("functionsFound", "functionsHit")]:
            if found not in record or hit not in record or not 0 <= record[hit] <= record[found]:
                raise ValueError(f"Invalid or missing coverage counters: {path}")
        for key in totals:
            totals[key] += record[key]
        files.append(record)
    if not files or not totals["linesHit"] or not totals["functionsHit"]:
        raise ValueError("Coverage must contain executed first-party lines and functions")
    totals["linePercent"] = 100 * totals["linesHit"] / totals["linesFound"]
    totals["functionPercent"] = 100 * totals["functionsHit"] / totals["functionsFound"]
    return {
        "schemaVersion": 1, "commit": commit, "configuration": "Debug-linux-x86_64-coverage",
        "suite": "Funke-Engine-Tests", "totals": totals, "files": files,
        "excluded": ["External", "vendor", "Tests", "/usr"],
        "scope": "Only files present in the instrumented native Engine test run; not whole-product coverage.",
        "unmeasured": ["C# assemblies", "separate Editor/Launcher/CLI test runs", "real GPU and managed interop runtime paths"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    try:
        report = summarize_lcov(args.input.read_text(encoding="utf-8"), args.commit)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Coverage evidence failed: {error}\n")
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    totals = report["totals"]
    summary = (f"\n## Native coverage\n\nCommit: `{args.commit}`\n\n"
               f"Lines: {totals['linePercent']:.2f}% ({totals['linesHit']}/{totals['linesFound']}); "
               f"functions: {totals['functionPercent']:.2f}% "
               f"({totals['functionsHit']}/{totals['functionsFound']}).\n\n{report['scope']}\n")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as output:
            output.write(summary)
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
