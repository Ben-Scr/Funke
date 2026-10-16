#!/usr/bin/env python3
"""Materialize a self-contained EngineSDK folder next to the Launcher (Linux / macOS).

POSIX counterpart of scripts/CopyEngineSdk.bat, run by the Funke-Launcher postbuild. The result lets a
packaged Launcher folder create projects, build project packages and export games without the source tree:

    <launcher dir>/                 Funke-Editor, Funke-Runtime, funke, Funke-PackageTool, ScriptCore, ...
    <launcher dir>/EngineSDK/       engine headers, External headers, premake, licenses, engine library
    <launcher dir>/EngineSDK/Runtime-Development, Runtime-Dist   runtimes the editor exports games from

Usage:
    python3 scripts/CopyEngineSdk.py <sdk dir> <engine root> <config bin dir>
"""

from __future__ import annotations

import fnmatch
import os
import platform
import shutil
import sys
from pathlib import Path

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
    "glad/include", "dawn/include", "dawn/build/gen/include", "miniaudio", "box2d/include",
    "Index-Physics/include", "dotnet",
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
    return True


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    sdk = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    bin_dir = Path(sys.argv[3]).resolve()
    launcher_dir = sdk.parent

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

    # --- Engine sources (NativeScripts CMake include path) and project templates.
    mirror(root / "Funke-Engine" / "src", sdk / "Funke-Engine" / "src")
    mirror(root / "templates", sdk / "templates")

    # --- Funke-ScriptCore project and runtime artifacts.
    copy_file(root / "Funke-ScriptCore" / "Funke-ScriptCore.csproj", sdk / "Funke-ScriptCore" / "Funke-ScriptCore.csproj")
    copy_files(bin_dir / "Funke-ScriptCore", sdk / "Funke-ScriptCore", SCRIPT_CORE_RUNTIME_FILES)
    copy_file(bin_dir / "Funke-ScriptCore" / SCRIPT_CORE_DOCS, sdk / "Funke-ScriptCore" / SCRIPT_CORE_DOCS, required=False)

    # --- Project-local package build tooling.
    copy_file(root / "vendor" / "bin" / f"premake5{EXE}", sdk / f"premake5{EXE}")
    copy_files(root / "premake", sdk / "premake",
               ("project-packages.lua", "package-loader.lua", "fix_csharp_platforms.lua", "architecture.lua"))
    copy_files(root / "scripts" / "packages", sdk / "scripts" / "packages",
               ("NewPackage.py", "RemovePackage.py", "BumpPackageVersion.py", "WriteNativeAbi.py"))

    # --- Vendored headers the NativeScripts CMakeLists and native packages reference.
    for relative in EXTERNAL_DIRS:
        source = root / "External" / relative
        if source.is_dir():
            mirror(source, sdk / "External" / relative)

    # --- Engine library for the active config and Dist (native packages link it).
    cfg_plat = bin_dir.name
    copy_files(bin_dir / "Funke-Engine", sdk / "bin" / cfg_plat / "Funke-Engine", (ENGINE_LIB,))
    dist_plat = "Dist-" + cfg_plat.split("-", 1)[1]
    dist_engine = root / "bin" / dist_plat / "Funke-Engine"
    if (dist_engine / ENGINE_LIB).is_file():
        copy_files(dist_engine, sdk / "bin" / dist_plat / "Funke-Engine", (ENGINE_LIB,))

    licenses_dir = sdk / "Licenses"
    stage_licenses(root, licenses_dir)

    # --- Runtimes the editor exports games from without a source tree.
    if not stage_runtime(bin_dir / "Funke-Runtime", bin_dir / "Funke-ScriptCore", licenses_dir,
                         sdk / "Runtime-Development"):
        print(f"CopyEngineSdk: active-config runtime missing at {bin_dir / 'Funke-Runtime'}; "
              "Runtime-Development not bundled", file=sys.stderr)
    dist_bin = root / "bin" / dist_plat
    if not stage_runtime(dist_bin / "Funke-Runtime", dist_bin / "Funke-ScriptCore", licenses_dir,
                         sdk / "Runtime-Dist"):
        print(f"CopyEngineSdk: Dist runtime missing at {dist_bin / 'Funke-Runtime'}; Runtime-Dist not bundled",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SdkError as error:
        print(f"CopyEngineSdk: {error}", file=sys.stderr)
        sys.exit(1)
