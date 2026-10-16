"""The game's engine packages (project.json "packages") and their manifests, for platform contributions.

Builders read package sections such as `android = { ... }` from funke-package.lua. Manifests are plain data
(the engine's C++ reader enforces the same), so a small literal-only Lua reader is enough here.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .protocol import BuilderError


class _LuaReader:
    """Parses `return { ... }` made of tables, strings, numbers, booleans and nil."""

    def __init__(self, text: str, origin: str) -> None:
        self.text = text
        self.pos = 0
        self.origin = origin

    def fail(self, message: str) -> BuilderError:
        line = self.text.count("\n", 0, self.pos) + 1
        return BuilderError(f"{self.origin}:{line}: {message} (package manifests must be plain data)")

    def skip(self) -> None:
        while self.pos < len(self.text):
            if self.text[self.pos].isspace():
                self.pos += 1
            elif self.text.startswith("--", self.pos):
                long = re.match(r"--\[(=*)\[", self.text[self.pos:])
                if long:
                    end = self.text.find("]" + long.group(1) + "]", self.pos)
                    self.pos = len(self.text) if end < 0 else end + len(long.group(1)) + 2
                else:
                    end = self.text.find("\n", self.pos)
                    self.pos = len(self.text) if end < 0 else end + 1
            else:
                return

    def peek(self, token: str) -> bool:
        self.skip()
        return self.text.startswith(token, self.pos)

    def expect(self, token: str) -> None:
        if not self.peek(token):
            raise self.fail(f"expected '{token}'")
        self.pos += len(token)

    def value(self) -> Any:
        self.skip()
        if self.pos >= len(self.text):
            raise self.fail("unexpected end of file")
        char = self.text[self.pos]
        if char == "{":
            return self.table()
        if char in "\"'":
            return self.short_string(char)
        long = re.match(r"\[(=*)\[", self.text[self.pos:])
        if long:
            start = self.pos + len(long.group(0))
            end = self.text.find("]" + long.group(1) + "]", start)
            if end < 0:
                raise self.fail("unterminated long string")
            self.pos = end + len(long.group(1)) + 2
            return self.text[start:end].removeprefix("\n")
        number = re.match(r"-?(0[xX][0-9a-fA-F]+|\d+(\.\d+)?([eE][-+]?\d+)?)", self.text[self.pos:])
        if number:
            self.pos += len(number.group(0))
            literal = number.group(0)
            return int(literal, 16) if "x" in literal.lower() else (float(literal) if "." in literal or "e" in literal.lower() else int(literal))
        word = re.match(r"[A-Za-z_][A-Za-z0-9_]*", self.text[self.pos:])
        if word and word.group(0) in ("true", "false", "nil"):
            self.pos += len(word.group(0))
            return {"true": True, "false": False, "nil": None}[word.group(0)]
        raise self.fail("expected a value")

    def short_string(self, quote: str) -> str:
        self.pos += 1
        out = []
        escapes = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", "\"": "\"", "'": "'", "0": "\0"}
        while self.pos < len(self.text):
            char = self.text[self.pos]
            if char == quote:
                self.pos += 1
                return "".join(out)
            if char == "\\" and self.pos + 1 < len(self.text):
                out.append(escapes.get(self.text[self.pos + 1], self.text[self.pos + 1]))
                self.pos += 2
                continue
            if char == "\n":
                break
            out.append(char)
            self.pos += 1
        raise self.fail("unterminated string")

    def table(self) -> Any:
        self.expect("{")
        fields: dict[str, Any] = {}
        array: list[Any] = []
        while not self.peek("}"):
            self.skip()
            key_match = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)", self.text[self.pos:])
            if key_match:
                self.pos += len(key_match.group(0))
                fields[key_match.group(1)] = self.value()
            elif self.peek("["):
                self.expect("[")
                key = self.value()
                self.expect("]")
                self.expect("=")
                fields[str(key)] = self.value()
            else:
                array.append(self.value())
            if self.peek(",") or self.peek(";"):
                self.pos += 1
            elif not self.peek("}"):
                raise self.fail("expected ',' or '}'")
        self.expect("}")
        if fields and array:
            fields.update({str(i + 1): v for i, v in enumerate(array)})
            return fields
        return array if array or not fields else fields

    def manifest(self) -> dict[str, Any]:
        self.expect("return")
        result = self.value()
        self.skip()
        if self.pos != len(self.text):
            raise self.fail("unexpected text after the manifest table")
        if not isinstance(result, dict):
            raise self.fail("the manifest must return a table with named fields")
        return result


def parse_manifest(path: Path) -> dict[str, Any]:
    return _LuaReader(path.read_text(encoding="utf-8-sig"), str(path)).manifest()


@dataclass
class ProjectPackage:
    name: str
    root: Path
    manifest: dict[str, Any]

    def section(self, name: str) -> dict[str, Any]:
        value = self.manifest.get(name)
        if value is None or value == []:
            return {}
        if not isinstance(value, dict):
            raise BuilderError(f"{self.name}: `{name}` in funke-package.lua must be a table with named fields.")
        return value

    def files(self, patterns: list[str] | str | None) -> list[Path]:
        """Package files matching premake-style globs relative to the package root ("Source/**.cs" recurses)."""
        if not patterns:
            return []
        found: list[Path] = []
        for pattern in [patterns] if isinstance(patterns, str) else patterns:
            # pathlib only recurses for a whole "**" segment; premake's "**.cs" means "any .cs below".
            glob = re.sub(r"\*\*(?!/)", "**/*", str(pattern).replace("\\", "/"))
            matches = sorted(p for p in self.root.glob(glob) if p.is_file())
            if not matches:
                raise BuilderError(f"{self.name}: '{pattern}' in funke-package.lua matches no files.")
            found += [m for m in matches if m not in found]
        return found


def _dependency_name(entry: Any) -> str:
    if isinstance(entry, dict):
        return str(entry.get("name", ""))
    return str(entry).split("@", 1)[0].strip()


def project_packages(engine_root: Path, project_root: Path, content_dir: Path | None = None) -> list[ProjectPackage]:
    """The packages project.json lists plus their dependencies, dependencies first.

    Looked up in <project>/Packages/<Name>, then <engine>/packages/<Name>; listed packages without a manifest
    (e.g. prebuilt DLLs only) contribute nothing to platform builds and are skipped.
    """
    project_json = (content_dir / "project.json") if content_dir and (content_dir / "project.json").is_file() \
        else project_root / "project.json"
    try:
        listed = json.loads(project_json.read_text(encoding="utf-8-sig")).get("packages") or []
    except (OSError, ValueError):
        return []
    roots = [project_root / "Packages", engine_root / "packages"]
    ordered: list[ProjectPackage] = []
    visiting: set[str] = set()

    def visit(name: str) -> None:
        if not name or any(p.name == name for p in ordered) or name in visiting:
            return
        root = next((r / name for r in roots if (r / name / "funke-package.lua").is_file()), None)
        if root is None:
            return
        visiting.add(name)
        manifest = parse_manifest(root / "funke-package.lua")
        for dependency in manifest.get("dependencies") or []:
            visit(_dependency_name(dependency))
        visiting.discard(name)
        ordered.append(ProjectPackage(name, root, manifest))

    for entry in listed:
        visit(_dependency_name(entry))
    return ordered
