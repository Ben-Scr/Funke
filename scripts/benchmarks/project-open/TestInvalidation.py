#!/usr/bin/env python3
"""Exercise package-open cache invalidation through a benchmark fixture's real launcher."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys

SCRIPT = Path(__file__).resolve()
SPEC = importlib.util.spec_from_file_location("project_open_benchmark", SCRIPT.with_name("Run.py"))
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--launcher", type=Path,
                        default=BENCH.REPO / "bin/Release-windows-x86_64/Funke-Launcher/Funke-Launcher.exe")
    args = parser.parse_args()
    project, output, launcher = args.project.resolve(), args.output.resolve(), args.launcher.resolve()
    fixtures = BENCH.read_json(project.parent / "fixtures.json") or []
    if not any(Path(item["project"]).resolve() == project and item["name"] == "OpenPackages" for item in fixtures):
        parser.error("Use the OpenPackages fixture created by CreateFixtures.py, never a user's project")
    if output.exists():
        parser.error("Use a new output directory")
    output.mkdir(parents=True)
    package = project / "Packages/Bench.Startup"
    source = package / "src/PInvoke.cpp"
    manifest = package / "funke-package.lua"
    workspace = project / ".funke/build/packages"
    stamp = workspace / "open-cache-Release-x64.json"
    native = workspace / "bin/Release-windows-x86_64/Pkg.Bench.Startup.Native/Pkg.Bench.Startup.Native.dll"
    abi = Path(str(native) + ".funkeabi")
    originals = {path: path.read_bytes() for path in (source, manifest)}
    cases = []

    def check(name, success=True, hit=False, stamp_exists=True):
        result = BENCH.run_once(launcher, project, output / name, 180, expected_packages=1)
        log = output / name / "user-data/Funke/Logs/Funke-Launcher.log"
        log_text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        cache_hit = "Package inputs and outputs unchanged; skipping generation and build." in log_text
        case = {"name": name, "success": result["success"], "cacheHit": cache_hit,
                "stampExists": stamp.exists(), "error": result.get("error")}
        case["passed"] = result["success"] == success and cache_hit == hit and stamp.exists() == stamp_exists
        cases.append(case)
        BENCH.write_json(output / "cases.json", cases)
        print(json.dumps(case), flush=True)
        if not case["passed"]:
            raise RuntimeError(f"Invalidation case failed: {name}")

    try:
        check("unchanged", hit=True)
        source.write_bytes(originals[source] + b"\n// Startup cache invalidation fixture.\n")
        check("native-source-edit")
        check("source-edit-warm", hit=True)
        manifest.write_bytes(originals[manifest].replace(b'"0.1.0"', b'"0.1.1"', 1))
        check("manifest-edit")
        check("manifest-edit-warm", hit=True)
        native.unlink()
        check("missing-native-output")
        source.write_bytes(source.read_bytes() + b"\n#error ExpectedStartupCacheBuildFailure\n")
        check("failed-build", success=False, stamp_exists=False)
        source.write_bytes(originals[source] + b"\n// Startup cache invalidation fixture.\n")
        check("recover-after-failure")
        abi.write_text("schema=1\nboundary=engine_cpp\nengine_sha256=" + "0" * 64 + "\n", encoding="ascii")
        check("invalid-native-abi")
        check("abi-repaired-warm", hit=True)
    finally:
        for path, contents in originals.items():
            path.write_bytes(contents)
        result = BENCH.run_once(launcher, project, output / "restored-original-inputs", 180, expected_packages=1)
        BENCH.write_json(output / "restoration.json", result)
        if not result["success"]:
            raise RuntimeError("Fixture inputs restored, but restoration build failed; see restoration.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
