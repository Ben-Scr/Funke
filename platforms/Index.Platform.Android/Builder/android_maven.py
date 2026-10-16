#!/usr/bin/env python3
"""Maven dependencies (AARs / JARs) that packages declare for Android builds, without Gradle.

Packages list root coordinates in index-package.lua (`android = { maven = { "group:artifact:version" } }`) and
ship `Android/maven.lock.json`, the resolved closure with a SHA-256 per file, so builds download exactly what the
package author tested. Without a matching lock the closure is resolved from the POMs (newest version wins, as in
Gradle) and checked against the repositories' published .sha256 instead.

Refresh a package's lock after changing its `maven` list:

    python android_maven.py lock <package dir>
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

REPOSITORIES = ("https://dl.google.com/dl/android/maven2", "https://repo1.maven.org/maven2")
LOCK_FILE = "Android/maven.lock.json"
LOCK_VERSION = 1
_POM_NS = "{http://maven.apache.org/POM/4.0.0}"
_SKIPPED_SCOPES = ("test", "provided", "system", "import")


def _sdk():
    from index_platform_sdk import BuilderError, emit_log, platform_cache  # noqa: E402
    return BuilderError, emit_log, platform_cache


def _error(message: str) -> Exception:
    return _sdk()[0](message)


def _log(message: str, level: str = "info") -> None:
    _sdk()[1](message, level)


def _cache(*parts: str) -> Path:
    return _sdk()[2]("Android", "maven", *parts)


@dataclass(frozen=True)
class Coordinate:
    group: str
    artifact: str
    version: str

    @property
    def key(self) -> str:
        return f"{self.group}:{self.artifact}"

    def __str__(self) -> str:
        return f"{self.group}:{self.artifact}:{self.version}"

    def path(self, extension: str) -> str:
        return f"{self.group.replace('.', '/')}/{self.artifact}/{self.version}/{self.artifact}-{self.version}.{extension}"

    @staticmethod
    def parse(text: str) -> "Coordinate":
        parts = str(text).strip().split(":")
        if len(parts) != 3 or not all(parts):
            raise _error(f"Maven coordinate '{text}' must be group:artifact:version.")
        return Coordinate(*parts)


@dataclass
class Artifact:
    coordinate: Coordinate
    type: str          # "aar" or "jar"
    url: str
    sha256: str
    size: int = 0

    def to_json(self) -> dict:
        return {"id": str(self.coordinate), "type": self.type, "url": self.url, "sha256": self.sha256, "size": self.size}

    @staticmethod
    def from_json(data: dict) -> "Artifact":
        return Artifact(Coordinate.parse(data["id"]), data["type"], data["url"], data["sha256"], int(data.get("size", 0)))


def version_key(version: str) -> tuple:
    """Maven-style ordering: numbers numerically, pre-releases (alpha/beta/rc/...) before the release."""
    qualifiers = {"alpha": 1, "a": 1, "beta": 2, "b": 2, "milestone": 3, "m": 3, "rc": 4, "cr": 4, "snapshot": 5,
                  "": 6, "ga": 6, "final": 6, "release": 6, "sp": 7}
    key = []
    for token in re.findall(r"\d+|[A-Za-z]+", version.lower()):
        key.append((1, int(token), 0) if token.isdigit() else (0, 0, qualifiers.get(token, 0)))
    # A release sorts after its own pre-releases: "1.0" > "1.0-beta01".
    key.append((0, 0, 6))
    return tuple(key)


def _fetch(url: str, timeout: int = 120) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Index-Platform-Android"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def _try_fetch(url: str) -> bytes | None:
    try:
        return _fetch(url)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise _error(f"Downloading {url} failed: HTTP {error.code}")
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise _error(f"Downloading {url} failed: {error}. Android builds with Maven dependencies need internet access "
                     "the first time.")


class _Poms:
    def __init__(self) -> None:
        self.cache = _cache("poms")
        self.memory: dict[str, tuple[str, ET.Element]] = {}

    def get(self, coordinate: Coordinate) -> tuple[str, ET.Element]:
        """(repository, root) of a POM, cached on disk: published versions never change."""
        key = str(coordinate)
        if key in self.memory:
            return self.memory[key]
        local = self.cache / coordinate.path("pom").replace("/", "_")
        repo_file = local.with_suffix(".repo")
        if local.is_file() and repo_file.is_file():
            result = repo_file.read_text(encoding="utf-8").strip(), ET.fromstring(local.read_bytes())
        else:
            result = None
            for repo in REPOSITORIES:
                data = _try_fetch(f"{repo}/{coordinate.path('pom')}")
                if data is not None:
                    local.write_bytes(data)
                    repo_file.write_text(repo, encoding="utf-8")
                    result = repo, ET.fromstring(data)
                    break
            if result is None:
                raise _error(f"Maven artifact {coordinate} was not found on Google Maven or Maven Central.")
        self.memory[key] = result
        return result


def _text(element: ET.Element | None, name: str) -> str:
    child = element.find(_POM_NS + name) if element is not None else None
    return (child.text or "").strip() if child is not None and child.text else ""


def _properties(poms: _Poms, pom: ET.Element, depth: int = 0) -> dict[str, str]:
    props: dict[str, str] = {}
    parent = pom.find(_POM_NS + "parent")
    if parent is not None and depth < 8:
        parent_coordinate = Coordinate(_text(parent, "groupId"), _text(parent, "artifactId"), _text(parent, "version"))
        props.update(_properties(poms, poms.get(parent_coordinate)[1], depth + 1))
        props.setdefault("project.parent.version", parent_coordinate.version)
    group = _text(pom, "groupId") or _text(parent, "groupId")
    version = _text(pom, "version") or _text(parent, "version")
    props.update({"project.groupId": group, "pom.groupId": group, "project.version": version, "pom.version": version})
    for prop in pom.find(_POM_NS + "properties") or []:
        props[prop.tag.replace(_POM_NS, "")] = (prop.text or "").strip()
    return props


def _expand(value: str, props: dict[str, str]) -> str:
    for _ in range(5):
        expanded = re.sub(r"\$\{([^}]+)\}", lambda m: props.get(m.group(1), m.group(0)), value)
        if expanded == value:
            break
        value = expanded
    return value


def _pinned(spec: str, coordinate_hint: str) -> str:
    """'1.2.3' and Gradle-published exact ranges '[1.2.3]'; other ranges take their lower bound."""
    spec = spec.strip()
    if not spec:
        raise _error(f"{coordinate_hint} has no version in its POM.")
    if spec[0] in "[(":
        bounds = spec.strip("[]()").split(",")
        if not bounds[0].strip():
            raise _error(f"Cannot resolve the open version range {spec} of {coordinate_hint}.")
        return bounds[0].strip()
    return spec


@dataclass
class _Node:
    coordinate: Coordinate
    type: str
    dependencies: list[tuple[Coordinate, str, frozenset[str]]] = field(default_factory=list)
    constraints: list[Coordinate] = field(default_factory=list)


def _managed(poms: _Poms, pom: ET.Element, props: dict[str, str], depth: int = 0) -> dict[str, str]:
    """dependencyManagement versions of a POM, its parents and imported BOMs (nearest wins)."""
    managed: dict[str, str] = {}
    parent = pom.find(_POM_NS + "parent")
    if parent is not None and depth < 8:
        parent_pom = poms.get(Coordinate(_text(parent, "groupId"), _text(parent, "artifactId"), _text(parent, "version")))[1]
        managed.update(_managed(poms, parent_pom, _properties(poms, parent_pom), depth + 1))
    for dep in pom.findall(f"{_POM_NS}dependencyManagement/{_POM_NS}dependencies/{_POM_NS}dependency"):
        group, artifact = _expand(_text(dep, "groupId"), props), _expand(_text(dep, "artifactId"), props)
        version = _expand(_text(dep, "version"), props)
        if not version or "${" in version:
            continue
        if _text(dep, "scope") == "import" and depth < 8:
            bom = poms.get(Coordinate(group, artifact, _pinned(version, f"{group}:{artifact}")))[1]
            managed.update(_managed(poms, bom, _properties(poms, bom), depth + 1))
        elif _text(dep, "scope") not in _SKIPPED_SCOPES:
            managed[f"{group}:{artifact}"] = _pinned(version, f"{group}:{artifact}")
    return managed


def _node(poms: _Poms, coordinate: Coordinate, requested_type: str) -> _Node:
    _, pom = poms.get(coordinate)
    props = _properties(poms, pom)
    packaging = _text(pom, "packaging") or "jar"
    node = _Node(coordinate, "aar" if "aar" in (packaging, requested_type) else ("pom" if packaging == "pom" else "jar"))
    managed = _managed(poms, pom, props)
    # Only the module's own constraints align versions; parents' and BOMs' merely fill in missing versions.
    for dep in pom.findall(f"{_POM_NS}dependencyManagement/{_POM_NS}dependencies/{_POM_NS}dependency"):
        key = f"{_expand(_text(dep, 'groupId'), props)}:{_expand(_text(dep, 'artifactId'), props)}"
        if _text(dep, "scope") not in _SKIPPED_SCOPES and key in managed:
            node.constraints.append(Coordinate(*key.split(":"), managed[key]))
    for dep in pom.findall(f"{_POM_NS}dependencies/{_POM_NS}dependency"):
        if _text(dep, "scope") in _SKIPPED_SCOPES or _text(dep, "optional") == "true" or _text(dep, "classifier"):
            continue
        group, artifact = _expand(_text(dep, "groupId"), props), _expand(_text(dep, "artifactId"), props)
        version = _expand(_text(dep, "version"), props) or managed.get(f"{group}:{artifact}", "")
        exclusions = frozenset(f"{_text(e, 'groupId')}:{_text(e, 'artifactId')}"
                               for e in dep.findall(f"{_POM_NS}exclusions/{_POM_NS}exclusion"))
        node.dependencies.append((Coordinate(group, artifact, _pinned(version, f"{group}:{artifact} (from {coordinate})")),
                                  _text(dep, "type") or "jar", exclusions))
    return node


def resolve(roots: list[str]) -> list[Artifact]:
    """The full closure of roots, dependencies first; the newest requested version of each module wins."""
    poms = _Poms()
    selected: dict[str, str] = {}
    types: dict[str, str] = {}
    root_coordinates = [Coordinate.parse(r) for r in roots]
    for _ in range(64):
        changed = False
        order: list[str] = []
        visited: set[str] = set()
        constraints: list[Coordinate] = []

        def visit(coordinate: Coordinate, requested_type: str, excluded: frozenset[str]) -> None:
            nonlocal changed
            current = selected.get(coordinate.key)
            if current is None or version_key(coordinate.version) > version_key(current):
                selected[coordinate.key] = coordinate.version
                changed = True
            if coordinate.key in visited:
                return
            visited.add(coordinate.key)
            node = _node(poms, Coordinate(coordinate.group, coordinate.artifact, selected[coordinate.key]), requested_type)
            types[coordinate.key] = node.type
            constraints.extend(node.constraints)
            for dependency, dependency_type, exclusions in node.dependencies:
                if dependency.key not in excluded:
                    visit(dependency, dependency_type, excluded | exclusions)
            order.append(coordinate.key)

        for root in root_coordinates:
            visit(root, "jar", frozenset())
        # Constraints (androidx publishes them to keep e.g. all lifecycle-* on one version) only raise
        # versions of modules that are already part of the graph.
        for constraint in constraints:
            current = selected.get(constraint.key)
            if constraint.key in visited and current and version_key(constraint.version) > version_key(current):
                selected[constraint.key] = constraint.version
                changed = True
        if not changed:
            break
    else:
        raise _error("Maven dependency resolution did not settle; check the packages' maven lists.")

    artifacts = []
    for key in order:
        if types.get(key) == "pom":
            continue
        coordinate = Coordinate(*key.split(":"), selected[key])
        repo = poms.get(coordinate)[0]
        artifacts.append(_locate(repo, coordinate, types.get(key, "jar")))
    return artifacts


def _locate(repo: str, coordinate: Coordinate, type_: str) -> Artifact:
    for extension in ([type_, "jar" if type_ == "aar" else "aar"]):
        url = f"{repo}/{coordinate.path(extension)}"
        published = _try_fetch(url + ".sha256")
        if published is not None:
            return Artifact(coordinate, extension, url, published.decode("ascii", "replace").split()[0].lower())
        if _try_fetch(url + ".sha1") is not None:
            return Artifact(coordinate, extension, url, "")
    raise _error(f"No .aar or .jar for {coordinate} in {repo}.")


def download(artifact: Artifact) -> Path:
    """The verified file in the per-user cache; downloads it once."""
    target = _cache("files") / f"{artifact.coordinate.group}" / f"{artifact.coordinate.artifact}-{artifact.coordinate.version}.{artifact.type}"
    target.parent.mkdir(parents=True, exist_ok=True)
    verified = target.with_name(target.name + ".sha256")
    if target.is_file() and verified.is_file():
        known = verified.read_text(encoding="utf-8").strip()
        if not artifact.sha256 or known == artifact.sha256:
            artifact.sha256 = known
            artifact.size = target.stat().st_size
            return target
    data = _fetch(artifact.url, timeout=600)
    actual = hashlib.sha256(data).hexdigest()
    if artifact.sha256 and actual != artifact.sha256:
        raise _error(f"{artifact.coordinate}: SHA-256 mismatch (expected {artifact.sha256}, got {actual}); not using it.")
    if not artifact.sha256:
        published = _try_fetch(artifact.url + ".sha1")
        if published is None or hashlib.sha1(data).hexdigest() != published.decode("ascii", "replace").split()[0].lower():
            raise _error(f"{artifact.coordinate}: checksum does not match the repository's .sha1; not using it.")
        artifact.sha256 = actual
    artifact.size = len(data)
    partial = target.with_name(target.name + ".part")
    partial.write_bytes(data)
    partial.replace(target)
    verified.write_text(actual, encoding="utf-8")
    return target


def read_lock(package_root: Path, roots: list[str]) -> list[Artifact] | None:
    """The package's pinned closure, or None when it is missing or was made for another root list."""
    path = package_root / LOCK_FILE
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise _error(f"{path} is not valid JSON: {error}")
    if data.get("lockVersion") != LOCK_VERSION or sorted(data.get("roots", [])) != sorted(roots):
        return None
    return [Artifact.from_json(a) for a in data.get("artifacts", [])]


def write_lock(package_root: Path, roots: list[str]) -> Path:
    artifacts = resolve(roots)
    for artifact in artifacts:
        _log(f"  {artifact.coordinate} ({artifact.type})")
        download(artifact)
    path = package_root / LOCK_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"lockVersion": LOCK_VERSION, "roots": sorted(roots), "artifacts": [a.to_json() for a in artifacts]}
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    return path


def merge(closures: list[list[Artifact]]) -> list[Artifact]:
    """One closure from several packages': the newest version of each module, first occurrence keeps its place."""
    newest: dict[str, Artifact] = {}
    order: list[str] = []
    for closure in closures:
        for artifact in closure:
            key = artifact.coordinate.key
            if key not in newest:
                order.append(key)
                newest[key] = artifact
            elif version_key(artifact.coordinate.version) > version_key(newest[key].coordinate.version):
                newest[key] = artifact
    return [newest[key] for key in order]


def _main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] != "lock":
        print(__doc__.strip(), file=sys.stderr)
        return 2
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here.parents[2] / "scripts" / "platform"))
    from index_platform_sdk.packages import parse_manifest  # noqa: E402
    package_root = Path(argv[2]).resolve()
    android = parse_manifest(package_root / "index-package.lua").get("android") or {}
    roots = [str(r) for r in (android.get("maven") or [])]
    if not roots:
        print(f"{package_root}: index-package.lua declares no android.maven dependencies.", file=sys.stderr)
        return 1
    path = write_lock(package_root, roots)
    print(f"Wrote {path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
