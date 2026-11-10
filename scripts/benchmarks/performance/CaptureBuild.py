"""Wrap a real incremental build; freeze output and reject changing source/binaries."""
from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import os
import shutil
import signal
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("material_benchmark", ROOT / "scripts/benchmarks/material-batching/Run.py")
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", required=True)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.timeout <= 0:
        parser.error("A build command and positive timeout are required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    names = [file.name for file in args.binary]
    if len(set(names)) != len(names):
        parser.error("Binary names must be unique")
    context = {"schemaVersion": 1, "status": "building", "command": command,
               "startedAt": dt.datetime.now(dt.timezone.utc).isoformat(), "sourceBefore": common.identity()}
    common.write_json(output / "build.json", context)
    try:
        with (output / "build.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=os.name != "nt")
            try:
                process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   stdout=log, stderr=subprocess.STDOUT, timeout=10)
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
                raise
        context["exitCode"] = process.returncode
        context["sourceAfter"] = common.identity()
        if process.returncode:
            raise RuntimeError(f"Build failed with exit code {process.returncode}")
        if context["sourceBefore"] != context["sourceAfter"]:
            raise RuntimeError("Sources changed during the build; provenance is incomplete")
        before = {file.name: common.digest(file) for file in args.binary}
        frozen = output / "binaries"
        frozen.mkdir()
        for file in args.binary:
            shutil.copy2(file, frozen / file.name)
        if before != {file.name: common.digest(file) for file in args.binary} or before != {
                file.name: common.digest(frozen / file.name) for file in args.binary}:
            raise RuntimeError("Build output changed while freezing binaries")
        context.update(status="complete", source=context["sourceBefore"], binaries=before,
                       harnessSha256=common.digest(ROOT / "Tests/Funke-Engine-Tests/src/PerformanceBenchmarkTests.cpp"))
    except Exception as error:
        context.update(status="incomplete", error=str(error))
    finally:
        context["finishedAt"] = dt.datetime.now(dt.timezone.utc).isoformat()
        common.write_json(output / "build.json", context)
    print(context["status"], context.get("error", ""))
    return 0 if context["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
