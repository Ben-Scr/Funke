#!/usr/bin/env python3
"""Stage a Windows installer from an existing bundle and a reviewed package lock.

Downloads happen on the release machine. Setup uses only these embedded files.
Optional packages are available in the editor's Package Manager; each project
still chooses its own packages, and the existing .NET build compiles their sources.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import stat
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))
import sdk_manifest
sys.path.insert(0, str(REPO_ROOT / "scripts" / "platform"))
from funke_platform_sdk.packages import parse_manifest  # noqa: E402

MAX_ARCHIVE_BYTES = 64 * 1024 * 1024
MAX_EXTRACTED_BYTES = 256 * 1024 * 1024
MAX_FILES = 10000
PACKAGE_NAME = re.compile(r"[A-Z][A-Za-z0-9]+(?:\.[A-Z][A-Za-z0-9]+)+\Z")
VERSION = re.compile(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?(?:\+[A-Za-z0-9.-]+)?\Z")
COMPONENT_ID = re.compile(r"[a-z][a-z0-9_]*\Z")
RESERVED = re.compile(r"(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?\Z", re.I)
REQUIRED_FILES = (
    "Funke-Launcher.exe", "Funke-Editor.exe", "Funke-Runtime.exe", "Funke-Engine.dll",
    "Funke-ScriptCore.dll", "Funke-ScriptCore.runtimeconfig.json", "Funke-PackageTool.exe",
    "SDL3.dll", "nethost.dll", "funke.exe", "BuildMetadata.json",
    "EngineSDK/premake5.exe", "EngineSDK/premake/project-packages.lua",
    "EngineSDK/Licenses/Funke/LICENSE.txt", "EngineSDK/Runtime-Dist/Funke-Runtime.exe",
    "EngineSDK/Runtime-Dist/Funke-Engine.dll",
)
SKIP_DIRECTORIES = {".git", ".vs", ".idea", "__pycache__"}
SKIP_SUFFIXES = {".pdb", ".log", ".dmp", ".pyc", ".ilk", ".exp"}
SKIP_ROOT_FILES = {"imgui.ini", "funke-project.json", "editorpreferences.json", "launchersettings.json"}
RETIRED_SDK_ROOTS = (
    ("enginesdk", "external", "index-physics"), ("enginesdk", "external", "funke-physics"),
    ("enginesdk", "external", "glad"), ("enginesdk", "index-engine"),
)
RETIRED_SDK_FILES = {
    "enginesdk/funke-engine/src/physics/" + name.lower()
    for name in ("FastPhysics2D.hpp", "FastPhysics2D.cpp", "IndexPhysicsWorld2D.hpp",
                 "IndexPhysicsWorld2D.cpp", "IndexPhysicsInterop.hpp", "IndexContact2D.hpp")
} | {
    "enginesdk/funke-engine/src/components/physics/" + name.lower()
    for name in ("FastBody2DComponent.hpp", "FastBody2DComponent.cpp", "FastBoxCollider2DComponent.hpp",
                 "FastBoxCollider2DComponent.cpp", "FastCircleCollider2DComponent.hpp", "FastCircleCollider2DComponent.cpp")
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_catalog(path: Path) -> list[dict]:
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if document.get("schemaVersion") != 1 or not isinstance(document.get("packages"), list):
        raise ValueError("Package catalog must have schemaVersion 1 and a packages array")
    packages = document["packages"]
    names, ids = set(), set()
    for entry in packages:
        if not isinstance(entry, dict):
            raise ValueError("Each catalog entry must be an object")
        name, component = entry.get("name", ""), entry.get("id", "")
        if not PACKAGE_NAME.fullmatch(name) or name.casefold() in names:
            raise ValueError(f"Invalid or duplicate package name: {name}")
        if not COMPONENT_ID.fullmatch(component) or component in ids:
            raise ValueError(f"Invalid or duplicate component id: {component}")
        if not VERSION.fullmatch(entry.get("version", "")):
            raise ValueError(f"Invalid pinned version for {name}")
        if not re.fullmatch(r"[a-fA-F0-9]{64}", entry.get("sha256", "")):
            raise ValueError(f"Missing SHA-256 for {name}")
        url = urllib.parse.urlparse(entry.get("downloadUrl", ""))
        if url.scheme != "https" or not url.hostname or url.username or url.password:
            raise ValueError(f"Package download must use HTTPS without credentials: {name}")
        if entry.get("layers") != ["csharp"]:
            raise ValueError(f"Installer currently accepts only pure C# packages: {name}")
        dependencies = entry.get("dependencies", [])
        if not isinstance(dependencies, list) or any(not isinstance(item, str) for item in dependencies):
            raise ValueError(f"Dependencies must be exact locked package names: {name}")
        if len(set(dependencies)) != len(dependencies):
            raise ValueError(f"Duplicate dependencies for {name}")
        names.add(name.casefold())
        ids.add(component)
    dependency_closures(packages)
    return packages


def dependency_closures(packages: list[dict]) -> dict[str, set[str]]:
    by_name = {entry["name"]: entry for entry in packages}
    complete: dict[str, set[str]] = {}

    def visit(name: str, stack: set[str]) -> set[str]:
        if name not in by_name:
            raise ValueError(f"Dependency is missing from the package lock: {name}")
        if name in stack:
            raise ValueError(f"Package dependency cycle involving {name}")
        if name not in complete:
            result = {name}
            for dependency in by_name[name].get("dependencies", []):
                result.update(visit(dependency, stack | {name}))
            complete[name] = result
        return complete[name]

    for name in by_name:
        visit(name, set())
    return complete


def download_archive(entry: dict, cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    expected = entry["sha256"].lower()
    destination = cache / f"{entry['name']}-{entry['version']}-{expected}.zip"
    if destination.exists():
        if destination.is_symlink() or sha256_file(destination) != expected:
            raise ValueError(f"Cached package checksum mismatch: {destination}")
        return destination
    temporary = None
    request = urllib.request.Request(entry["downloadUrl"], headers={"User-Agent": "Funke-Installer-Builder/1"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, tempfile.NamedTemporaryFile(
                mode="wb", suffix=".download", prefix=entry["id"] + "-", dir=cache, delete=False) as output:
            temporary = Path(output.name)
            if urllib.parse.urlparse(response.url).scheme != "https":
                raise ValueError(f"Package download redirected away from HTTPS: {entry['name']}")
            count = 0
            while block := response.read(1024 * 1024):
                count += len(block)
                if count > MAX_ARCHIVE_BYTES:
                    raise ValueError(f"Package archive exceeds size limit: {entry['name']}")
                output.write(block)
        if sha256_file(temporary) != expected:
            raise ValueError(f"Downloaded package checksum mismatch: {entry['name']}")
        temporary.replace(destination)
    finally:
        if temporary is not None and temporary.is_file() and not temporary.is_symlink():
            temporary.unlink()
    return destination


def safe_archive_path(name: str, package_name: str) -> PurePosixPath:
    if "\\" in name or "\x00" in name:
        raise ValueError(f"Unsafe ZIP member: {name!r}")
    path = PurePosixPath(name)
    raw_parts = name.rstrip("/").split("/")
    if path.is_absolute() or len(raw_parts) < 1 or raw_parts[0] != package_name:
        raise ValueError(f"ZIP must contain one package folder named {package_name}: {name}")
    for part in raw_parts:
        if (not part or part in (".", "..") or part.endswith((".", " "))
                or re.search(r'[<>:"|?*\x00-\x1f]', part) or RESERVED.fullmatch(part)):
            raise ValueError(f"Unsafe ZIP member: {name!r}")
    return path


def extract_package(archive: Path, output: Path, entry: dict) -> Path:
    if archive.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("Package archive exceeds size limit")
    package_root = output / entry["name"]
    if package_root.exists():
        raise ValueError(f"Refusing to overwrite package directory: {package_root}")
    with zipfile.ZipFile(archive) as source:
        members = source.infolist()
        if len(members) > MAX_FILES or sum(item.file_size for item in members) > MAX_EXTRACTED_BYTES:
            raise ValueError("Package exceeds extraction limits")
        seen = set()
        checked = []
        for item in members:
            path = safe_archive_path(item.orig_filename, entry["name"])
            key = str(path).casefold()
            if key in seen:
                raise ValueError(f"Duplicate ZIP member: {item.filename}")
            seen.add(key)
            mode = item.external_attr >> 16
            if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise ValueError(f"Unsupported ZIP member type: {item.filename}")
            if item.flag_bits & 1:
                raise ValueError("Encrypted package archives are unsupported")
            checked.append((item, path))
        for item, relative in checked:
            target = output / Path(*relative.parts)
            if item.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(item) as input_stream, target.open("xb") as output_stream:
                    shutil.copyfileobj(input_stream, output_stream)
    manifest = parse_manifest(package_root / "funke-package.lua")
    if manifest.get("name") != entry["name"] or manifest.get("version") != entry["version"]:
        raise ValueError(f"Package manifest identity differs from lock: {entry['name']}")
    if set(manifest.get("layers", {})) != {"csharp"}:
        raise ValueError(f"Package contains an unsupported native layer: {entry['name']}")
    if sorted(manifest.get("dependencies", [])) != sorted(entry.get("dependencies", [])):
        raise ValueError(f"Package dependencies differ from lock: {entry['name']}")
    if not list(package_root.rglob("*.cs")):
        raise ValueError(f"Package has no C# sources: {entry['name']}")
    (package_root / ".funke-installer-package.json").write_text(
        json.dumps({"name": entry["name"], "version": entry["version"], "sha256": entry["sha256"]}) + "\n",
        encoding="utf-8")
    return package_root


def bundle_component(relative: Path, include_native_sdk: bool = False) -> str | None:
    lowered = tuple(part.lower() for part in relative.parts)
    if any(lowered[:len(root)] == root for root in RETIRED_SDK_ROOTS):
        return None
    if "/".join(lowered) in RETIRED_SDK_FILES:
        return None
    if any(part in SKIP_DIRECTORIES for part in lowered):
        return None
    if relative.suffix.lower() in SKIP_SUFFIXES or relative.name.lower() == ".git":
        return None
    if len(lowered) == 1 and lowered[0] in SKIP_ROOT_FILES:
        return None
    if lowered[:2] in (("enginesdk", "packages"), ("enginesdk", "platforms")):
        return None  # Only the reviewed catalog may introduce optional packages.
    if lowered[:2] in (("enginesdk", "funke-engine"), ("enginesdk", "external"), ("enginesdk", "bin")):
        if not include_native_sdk:
            return None
        if lowered[1] != "bin" and relative.suffix and relative.suffix.lower() not in sdk_manifest.HEADER_SUFFIXES:
            return None
    if lowered[:3] == ("enginesdk", "funke-scriptcore", "source") or relative.suffix.lower() in (".csproj", ".sln", ".vcxproj"):
        return None
    if lowered[:2] == ("enginesdk", "templates"):
        return "Templates"
    if lowered == ("funke.exe",):
        return "CLI"
    return "Core"


def copy_bundle(bundle: Path, output: Path, include_native_sdk: bool = False) -> dict[str, int]:
    manifest = sdk_manifest.validate_sdk(bundle / "EngineSDK")
    if include_native_sdk and not manifest["capabilities"]["nativeDevelopment"]:
        raise ValueError("The input bundle has no native SDK; stage it with native development support first")
    missing = [name for name in REQUIRED_FILES if not (bundle / name).is_file()]
    if missing:
        raise ValueError("Bundle is incomplete: " + ", ".join(missing))
    if not (bundle / "EngineSDK" / "templates").is_dir():
        raise ValueError("Bundle is missing project templates")
    counts = {"Core": 0, "CLI": 0, "Templates": 0}
    for path in sorted(bundle.rglob("*")):
        relative = path.relative_to(bundle)
        component = bundle_component(relative, include_native_sdk)
        if component is None:
            continue
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Bundle contains a link or reparse point: {relative}")
        if path.is_file():
            destination = output / component / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
            counts[component] += 1
    if not all(counts.values()):
        raise ValueError("Bundle must contain core files, CLI and at least one project template")
    manifest["capabilities"]["nativeDevelopment"] = include_native_sdk
    (output / "Core/EngineSDK" / sdk_manifest.MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    sdk_manifest.validate_sdk(output / "Core/EngineSDK", require_binary=not include_native_sdk, require_templates=False)
    return counts


def write_includes(output: Path, packages: list[dict], include_native_sdk: bool = False) -> None:
    closures = dependency_closures(packages)
    components = ['Name: "packages"; Description: "{cm:OptionalPackages}"; Types: full; Flags: dontinheritcheck'] if packages else []
    files = []
    deletes = []
    for entry in packages:
        component = "packages\\" + entry["id"]
        title = entry["name"] + " " + entry["version"]
        components.append(f'Name: "{component}"; Description: "{title}"; Types: full; Flags: dontinheritcheck')
        consumers = ["packages\\" + candidate["id"] for candidate in packages
                     if entry["name"] in closures[candidate["name"]]]
        condition = " or ".join(consumers)
        files.append('Source: "{#PayloadDir}\\Packages\\' + entry["id"] + '\\*"; DestDir: "{app}"; '
                     f'Components: {condition}; Flags: ignoreversion recursesubdirs createallsubdirs')
        package_dir = "{app}\\EngineSDK\\packages\\" + entry["name"]
        marker = package_dir + "\\.funke-installer-package.json"
        deletes.append(f'Type: filesandordirs; Name: "{package_dir}"; '
                       f"Check: WizardIsComponentSelected('{condition}') and FileExists(ExpandConstant('{marker}'))")
    (output / "Components.iss").write_text("\n".join(components) + "\n", encoding="utf-8-sig")
    (output / "Files.iss").write_text("\n".join(files) + "\n", encoding="utf-8-sig")
    (output / "PackageDelete.iss").write_text("\n".join(deletes) + "\n", encoding="utf-8-sig")
    obsolete = ["Funke-ScriptCore\\Funke-ScriptCore.csproj", "Funke-ScriptCore\\Source",
                "Funke-Engine", "External", "bin"]
    (output / "SdkDelete.iss").write_text("\n".join(
        'Type: filesandordirs; Name: "{app}\\EngineSDK\\' + name + '";'
        for name in obsolete) + "\n", encoding="utf-8-sig")


def prepare(bundle: Path, repo: Path, output: Path, catalog: Path, cache: Path, include_native_sdk: bool = False) -> dict:
    bundle, repo, output = bundle.resolve(), repo.resolve(), output.resolve()
    if output == repo or output.is_relative_to(bundle) or bundle.is_relative_to(output):
        raise ValueError("Payload directory must be separate from the bundle and repository root")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Payload directory must be empty: {output}")
    packages = load_catalog(catalog)
    output.mkdir(parents=True, exist_ok=True)
    counts = copy_bundle(bundle, output, include_native_sdk)
    for entry in packages:
        print(f"[Installer] Staging {entry['name']} {entry['version']}", flush=True)
        archive = download_archive(entry, cache)
        extract_package(archive, output / "Packages" / entry["id"] / "EngineSDK" / "packages", entry)
    write_includes(output, packages, include_native_sdk)
    metadata = {
        "schemaVersion": 1,
        "engine": json.loads((bundle / "BuildMetadata.json").read_text(encoding="utf-8-sig")),
        "components": counts,
        "nativeSdkIncluded": include_native_sdk,
        "packages": packages,
        "packageUsage": "Available in Package Manager; enabled and compiled separately for each project.",
    }
    (output / "payload-manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--repo", default=REPO_ROOT, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--catalog", default=Path(__file__).with_name("packages.lock.json"), type=Path)
    parser.add_argument("--cache", default=REPO_ROOT / "bin-int" / "installer" / "package-cache", type=Path)
    parser.add_argument("--include-native-sdk", action="store_true", help="Include optional native headers and engine link libraries")
    args = parser.parse_args()
    try:
        prepare(args.bundle, args.repo, args.output, args.catalog, args.cache, args.include_native_sdk)
        print(f"[Installer] Payload: {args.output.resolve()}")
        return 0
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"[Installer] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
