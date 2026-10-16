#!/usr/bin/env python3
"""Funke.Platform.iOS builder (Funke Platform Builder protocol v1). Runs on macOS only.

SDL3, Dawn (Metal) and the engine build as static libraries for arm64 iOS; the app target is
generated as an Xcode project that embeds the game content and the managed assemblies. With
`developmentTeam` set in the build options the builder also produces a signed device .app;
otherwise it stops after generating the project, which opens in Xcode for signing and archiving.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
RID = "ios-arm64"
MONO_PACKAGE = "Funke.ScriptRuntime.Mono"
DEPLOYMENT_TARGET = "15.0"


def _engine_root_from_argv() -> Path | None:
    argv = sys.argv
    for i, arg in enumerate(argv):
        if arg == "--engine-root" and i + 1 < len(argv):
            return Path(argv[i + 1])
        if arg == "--request" and i + 1 < len(argv):
            try:
                return Path(json.loads(Path(argv[i + 1]).read_text(encoding="utf-8"))["engineRoot"])
            except (OSError, KeyError, ValueError):
                return None
    return None


def _bootstrap_sdk_path(engine_root: Path | None) -> None:
    roots = [engine_root] if engine_root else []
    roots += [Path(os.environ["FUNKE_ROOT"])] if os.environ.get("FUNKE_ROOT") else []
    roots.append(PACKAGE_ROOT.parents[1])
    for root in roots:
        sdk = root / "scripts" / "platform"
        if (sdk / "funke_platform_sdk").is_dir():
            sys.path.insert(0, str(sdk))
            return


_bootstrap_sdk_path(_engine_root_from_argv())

from funke_platform_sdk import (  # noqa: E402
    BuildRequest, BuilderError, emit_log, emit_progress, find_cmake, find_ninja, platform_cache, run, run_builder,
)
from funke_platform_sdk.deps import CMakeToolchain, build_dawn, build_sdl3  # noqa: E402
from funke_platform_sdk.orientation import IOS_INTERFACE_ORIENTATIONS, project_orientation  # noqa: E402
from funke_platform_sdk.tools import set_log_file  # noqa: E402


def _xcode_version() -> str:
    try:
        out = subprocess.run(["xcodebuild", "-version"], capture_output=True, text=True, timeout=60).stdout
    except OSError:
        return ""
    match = re.search(r"Xcode (\d+(?:\.\d+)*)", out)
    return match.group(1) if match else ""


def describe() -> dict:
    return {
        "platform": "iOS",
        "displayName": "iOS (iPhone / iPad)",
        "hosts": ["macos"],
        "artifact": "xcodeproj or app",
        "architectures": ["arm64"],
        "options": {"developmentTeam": "Apple team ID for automatic signing (empty: generate the Xcode project only)",
                    "bundleId": "overrides the generated bundle identifier"},
    }


def check(engine_root: Path | None) -> dict:
    issues = []
    if platform.system() != "Darwin":
        issues.append({"severity": "error", "message": "iOS builds need a Mac.",
                       "fix": "Run the build on macOS with Xcode installed."})
    else:
        version = _xcode_version()
        if not version:
            issues.append({"severity": "error", "message": "Xcode not found.", "fix": "Install Xcode and run `xcode-select -s`."})
        elif int(version.split(".")[0]) < 15:
            issues.append({"severity": "error", "message": f"Xcode {version} is too old.", "fix": "Install Xcode 15 or newer."})
        if not find_ninja():
            issues.append({"severity": "error", "message": "Ninja not found.", "fix": "brew install ninja"})
    try:
        find_cmake(engine_root)
    except BuilderError as error:
        issues.append({"severity": "error", "message": str(error), "fix": "brew install cmake"})
    return {"ok": not issues, "issues": issues, "details": {"xcode": _xcode_version() if platform.system() == "Darwin" else ""}}


def _find_mono_package(engine_root: Path) -> Path:
    for candidate in (PACKAGE_ROOT.parent / MONO_PACKAGE, engine_root / "platforms" / MONO_PACKAGE):
        if (candidate / "funke-package.lua").is_file():
            return candidate
    raise BuilderError(f"{MONO_PACKAGE} is not installed; it is required for C# scripting on iOS.")


def _sanitize(text: str) -> str:
    part = re.sub(r"[^A-Za-z0-9-]", "", text.replace(" ", "-")).lower()
    return part or "app"


def build(request: BuildRequest) -> Path:
    if platform.system() != "Darwin":
        raise BuilderError("iOS builds need a Mac with Xcode.")
    request.output_dir.mkdir(parents=True, exist_ok=True)
    work = platform_cache("iOS", "work", hashlib.sha256(str(request.project_root.resolve()).encode()).hexdigest()[:10])
    set_log_file(work / "build.log")

    ninja = find_ninja()
    if not ninja:
        raise BuilderError("Ninja not found (brew install ninja).")
    libs_toolchain = CMakeToolchain(
        key="iOS/arm64",
        configure_args=["-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_ARCHITECTURES=arm64",
                        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={DEPLOYMENT_TARGET}", "-DCMAKE_OSX_SYSROOT=iphoneos"],
        generator="Ninja",
        make_program=ninja,
    )

    mono_package = _find_mono_package(request.engine_root)
    sys.path.insert(0, str(mono_package / "Builder"))
    import mono_runtime  # noqa: E402

    emit_progress(0.05, "SDL3 for iOS")
    sdl3 = build_sdl3(request.engine_root, libs_toolchain, shared=False)
    emit_progress(0.15, "Dawn (Metal) for iOS")
    dawn = build_dawn(request.engine_root, libs_toolchain)
    emit_progress(0.30, "Mono runtime pack")
    mono_pack = mono_runtime.fetch_runtime_pack(RID)

    emit_progress(0.35, "Staging managed assemblies")
    managed = work / "managed"
    shutil.rmtree(managed, ignore_errors=True)
    managed.mkdir(parents=True)
    for assembly in mono_pack.framework_assemblies():
        shutil.copy2(assembly, managed / assembly.name)
    for assembly in request.content_dir.glob("Funke-ScriptCore.dll"):
        shutil.copy2(assembly, managed / assembly.name)

    emit_progress(0.40, "Generating the Xcode project")
    bundle_id = str(request.options.get("bundleId") or request.app_id
                    or f"com.{_sanitize(request.company or 'funke')}.{_sanitize(request.project_name)}")
    team = str(request.options.get("developmentTeam") or "")
    project_dir = request.output_dir / f"{re.sub(r'[^A-Za-z0-9._-]', '_', request.app_name)}-Xcode"
    cmake = find_cmake(request.engine_root)
    args = [
        "-G", "Xcode",
        "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_ARCHITECTURES=arm64", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={DEPLOYMENT_TARGET}",
        f"-DFUNKE_ROOT={request.engine_root.as_posix()}",
        f"-DFUNKE_MONO_PACKAGE={mono_package.as_posix()}",
        f"-DFUNKE_MONO_PACK_NATIVE={mono_pack.native_dir.as_posix()}",
        f"-DFUNKE_SDL3_DIR={sdl3.as_posix()}",
        f"-DFUNKE_DAWN_LIBRARY={dawn.library.as_posix()}",
        "-DFUNKE_DAWN_INCLUDE_DIRS=" + ";".join(p.as_posix() for p in dawn.include_dirs),
        f"-DFUNKE_CONTENT_DIR={request.content_dir.as_posix()}",
        f"-DFUNKE_MANAGED_DIR={managed.as_posix()}",
        f"-DFUNKE_BUNDLE_ID={bundle_id}",
        f"-DFUNKE_APP_NAME={request.app_name}",
        f"-DFUNKE_EXECUTABLE_NAME={re.sub(r'[^A-Za-z0-9_]', '', request.project_name) or 'FunkeGame'}",
        f"-DFUNKE_VERSION={request.version}",
        f"-DFUNKE_BUILD_NUMBER={request.options.get('buildNumber') or 1}",
        f"-DFUNKE_DEVELOPMENT_TEAM={team}",
        f"-DFUNKE_CONFIGURATION={request.configuration}",
        f"-DFUNKE_ENTITY_BITS={request.entity_bits}",
        *request.cmake_module_args(),
        "-DFUNKE_IOS_ORIENTATIONS=" + "".join(
            f"<string>{o}</string>" for o in IOS_INTERFACE_ORIENTATIONS[project_orientation(request.content_dir)]),
    ]
    run([str(cmake), "-S", str(PACKAGE_ROOT / "Native"), "-B", str(project_dir)] + args, label="CMake (Xcode project)")

    if not team:
        emit_log("No developmentTeam set: open the generated Xcode project to sign, run and archive.", "warn")
        return project_dir

    emit_progress(0.50, "Building the app (xcodebuild)")
    configuration = "Release" if request.is_release else "RelWithDebInfo"
    run(["xcodebuild", "-project", str(next(project_dir.glob("*.xcodeproj"))), "-scheme", "FunkeGame",
         "-configuration", configuration, "-sdk", "iphoneos", "-allowProvisioningUpdates",
         f"DEVELOPMENT_TEAM={team}", "build"], label="xcodebuild")
    apps = list(project_dir.rglob(f"{configuration}-iphoneos/*.app"))
    if not apps:
        raise BuilderError("xcodebuild finished without producing an .app")
    target = request.output_dir / apps[0].name
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(apps[0], target, symlinks=True)
    emit_log(f"iOS app: {target} (Xcode project: {project_dir})")
    return target


if __name__ == "__main__":
    sys.exit(run_builder(describe, check, build))
