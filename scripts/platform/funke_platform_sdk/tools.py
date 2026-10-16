"""Process and tool discovery helpers. Tool output goes to stderr and the build log, never stdout."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Mapping

from .protocol import BuilderError

_log_file = None


def set_log_file(path: Path) -> None:
    """Mirror every tool's output into a log file (the editor links it on failure)."""
    global _log_file
    path.parent.mkdir(parents=True, exist_ok=True)
    _log_file = open(path, "w", encoding="utf-8", errors="replace")


def which(name: str, extra_dirs: Iterable[Path] = ()) -> Path | None:
    for directory in extra_dirs:
        for candidate in (Path(directory) / name, Path(directory) / f"{name}.exe", Path(directory) / f"{name}.bat", Path(directory) / f"{name}.cmd"):
            if candidate.is_file():
                return candidate
    found = shutil.which(name)
    return Path(found) if found else None


def find_cmake(engine_root: Path | None = None) -> Path:
    candidates = []
    if engine_root:
        candidates.append(engine_root / "vendor" / "cmake" / "bin")
    found = which("cmake", candidates)
    if not found:
        raise BuilderError("CMake was not found (install it or put it on PATH).")
    return found


def find_ninja(extra_dirs: Iterable[Path] = ()) -> Path | None:
    return which("ninja", extra_dirs)


def run(cmd: list[str | os.PathLike], cwd: Path | None = None, env: Mapping[str, str] | None = None,
        label: str | None = None) -> str:
    """Run a tool, stream its output to stderr/log, raise BuilderError with the tail on failure."""
    printable = " ".join(str(c) for c in cmd)
    _write(f"> {printable}\n")
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    process = subprocess.Popen([str(c) for c in cmd], cwd=str(cwd) if cwd else None, env=full_env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace")
    tail: list[str] = []
    assert process.stdout is not None
    for line in process.stdout:
        _write(line)
        tail.append(line.rstrip())
        if len(tail) > 40:
            tail.pop(0)
    code = process.wait()
    if code != 0:
        raise BuilderError(f"{label or Path(str(cmd[0])).name} failed (exit {code}):\n" + "\n".join(tail[-25:]))
    return "\n".join(tail)


def _write(text: str) -> None:
    try:
        sys.stderr.write(text)
    except UnicodeEncodeError:
        # A tool's non-ASCII output must not end the build on a console that cannot show it.
        encoding = sys.stderr.encoding or "ascii"
        sys.stderr.write(text.encode(encoding, "replace").decode(encoding))
    if _log_file:
        _log_file.write(text)
        _log_file.flush()
