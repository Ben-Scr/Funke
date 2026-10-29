#!/usr/bin/env python3
"""Materialize a self-contained EngineSDK folder next to the Launcher.

Shared by desktop postbuild steps and the Windows CopyEngineSdk.bat wrapper. The result lets a
packaged Launcher folder create projects, build project packages and export games without the source tree:

    <launcher dir>/                 Funke-Editor, Funke-Runtime, funke, Funke-PackageTool, ScriptCore, ...
    <launcher dir>/EngineSDK/       engine headers, External headers, premake, licenses, engine library
    <launcher dir>/EngineSDK/Runtime-Development, Runtime-Dist   runtimes the editor exports games from

Usage:
    python3 scripts/CopyEngineSdk.py <sdk dir> <engine root> <config bin dir>
"""

from __future__ import annotations

import fnmatch
import argparse
import json
import os
import platform
import shutil
import stat
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_manifest

IS_MAC = platform.system() == "Darwin"
EXE = ".exe" if os.name == "nt" else ""


def shared_lib(stem: str) -> str:
    if os.name == "nt":
        return f"{stem}.dll"
    return f"lib{stem}.dylib" if IS_MAC else f"lib{stem}.so"


SDL_LIB = "SDL3.dll" if os.name == "nt" else ("libSDL3.0.dylib" if IS_MAC else "libSDL3.so.0")
DAWN_LIB = None if os.name == "nt" else shared_lib("webgpu_dawn")
ENGINE_LIB = shared_lib("Funke-Engine")
TRACY_LIB = shared_lib("Tracy")

SCRIPT_CORE_RUNTIME_FILES = (
    "Funke-ScriptCore.dll",
    "Funke-ScriptCore.runtimeconfig.json",
    "Funke-ScriptCore.deps.json",
)
# IntelliSense docs; optional like in CopyEngineSdk.bat.
SCRIPT_CORE_DOCS = "Funke-ScriptCore.xml"

EXTERNAL_DIRS = (
    "spdlog/include", "glm", "entt/src", "stb", "magic_enum/include", "rapidxml", "SDL/include",
    "dawn/include", "dawn/build/gen/include", "miniaudio", "box2d/include",
    "dotnet",
)

LICENSES = (
    ("", "Funke", "LICENSE.txt"),
    ("", "Funke", "THIRD_PARTY_LICENSES.md"),
    ("External/box2d", "Box2D", "LICENSE"),
    ("External/JoltPhysics", "JoltPhysics", "LICENSE"),
    ("External/entt", "EnTT", "LICENSE"),
    ("External/glm", "GLM", "copying.txt"),
    ("External/SDL", "SDL3", "LICENSE.txt"),
    ("External/imgui", "Dear-ImGui", "LICENSE.txt"),
    ("External/ImGuizmo", "ImGuizmo", "LICENSE"),
    ("External/magic_enum", "magic_enum", "LICENSE"),
    ("External/miniaudio", "miniaudio", "LICENSE"),
    ("External/spdlog", "spdlog", "LICENSE"),
    ("External/stb", "stb", "LICENSE"),
    ("External/tracy", "Tracy", "LICENSE"),
    ("External/dawn", "Dawn", "LICENSE"),
    ("External/concurrentqueue", "concurrentqueue", "LICENSE.md"),
    ("External/rapidxml", "RapidXML", "LICENSE.txt"),
    ("Funke-Runtime/FunkeAssets/Fonts", "Fonts", "LICENSES.md"),
)
DAWN_LICENSE_PATTERNS = ("LICENSE*", "NOTICE*", "COPYING*", "ThirdPartyNotices.txt")


class SdkError(RuntimeError):
    pass


def prune_retired_dependencies(sdk: Path) -> None:
    sdk = sdk.resolve()
    external = sdk / "External"
    retired = [external / "Index-Physics", external / "Funke-Physics", external / "glad", sdk / "Index-Engine"]

    def validate(path: Path) -> None:
        attributes = path.lstat()
        if path.is_symlink() or getattr(attributes, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise SdkError(f"refusing to traverse linked SDK dependency: {path}")
        resolved = path.resolve()
        if not resolved.is_relative_to(sdk) or resolved == sdk:
            raise SdkError(f"retired dependency escapes SDK: {path}")

    try:
        if os.path.lexists(external):
            validate(external)
        targets = [path for path in retired if os.path.lexists(path)]
        for target in targets:
            pending = [target]
            while pending:
                path = pending.pop()
                validate(path)
                if path.is_dir():
                    pending.extend(path.iterdir())
        for target in targets:
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()
    except OSError as error:
        raise SdkError(f"could not remove retired SDK dependency: {error}") from error


def copy_file(source: Path, destination: Path, required: bool = True) -> bool:
    if not source.is_file():
        if required:
            raise SdkError(f"required file missing at {source}")
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    # copy2 keeps the executable bit and timestamps.
    shutil.copy2(source, destination)
    return True


def copy_files(source_dir: Path, destination_dir: Path, names, required: bool = True) -> None:
    for name in names:
        copy_file(source_dir / name, destination_dir / name, required)


def mirror(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, symlinks=False, ignore=shutil.ignore_patterns(".git"))


def remove_owned(sdk: Path, relative: str) -> None:
    target = sdk_manifest.contained_path(sdk, relative)
    if not target.exists():
        return
    for path in target.rglob("*") if target.is_dir() else ():
        sdk_manifest.contained_path(sdk, path.relative_to(sdk).as_posix())
    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()


def stage_headers(source: Path, destination: Path) -> None:
    for path in source.rglob("*"):
        if ".git" not in path.parts and path.is_file() and path.suffix.lower() in sdk_manifest.HEADER_SUFFIXES:
            copy_file(path, destination / path.relative_to(source))


def dotnet_license_dir() -> Path | None:
    found = shutil.which("dotnet")
    candidates = [Path(found).resolve().parent] if found else []
    candidates += [Path(p) for p in ("/usr/local/share/dotnet", "/usr/share/dotnet", "/usr/lib/dotnet")]
    root = os.environ.get("DOTNET_ROOT")
    if root:
        candidates.insert(0, Path(root))
    for candidate in candidates:
        if (candidate / "ThirdPartyNotices.txt").is_file():
            return candidate
    return None


def stage_licenses(root: Path, licenses_dir: Path) -> None:
    if licenses_dir.exists():
        shutil.rmtree(licenses_dir)
    for relative, folder, name in LICENSES:
        copy_file(root / relative / name, licenses_dir / folder / name)

    dawn_third_party = root / "External" / "dawn" / "third_party"
    if not dawn_third_party.is_dir():
        raise SdkError(f"Dawn third-party licenses missing at {dawn_third_party}")
    for directory, subdirs, files in os.walk(dawn_third_party):
        subdirs[:] = [d for d in subdirs if d != ".git"]
        for name in files:
            if any(fnmatch.fnmatch(name, pattern) for pattern in DAWN_LICENSE_PATTERNS):
                source = Path(directory) / name
                copy_file(source, licenses_dir / "Dawn-Third-Party" / source.relative_to(dawn_third_party))

    dotnet_dir = dotnet_license_dir()
    if not dotnet_dir:
        raise SdkError(".NET license files not found (dotnet is not on PATH and DOTNET_ROOT is unset)")
    copy_file(dotnet_dir / "LICENSE.txt", licenses_dir / "DotNet" / "LICENSE.txt")
    copy_file(dotnet_dir / "ThirdPartyNotices.txt", licenses_dir / "DotNet" / "ThirdPartyNotices.txt")


def stage_runtime(runtime_dir: Path, script_core_dir: Path, licenses_dir: Path, destination: Path) -> bool:
    if not (runtime_dir / f"Funke-Runtime{EXE}").is_file():
        return False
    if destination.exists():
        shutil.rmtree(destination)
    # Named files plus FunkeAssets only, so files left by running a game from that folder never leak in.
    copy_files(runtime_dir, destination, [f"Funke-Runtime{EXE}", ENGINE_LIB, SDL_LIB])
    if DAWN_LIB:
        copy_file(runtime_dir / DAWN_LIB, destination / DAWN_LIB)
    copy_file(runtime_dir / TRACY_LIB, destination / TRACY_LIB, required=False)
    copy_file(runtime_dir / "nethost.dll", destination / "nethost.dll", required=False)
    copy_files(script_core_dir, destination, SCRIPT_CORE_RUNTIME_FILES)
    mirror(runtime_dir / "FunkeAssets", destination / "FunkeAssets")
    mirror(licenses_dir, destination / "Licenses")
    platform_name = "windows" if os.name == "nt" else ("macosx" if IS_MAC else "linux")
    sdk_manifest.validate_runtime(destination.parent, destination.name, platform_name)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("bin_dir", type=Path)
    parser.add_argument("--binary-only", action="store_true", help="Omit optional native headers and link libraries")
    args = parser.parse_args()
    sdk, root, bin_dir = args.sdk.absolute(), args.root.resolve(), args.bin_dir.resolve()
    if sdk.name != "EngineSDK" or sdk == root or root.is_relative_to(sdk) or sdk.is_relative_to(root / "Funke-Engine"):
        raise SdkError("SDK target must be a dedicated EngineSDK directory outside engine sources")
    if os.path.lexists(sdk) and sdk_manifest.is_link(sdk):
        raise SdkError("refusing linked SDK directory")
    sdk = sdk.resolve()
    launcher_dir = sdk.parent
    prune_retired_dependencies(sdk)
    sdk.mkdir(parents=True, exist_ok=True)
    # Validate every destination before any mirror/delete follows an existing junction.
    for path in sdk.rglob("*"):
        sdk_manifest.contained_path(sdk, path.relative_to(sdk).as_posix())
    for relative in ("Funke-Engine", "Funke-ScriptCore", "bin", "Runtime-Development", "Runtime-Dist", "Runtime-Debug"):
        remove_owned(sdk, relative)
    if args.binary_only:
        remove_owned(sdk, "External")

    # --- Flat host applications next to the Launcher.
    copy_file(bin_dir / "Funke-Editor" / f"Funke-Editor{EXE}", launcher_dir / f"Funke-Editor{EXE}")
    copy_file(bin_dir / "Funke-Runtime" / f"Funke-Runtime{EXE}", launcher_dir / f"Funke-Runtime{EXE}")
    copy_file(bin_dir / "Funke-CLI" / f"funke{EXE}", launcher_dir / f"funke{EXE}", required=False)
    copy_files(bin_dir / "Funke-ScriptCore", launcher_dir, SCRIPT_CORE_RUNTIME_FILES)
    copy_file(bin_dir / "Funke-ScriptCore" / SCRIPT_CORE_DOCS, launcher_dir / SCRIPT_CORE_DOCS, required=False)
    copy_files(bin_dir / "Funke-ScriptCompiler", launcher_dir / "ScriptCompiler",
               ("Funke-ScriptCompiler.dll", "Funke-ScriptCompiler.deps.json", "Funke-ScriptCompiler.runtimeconfig.json"),
               required=False)
    copy_file(root / "build" / "BuildMetadata.json", launcher_dir / "BuildMetadata.json", required=False)
    copy_files(bin_dir / "Funke-PackageTool", launcher_dir,
               (f"Funke-PackageTool{EXE}", "Funke-PackageTool.dll", "Funke-PackageTool.runtimeconfig.json",
                "Funke-PackageTool.deps.json"))

    # Native headers retain the include paths used by existing native projects.
    if not args.binary_only:
        stage_headers(root / "Funke-Engine" / "src", sdk / "Funke-Engine" / "src")
    mirror(root / "templates", sdk / "templates")

    # ScriptCore is consumed as an assembly; no source project is distributed.
    copy_files(bin_dir / "Funke-ScriptCore", sdk / "Funke-ScriptCore", SCRIPT_CORE_RUNTIME_FILES)
    copy_file(bin_dir / "Funke-ScriptCore" / SCRIPT_CORE_DOCS, sdk / "Funke-ScriptCore" / SCRIPT_CORE_DOCS, required=False)

    # --- Project-local package build tooling.
    copy_file(root / "vendor" / "bin" / f"premake5{EXE}", sdk / f"premake5{EXE}")
    copy_files(root / "premake", sdk / "premake",
               ("project-packages.lua", "package-loader.lua", "fix_csharp_platforms.lua", "architecture.lua", "compat.lua"))
    copy_files(root / "scripts" / "packages", sdk / "scripts" / "packages",
               ("NewPackage.py", "RemovePackage.py", "BumpPackageVersion.py", "WriteNativeAbi.py"))
    copy_file(root / "scripts" / "BuildProjectPackages.ps1", sdk / "scripts" / "BuildProjectPackages.ps1", required=False)

    # --- Vendored headers the NativeScripts CMakeLists and native packages reference.
    if not args.binary_only:
        for relative in EXTERNAL_DIRS:
            source = root / "External" / relative
            if source.is_dir():
                remove_owned(sdk, "External/" + relative)
                stage_headers(source, sdk / "External" / relative)

    # --- Engine library for the active config and Dist (native packages link it).
    cfg_plat = bin_dir.name
    platform_name, architecture = cfg_plat.split("-", 2)[1:]
    link_files = (ENGINE_LIB, "Funke-Engine.lib") if os.name == "nt" else (ENGINE_LIB,)
    if not args.binary_only:
        copy_files(bin_dir / "Funke-Engine", sdk / "bin" / cfg_plat / "Funke-Engine", link_files)
    dist_plat = "Dist-" + cfg_plat.split("-", 1)[1]
    dist_engine = root / "bin" / dist_plat / "Funke-Engine"
    if not args.binary_only and (dist_engine / ENGINE_LIB).is_file():
        copy_files(dist_engine, sdk / "bin" / dist_plat / "Funke-Engine", link_files)

    licenses_dir = sdk / "Licenses"
    stage_licenses(root, licenses_dir)

    # --- Runtimes the editor exports games from without a source tree.
    runtimes = {}
    if stage_runtime(bin_dir / "Funke-Runtime", bin_dir / "Funke-ScriptCore", licenses_dir,
                     sdk / "Runtime-Development"):
        runtimes["Development"] = "Runtime-Development"
    else:
        print(f"CopyEngineSdk: active-config runtime missing at {bin_dir / 'Funke-Runtime'}; "
              "Runtime-Development not bundled", file=sys.stderr)
    dist_bin = root / "bin" / dist_plat
    if stage_runtime(dist_bin / "Funke-Runtime", dist_bin / "Funke-ScriptCore", licenses_dir,
                     sdk / "Runtime-Dist"):
        runtimes["Release"] = "Runtime-Dist"
    else:
        print(f"CopyEngineSdk: Dist runtime missing at {dist_bin / 'Funke-Runtime'}; Runtime-Dist not bundled",
              file=sys.stderr)
    if cfg_plat.startswith("Debug-"):
        stage_runtime(bin_dir / "Funke-Runtime", bin_dir / "Funke-ScriptCore", licenses_dir, sdk / "Runtime-Debug")
        runtimes["Debug"] = "Runtime-Debug"
    metadata = json.loads((launcher_dir / "BuildMetadata.json").read_text(encoding="utf-8-sig"))
    sdk_manifest.write_manifest(sdk, metadata["version"], platform_name, architecture, not args.binary_only, runtimes)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (SdkError, OSError, ValueError) as error:
        print(f"CopyEngineSdk: {error}", file=sys.stderr)
        sys.exit(1)
