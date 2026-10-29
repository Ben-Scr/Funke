"""Shared contract for a relocatable Funke SDK, independent of a source checkout."""

from __future__ import annotations

import argparse
import json
import os
import re
import stat
import io
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

MANIFEST = "FunkeSDK.json"
SCRIPT_CORE_FILES = ("Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json")
HEADER_SUFFIXES = {".h", ".hpp", ".hh", ".hxx", ".inl", ".ipp", ".tpp", ".inc"}
NATIVE_ROOTS = {"Funke-Engine", "External", "bin"}
PLATFORMS = {
    "windows": ("Funke-Runtime.exe", "Funke-Engine.dll", "SDL3.dll", "nethost.dll"),
    "linux": ("Funke-Runtime", "libFunke-Engine.so", "libSDL3.so.0", "libwebgpu_dawn.so"),
    "macosx": ("Funke-Runtime", "libFunke-Engine.dylib", "libSDL3.0.dylib", "libwebgpu_dawn.dylib"),
}
NATIVE_HEADERS = (
    "Funke-Engine/src/Funke.hpp", "External/spdlog/include/spdlog/spdlog.h",
    "External/glm/glm/glm.hpp", "External/entt/src/entt/entt.hpp",
    "External/SDL/include/SDL3/SDL.h", "External/dawn/include/webgpu/webgpu_cpp.h",
)
RUNTIME_LICENSES = (
    "Funke/LICENSE.txt", "Funke/THIRD_PARTY_LICENSES.md", "Box2D/LICENSE", "JoltPhysics/LICENSE",
    "EnTT/LICENSE", "GLM/copying.txt", "SDL3/LICENSE.txt", "Dear-ImGui/LICENSE.txt", "ImGuizmo/LICENSE",
    "magic_enum/LICENSE", "miniaudio/LICENSE", "spdlog/LICENSE", "stb/LICENSE", "Tracy/LICENSE",
    "Dawn/LICENSE", "concurrentqueue/LICENSE.md", "RapidXML/LICENSE.txt", "Fonts/LICENSES.md",
    "DotNet/LICENSE.txt", "DotNet/ThirdPartyNotices.txt", "Dawn-Third-Party/abseil-cpp/LICENSE",
)


def required_licenses(platform: str) -> tuple[str, ...]:
    return RUNTIME_LICENSES + (("Dawn-Third-Party/directx-shader-compiler/LICENSE",) if platform == "windows" else ())


def is_link(path: Path) -> bool:
    info = path.lstat()
    return path.is_symlink() or bool(getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def contained_path(root: Path, relative: str, *, allow_file_links: bool = False) -> Path:
    if (not isinstance(relative, str) or not relative or "\\" in relative or ":" in relative
            or relative.startswith("/") or any(p in ("", ".", "..") for p in relative.split("/"))):
        raise ValueError(f"SDK path must be a safe relative path: {relative!r}")
    root = root.resolve()
    path = root.joinpath(*PurePosixPath(relative).parts)
    if not path.resolve().is_relative_to(root) or path.resolve() == root:
        raise ValueError(f"SDK path escapes installation: {relative}")
    current = root
    for part in PurePosixPath(relative).parts:
        current /= part
        if os.path.lexists(current) and is_link(current):
            if allow_file_links and current == path and current.is_symlink() and current.is_file():
                continue
            raise ValueError(f"SDK path contains a link or reparse point: {relative}")
    return path


def require_file(root: Path, relative: str) -> None:
    path = contained_path(root, relative, allow_file_links=True)
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"SDK file missing or empty: {relative}")


def require_directory(root: Path, relative: str) -> None:
    if not contained_path(root, relative).is_dir():
        raise ValueError(f"SDK directory missing: {relative}")


def validate_runtime(sdk: Path, relative: str, platform: str) -> None:
    runtime = contained_path(sdk, relative)
    for name in (*PLATFORMS[platform], *SCRIPT_CORE_FILES):
        require_file(runtime, name)
    require_directory(runtime, "FunkeAssets")
    for name in required_licenses(platform):
        require_file(runtime, "Licenses/" + name)


def validate_sdk(sdk: Path, *, require_runtimes: bool = True, require_binary: bool = False,
                 require_templates: bool = True) -> dict:
    require_file(sdk, MANIFEST)
    manifest = json.loads((sdk / MANIFEST).read_text(encoding="utf-8-sig"))
    if not isinstance(manifest, dict) or type(manifest.get("schemaVersion")) is not int or manifest["schemaVersion"] != 1:
        raise ValueError("Unsupported FunkeSDK schemaVersion (expected 1)")
    if not isinstance(manifest.get("engineVersion"), str) or not manifest["engineVersion"].strip():
        raise ValueError("FunkeSDK engineVersion is missing")
    platform = manifest.get("platform")
    if not isinstance(platform, str) or platform not in PLATFORMS or manifest.get("architecture") not in ("x86_64", "arm64"):
        raise ValueError("Unsupported FunkeSDK platform or architecture")
    capabilities = manifest.get("capabilities")
    if not isinstance(capabilities, dict) or any(type(capabilities.get(k)) is not bool for k in
                                               ("managedProjects", "nativeDevelopment", "sourceBuild")):
        raise ValueError("FunkeSDK capabilities must be explicit booleans")
    if not capabilities["managedProjects"] or capabilities["sourceBuild"]:
        raise ValueError("Distributed SDK must support managedProjects and must not require sourceBuild")
    if require_binary and capabilities["nativeDevelopment"]:
        raise ValueError("Binary distribution unexpectedly enables nativeDevelopment")
    runtimes = manifest.get("runtimes")
    if not isinstance(runtimes, dict) or any(k not in ("Development", "Release", "Debug") for k in runtimes):
        raise ValueError("Invalid FunkeSDK runtimes map")
    if require_runtimes and not {"Development", "Release"}.issubset(runtimes):
        raise ValueError("Standalone SDK requires Development and Release runtimes")
    runtime_paths = []
    for configuration, relative in runtimes.items():
        validate_runtime(sdk, relative, platform)
        if configuration == "Release" and any((sdk / relative / name).exists() for name in
                                               ("Tracy.dll", "libTracy.so", "libTracy.dylib")):
            raise ValueError("Release runtime contains a development profiler library")
        runtime_paths.append(relative.casefold())
    if len(set(runtime_paths)) != len(runtime_paths):
        raise ValueError("SDK runtime configurations must have distinct directories")
    variants = manifest.get("runtimeVariants", {})
    if not isinstance(variants, dict):
        raise ValueError("Invalid SDK runtimeVariants")
    for variant, configurations in variants.items():
        if not re.fullmatch(r"[A-Za-z0-9_-]+", variant) or not isinstance(configurations, dict):
            raise ValueError("Invalid SDK runtime variant")
        for configuration, relative in configurations.items():
            if configuration not in ("Development", "Release", "Debug"):
                raise ValueError("Invalid SDK runtime variant configuration")
            validate_runtime(sdk, relative, platform)
    for name in SCRIPT_CORE_FILES:
        require_file(sdk, "Funke-ScriptCore/" + name)
    if require_templates:
        require_directory(sdk, "templates")
    require_file(sdk, "premake5.exe" if platform == "windows" else "premake5")
    for name in ("project-packages.lua", "package-loader.lua", "fix_csharp_platforms.lua", "architecture.lua", "compat.lua"):
        require_file(sdk, "premake/" + name)
    for name in required_licenses(platform):
        require_file(sdk, "Licenses/" + name)
    for forbidden in ("Funke.sln", "premake5.lua", "Funke-ScriptCore/Funke-ScriptCore.csproj", "Funke-ScriptCore/Source"):
        if os.path.lexists(sdk / forbidden):
            raise ValueError(f"Source-build dependency present in SDK: {forbidden}")
    if capabilities["nativeDevelopment"]:
        for relative in NATIVE_HEADERS:
            require_file(sdk, relative)
        libraries = list((sdk / "bin").glob(f"*-{platform}-{manifest['architecture']}/Funke-Engine"))
        library_name = "Funke-Engine.lib" if platform == "windows" else PLATFORMS[platform][1]
        if not libraries:
            raise ValueError("Native SDK has no engine link libraries")
        for directory in libraries:
            require_file(sdk, (directory / library_name).relative_to(sdk).as_posix())
            require_file(sdk, (directory / PLATFORMS[platform][1]).relative_to(sdk).as_posix())
    elif any(os.path.lexists(sdk / name) for name in NATIVE_ROOTS):
        raise ValueError("Binary SDK contains native headers, libraries or source layout")
    for owned in ("Funke-Engine", "External"):
        for path in (sdk / owned).rglob("*"):
            contained_path(sdk, path.relative_to(sdk).as_posix())
            if path.is_file() and path.suffix.lower() not in HEADER_SUFFIXES:
                raise ValueError(f"Native SDK contains a non-header source file: {path.relative_to(sdk)}")
    return manifest


def write_manifest(sdk: Path, version: str, platform: str, architecture: str, native: bool, runtimes: dict) -> dict:
    manifest = {"schemaVersion": 1, "engineVersion": version, "platform": platform, "architecture": architecture,
                "capabilities": {"managedProjects": True, "nativeDevelopment": native, "sourceBuild": False},
                "runtimes": runtimes}
    (sdk / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def verify_bundle(bundle: Path, *, require_binary: bool = False) -> dict:
    manifest = validate_sdk(bundle / "EngineSDK", require_binary=require_binary)
    platform = manifest["platform"]
    suffix = ".exe" if platform == "windows" else ""
    for name in (f"Funke-Launcher{suffix}", f"Funke-Editor{suffix}", *PLATFORMS[platform], *SCRIPT_CORE_FILES,
                 f"Funke-PackageTool{suffix}", "Funke-PackageTool.dll", "Funke-PackageTool.deps.json",
                 "Funke-PackageTool.runtimeconfig.json", "BuildMetadata.json"):
        require_file(bundle, name)
    require_directory(bundle, "FunkeAssets/Fonts")
    metadata = json.loads((bundle / "BuildMetadata.json").read_text(encoding="utf-8-sig"))
    if metadata.get("version") != manifest["engineVersion"]:
        raise ValueError("Bundle and SDK engine versions do not match")
    for path in bundle.rglob("*"):
        contained_path(bundle, path.relative_to(bundle).as_posix(), allow_file_links=True)
        if path.name == ".git" or path.suffix.lower() == ".pdb":
            raise ValueError(f"Private build data in distribution: {path.relative_to(bundle)}")
    return manifest


def archive_bundle(bundle: Path, archive: Path, *, prefix: str = "", include_native: bool = False) -> None:
    manifest = verify_bundle(bundle)
    if include_native and not manifest["capabilities"]["nativeDevelopment"]:
        raise ValueError("Input bundle has no optional native SDK")
    manifest["capabilities"]["nativeDevelopment"] = include_native
    manifest_bytes = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    if archive.resolve().is_relative_to(bundle.resolve()):
        raise ValueError("Archive must be outside the input bundle")
    def entries():
        for path in sorted(bundle.rglob("*")):
            relative = path.relative_to(bundle)
            if not include_native and len(relative.parts) > 1 and relative.parts[0] == "EngineSDK" and relative.parts[1] in NATIVE_ROOTS:
                continue
            if path.name in ("imgui.ini", "funke-project.json") or path.suffix.lower() in (".log", ".pdb"):
                continue
            if path.is_file():
                yield path, relative.as_posix()
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for path, relative in entries():
                if relative == "EngineSDK/" + MANIFEST:
                    output.writestr(prefix + relative, manifest_bytes)
                else:
                    output.write(path, prefix + relative)
    elif archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "w:gz", dereference=True) as output:
            for path, relative in entries():
                info = output.gettarinfo(str(path), arcname=prefix + relative)
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                if relative == "EngineSDK/" + MANIFEST:
                    info.size = len(manifest_bytes)
                    output.addfile(info, io.BytesIO(manifest_bytes))
                else:
                    with path.open("rb") as stream:
                        output.addfile(info, stream)
    else:
        raise ValueError("SDK archive must use .zip or .tar.gz")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--binary-only", action="store_true")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--include-native-sdk", action="store_true")
    args = parser.parse_args()
    try:
        if args.archive:
            archive_bundle(args.bundle, args.archive, include_native=args.include_native_sdk)
        else:
            verify_bundle(args.bundle, require_binary=args.binary_only)
    except (OSError, ValueError) as error:
        parser.exit(1, f"[verify-standalone] {error}\n")
    print("[verify-standalone] SDK manifest and standalone payload are complete.")


if __name__ == "__main__":
    main()
