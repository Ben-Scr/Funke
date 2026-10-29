#!/usr/bin/env python3
"""Create, export and execute a C# game with a private .NET runtime."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import runtime_smoke_test as smoke


def checked(command: list[str], timeout: int = 1200) -> str:
    result = subprocess.run(command, cwd=smoke.REPO_ROOT, text=True, encoding="utf-8",
                            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout}")
    return result.stdout


def verify(cli: Path, root: Path) -> None:
    checked([str(cli), "create", "ExportSmoke", "--path", str(root),
             "--no-register", "--no-git", "--no-compile"])
    project = root / "ExportSmoke"
    project_file = project / "project.json"
    config = json.loads(project_file.read_text(encoding="utf-8-sig"))
    config["modules"] = ["Funke.2D", "Funke.3D"]
    config["buildScenes"] = ["SampleScene"]
    project_file.write_text(json.dumps(config, indent=2), encoding="utf-8")
    script = project / "Assets/Scripts/ExportProbe.cs"
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text('using Funke;\npublic class ExportProbe : EntityScript {\n'
                      ' public override void OnStart() => Log.Info("EXPORT_SMOKE_SCRIPT_STARTED");\n}\n',
                      encoding="utf-8")
    scene_file = project / "Assets/Scenes/SampleScene.scene"
    scene = json.loads(scene_file.read_text(encoding="utf-8-sig"))
    scene["entities"].append({"name": "Export Probe", "Scripts": [{"className": "ExportProbe", "enabled": True}]})
    scene_file.write_text(json.dumps(scene, indent=2), encoding="utf-8")
    output = root / "Export"
    checked([str(cli), "build", "--project", str(project), "--development", "--output", str(output), "--verbose"])
    relocated = root / "Relocated Game Prüfung"
    shutil.copytree(output, relocated)
    if sys.platform == "darwin":
        content = relocated / "ExportSmoke.app/Contents/Resources"
        binary = relocated / "ExportSmoke.app/Contents/MacOS/ExportSmoke"
    else:
        content = relocated
        binary = content / ("ExportSmoke.exe" if os.name == "nt" else "ExportSmoke")
    if not (content / "dotnet/host/fxr").is_dir():
        raise RuntimeError("Export did not bundle .NET.")
    environment = os.environ.copy()
    for key in ("DOTNET_ROOT", "DOTNET_ROOT_X64", "DOTNET_ROOT_ARM64", "DOTNET_ROOT(x86)"):
        environment[key] = str(root / "no-system-runtime")
    environment["DOTNET_MULTILEVEL_LOOKUP"] = "0"
    environment["CI"] = "true"
    markers = smoke.REQUIRED_MARKERS + ("Core assembly loaded:", "Using bundled .NET runtime:",
                                        "EXPORT_SMOKE_SCRIPT_STARTED")
    log_path, offset = smoke.log_snapshot(binary)
    status, log = smoke.run_runtime(binary, 60, markers, environment)
    log += smoke.read_log_tail(log_path, offset)
    if status or any(marker not in log for marker in markers) or any(marker in log for marker in smoke.FORBIDDEN_MARKERS):
        raise RuntimeError(f"Exported game failed (status {status}):\n{log}")
    # A damaged private install must never silently select a machine-wide runtime.
    framework = content / "dotnet/shared/Microsoft.NETCore.App"
    framework.rename(framework.with_name("unavailable"))
    log_path, offset = smoke.log_snapshot(binary)
    status, log = smoke.run_runtime(binary, 60, (), environment)
    log += smoke.read_log_tail(log_path, offset)
    if status == 124 or "EXPORT_SMOKE_SCRIPT_STARTED" in log or "Core assembly loaded:" in log:
        raise RuntimeError(f"Damaged private runtime fell back or did not terminate:\n{log}")
    if "Failed to initialize .NET runtime" not in log:
        raise RuntimeError(f"Missing private framework was not diagnosed:\n{log}")
    (content / "dotnet").rename(content / "unavailable-dotnet")
    log_path, offset = smoke.log_snapshot(binary)
    status, log = smoke.run_runtime(binary, 60, (), environment)
    log += smoke.read_log_tail(log_path, offset)
    if status == 124 or "Core assembly loaded:" in log or "Bundled .NET runtime is missing:" not in log:
        raise RuntimeError(f"Missing private runtime folder was not rejected:\n{log}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    args = parser.parse_args()
    cli = args.cli.resolve()
    if not cli.is_file():
        parser.error(f"CLI does not exist: {cli}")
    work = smoke.REPO_ROOT / "bin-int/_agent/export-smoke"
    work.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="run-", dir=work) as temporary:
            verify(cli, Path(temporary))
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        return 1
    print("Desktop export passed: relocated game ran C# using bundled .NET; damaged bundle refused fallback.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
