"""Builder <-> editor protocol (version 1).

stdout carries JSON only, one object per line, so the editor can stream it:

    {"event": "progress", "value": 0.42, "stage": "Compiling engine"}
    {"event": "log", "level": "info"|"warn"|"error", "message": "..."}
    {"event": "result", "ok": true, "artifact": "<path>", "error": ""}

Tools the builder runs write to stderr / the build log, never to stdout.

The build request (written by the editor) is a JSON file:

    {
      "protocol": 1,
      "engineRoot": "<engine checkout or SDK root>",
      "projectRoot": "<game project>",
      "projectName": "MyGame",
      "contentDir": "<platform-neutral staged content: project.json, Assets/, IndexAssets/, bin/, managed/>",
      "outputDir": "<where the artifact goes>",
      "configuration": "Development" | "Release",
      "appName": "My Game", "appId": "com.company.mygame", "version": "1.0.0",
      "company": "Company", "iconPath": "<png or empty>",
      "entityBits": 20,
      "modules": ["Index.2D", "Index.3D"],   // engine feature modules; absent = all
      "options": { ... free-form, platform specific ... }
    }
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

PROTOCOL_VERSION = 1


class BuilderError(Exception):
    """A user-facing build failure (missing toolchain, compile error, ...)."""


def _emit(obj: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def emit_progress(value: float, stage: str) -> None:
    _emit({"event": "progress", "value": max(0.0, min(1.0, float(value))), "stage": stage})


def emit_log(message: str, level: str = "info") -> None:
    _emit({"event": "log", "level": level, "message": message})


def emit_result(ok: bool, artifact: str = "", error: str = "") -> None:
    _emit({"event": "result", "ok": ok, "artifact": artifact, "error": error})


@dataclass
class BuildRequest:
    engine_root: Path
    project_root: Path
    project_name: str
    content_dir: Path
    output_dir: Path
    configuration: str
    app_name: str
    app_id: str
    version: str
    company: str
    icon_path: Path | None
    entity_bits: int
    options: dict[str, Any] = field(default_factory=dict)
    # Engine feature modules (Index-Engine/EngineModules.json) the game uses; None = all of them.
    modules: list[str] | None = None

    @property
    def is_release(self) -> bool:
        return self.configuration == "Release"

    @property
    def modules_key(self) -> str:
        """Part of a build-cache directory name: one cache per engine-module combination."""
        if self.modules is None:
            return "all"
        return "-".join(sorted(m.removeprefix("Index.").lower() for m in self.modules)) or "nomodules"

    def cmake_module_args(self) -> list[str]:
        """index_add_engine(MODULES ...) input; "none" selects no module (an empty list would mean all)."""
        if self.modules is None:
            return []
        return ["-DINDEX_ENGINE_MODULES=" + (";".join(self.modules) if self.modules else "none")]

    def option(self, name: str, default: str = "") -> str:
        """A build option as text. "env:NAME" reads the environment, so secrets such as signing
        passwords stay out of .build files that are committed with the project."""
        value = self.options.get(name)
        if value is None or value == "":
            return default
        text = str(value)
        if text.startswith("env:"):
            variable = text[4:]
            if variable not in os.environ:
                raise BuilderError(f"Build option '{name}' reads the environment variable {variable}, which is not set.")
            return os.environ[variable]
        return text

    @staticmethod
    def load(path: Path) -> "BuildRequest":
        data = json.loads(path.read_text(encoding="utf-8"))
        if int(data.get("protocol", 0)) != PROTOCOL_VERSION:
            raise BuilderError(f"Unsupported build request protocol {data.get('protocol')} (builder speaks {PROTOCOL_VERSION}).")
        icon = data.get("iconPath") or ""
        return BuildRequest(
            engine_root=Path(data["engineRoot"]),
            project_root=Path(data["projectRoot"]),
            project_name=data["projectName"],
            content_dir=Path(data["contentDir"]),
            output_dir=Path(data["outputDir"]),
            configuration=data.get("configuration", "Development"),
            app_name=data.get("appName") or data["projectName"],
            app_id=data.get("appId") or "",
            version=data.get("version") or "1.0.0",
            company=data.get("company") or "",
            icon_path=Path(icon) if icon else None,
            entity_bits=int(data.get("entityBits", 20)),
            options=data.get("options") or {},
            modules=[str(m) for m in data["modules"]] if isinstance(data.get("modules"), list) else None,
        )


def run_builder(
    describe: Callable[[], dict[str, Any]],
    check: Callable[[Path | None], dict[str, Any]],
    build: Callable[[BuildRequest], Path],
) -> int:
    """Standard CLI for a platform builder: dispatches describe / check / build."""
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("describe", "check"):
        p = sub.add_parser(name)
        p.add_argument("--engine-root", type=Path, default=None)
    b = sub.add_parser("build")
    b.add_argument("--request", type=Path, required=True)
    args = parser.parse_args()

    if args.command == "describe":
        info = describe()
        info.setdefault("protocol", PROTOCOL_VERSION)
        _emit(info)
        return 0
    if args.command == "check":
        _emit(check(args.engine_root))
        return 0

    try:
        request = BuildRequest.load(args.request)
        artifact = build(request)
        emit_progress(1.0, "Done")
        emit_result(True, artifact=str(artifact))
        return 0
    except BuilderError as error:
        emit_log(str(error), "error")
        emit_result(False, error=str(error))
        return 1
    except Exception as error:  # surface unexpected failures with a trace in the log
        emit_log(traceback.format_exc(), "error")
        emit_result(False, error=f"{type(error).__name__}: {error}")
        return 1
