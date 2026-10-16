#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


STABLE_MARKER = "CI smoke test reached a stable frame; requesting graceful shutdown."
FORBIDDEN_MARKERS = (
    "Unhandled exception at app boundary",
    "CI smoke test failed",
    "CrashReporter wrote minidump",
)


def log_snapshot(binary: Path) -> tuple[Path | None, int]:
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
    else:
        base = os.environ.get("XDG_CONFIG_HOME")
        if not base:
            home = os.environ.get("HOME")
            base = str(Path(home) / ".config") if home else None
    if not base:
        return None, 0
    path = Path(base) / "Funke" / "Logs" / f"{binary.stem}.log"
    return path, path.stat().st_size if path.is_file() else 0


def read_log_tail(path: Path | None, offset: int) -> str:
    if not path or not path.is_file():
        return ""
    size = path.stat().st_size
    with path.open("rb") as stream:
        stream.seek(offset if size >= offset else 0)
        return stream.read().decode("utf-8", errors="replace")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Launch a Funke GUI application and require graceful exit.")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--argument", action="append", default=[])
    parser.add_argument(
        "--editor-project",
        action="store_true",
        help="Stage a schema-v1 project and require the Editor to load its SampleScene.",
    )
    return parser.parse_args()


def stage_editor_project(binary: Path) -> tempfile.TemporaryDirectory[str]:
    temporary = tempfile.TemporaryDirectory(prefix="Funke Editor Prüfung ")
    project = Path(temporary.name)
    scenes = project / "Assets" / "Scenes"
    scenes.mkdir(parents=True)

    metadata_path = binary.parent / "BuildMetadata.json"
    version = "0.0.0-dev"
    if metadata_path.is_file():
        version = str(json.loads(metadata_path.read_text(encoding="utf-8"))["version"])
    (project / "project.json").write_text(
        json.dumps(
            {
                "projectFormatVersion": 1,
                "name": "CIEditorSmoke",
                "engineVersion": version,
                "startupScene": "SampleScene",
                "lastOpenedScene": "SampleScene",
                "buildScenes": ["SampleScene"],
                "assetSerializationFormat": "Json",
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    (scenes / "SampleScene.scene").write_text(
        json.dumps({"version": 1, "name": "SampleScene", "entities": []}, indent=2),
        encoding="utf-8",
    )
    return temporary


def terminate(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        process.kill()
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            return
    process.wait(timeout=5.0)


def main() -> int:
    args = parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        print(f"Application binary was not found: {binary}", file=sys.stderr)
        return 1
    if not (binary.parent / "FunkeAssets").is_dir():
        print(f"FunkeAssets was not found next to: {binary}", file=sys.stderr)
        return 1

    editor_project: tempfile.TemporaryDirectory[str] | None = None
    arguments = list(args.argument)
    required_markers = [STABLE_MARKER]
    if args.editor_project:
        editor_project = stage_editor_project(binary)
        arguments.append(f"--project={editor_project.name}")
        required_markers.append("Editor CI smoke test opened project: CIEditorSmoke; scene: SampleScene")

    command = [str(binary), "--ci-smoke-test", *arguments]
    if os.name != "nt" and (xvfb_run := shutil.which("xvfb-run")):
        command = [xvfb_run, "-a", *command]

    kwargs: dict[str, object] = {
        "cwd": binary.parent,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "text": True,
        "encoding": "utf-8",
        "errors": "replace",
    }
    if os.name == "nt":
        kwargs["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    else:
        kwargs["start_new_session"] = True

    log_path, log_offset = log_snapshot(binary)
    process = subprocess.Popen(command, **kwargs)
    try:
        output, _ = process.communicate(timeout=max(args.timeout, 1.0))
    except subprocess.TimeoutExpired:
        terminate(process)
        if editor_project:
            editor_project.cleanup()
        print(f"Application did not exit naturally within {args.timeout:g} seconds.", file=sys.stderr)
        return 1

    if editor_project:
        editor_project.cleanup()

    output += read_log_tail(log_path, log_offset)

    if process.returncode != 0:
        print(f"Application exited with status {process.returncode}.\n{output}", file=sys.stderr)
        return 1
    for marker in required_markers:
        if marker not in output:
            print(f"Application exited without required marker {marker!r}.\n{output}", file=sys.stderr)
            return 1
    for marker in FORBIDDEN_MARKERS:
        if marker in output:
            print(f"Application log contained failure marker {marker!r}.\n{output}", file=sys.stderr)
            return 1

    print(f"Application smoke test passed with graceful exit: {binary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
