#!/usr/bin/env python3
"""Funke.Platform.Web builder (Funke Platform Builder protocol v1).

Emscripten builds the engine, SDL3 and the runtime to WebAssembly; WebGPU comes from Dawn's
emdawnwebgpu port built from the engine's own Dawn checkout, so the browser API matches the
webgpu_cpp.h the engine is written against.

Games without C# link directly with emsdk: <output>/<Game>/index.html plus .js / .wasm / .data, the
content preloaded at /game. Games with C# scripts need Funke.ScriptRuntime.Mono and the .NET 11 SDK
with the wasm-tools workload: the engine is compiled with that SDK's Emscripten into FunkeWeb.a and
`dotnet publish` links it with the Mono runtime (see Funke.ScriptRuntime.Mono/Builder/dotnet_wasm.py).
"""

from __future__ import annotations

import glob
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
MIN_EMSCRIPTEN = (4, 0, 3)  # emdawnwebgpu requirement


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
    BuildRequest, BuilderError, emit_log, emit_progress, find_cmake, platform_cache, prepare_native_packages, run,
    run_builder, which,
)
from funke_platform_sdk.deps import CMakeToolchain, build_sdl3  # noqa: E402
from funke_platform_sdk.tools import set_log_file  # noqa: E402

IS_WINDOWS = platform.system() == "Windows"
MONO_PACKAGE = "Funke.ScriptRuntime.Mono"


def _script(name: str) -> str:
    return f"{name}.bat" if IS_WINDOWS else name


def find_emsdk(engine_root: Path | None, override: str | None = None) -> Path | None:
    candidates = [override, os.environ.get("EMSDK")]
    if engine_root:
        candidates.append(str(engine_root / "vendor" / "emsdk"))
    candidates.append(str(Path.home() / "emsdk"))
    for candidate in candidates:
        if candidate and (Path(candidate) / "upstream" / "emscripten" / _script("emcc")).is_file():
            return Path(candidate)
    return None


def emscripten_version(emsdk: Path) -> tuple[int, int, int]:
    version_file = emsdk / "upstream" / "emscripten" / "emscripten-version.txt"
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", version_file.read_text() if version_file.is_file() else "")
    return tuple(int(x) for x in match.groups()) if match else (0, 0, 0)


def find_ninja() -> Path | None:
    found = which("ninja")
    if found:
        return found
    patterns = []
    if IS_WINDOWS:
        program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
        patterns += [rf"{program_files}\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
                     os.path.join(os.environ.get("LOCALAPPDATA", ""), r"Android\Sdk\cmake\*\bin\ninja.exe"),
                     rf"{program_files}\Unity\Hub\Editor\*\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\cmake\*\bin\ninja.exe"]
    else:
        patterns += [str(Path.home() / "Library/Android/sdk/cmake/*/bin/ninja"), str(Path.home() / "Android/Sdk/cmake/*/bin/ninja")]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern), reverse=True)
        if matches:
            return Path(matches[0])
    return None


def _find_mono_package(engine_root: Path | None) -> Path | None:
    candidates = [PACKAGE_ROOT.parent / MONO_PACKAGE]
    if engine_root:
        candidates.append(engine_root / "platforms" / MONO_PACKAGE)
    return next((c for c in candidates if (c / "funke-package.lua").is_file()), None)


def _import_dotnet_wasm(engine_root: Path | None):
    mono_package = _find_mono_package(engine_root)
    if not mono_package:
        return None, None
    if str(mono_package / "Builder") not in sys.path:
        sys.path.insert(0, str(mono_package / "Builder"))
    import dotnet_wasm  # noqa: E402
    return mono_package, dotnet_wasm


def _game_assemblies(content_dir: Path) -> list[Path]:
    """The game's compiled scripts and their dependencies (bin/<configuration>/*.dll)."""
    return sorted(p for p in content_dir.glob("bin/*/*.dll") if p.name != "Funke-ScriptCore.dll")


def _wants_debug_symbols(request: BuildRequest) -> bool:
    return request.option("debugSymbols").lower() in ("1", "true", "yes")


def _drop_stale_pch(build_dir: Path, cache_dir: Path) -> None:
    """Precompiled headers record the sysroot headers' timestamps, which CMake does not track; after
    Emscripten reinstalls its sysroot every PCH would fail with "has been modified"."""
    stamp = cache_dir / "sysroot_install.stamp"
    current = str(stamp.stat().st_mtime_ns) if stamp.is_file() else ""
    record = build_dir / ".funke-sysroot-stamp"
    if record.is_file() and record.read_text() != current:
        for pch in build_dir.rglob("cmake_pch*.pch"):
            pch.unlink(missing_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)
    record.write_text(current)


def describe() -> dict:
    return {
        "platform": "Web",
        "displayName": "Web (WebAssembly + WebGPU)",
        "hosts": ["windows", "macos", "linux"],
        "artifact": "folder",
        "architectures": ["wasm32"],
        "options": {"emsdk": "path to an emsdk checkout (defaults to $EMSDK or <engine>/vendor/emsdk)",
                    "dotnetSdk": "path to a .NET 11+ SDK with wasm-tools, for C# games (defaults to <engine>/vendor/dotnet11)",
                    "debugSymbols": "true keeps DWARF debug info for browser debugging (makes the .wasm about 10x larger)"},
    }


def check(engine_root: Path | None) -> dict:
    issues = []
    emsdk = find_emsdk(engine_root)
    if not emsdk:
        issues.append({"severity": "error", "message": "Emscripten SDK not found.",
                       "fix": "Install emsdk (https://emscripten.org) and set EMSDK, or place it at <engine>/vendor/emsdk."})
    elif emscripten_version(emsdk) < MIN_EMSCRIPTEN:
        issues.append({"severity": "error", "message": f"Emscripten {'.'.join(map(str, emscripten_version(emsdk)))} is too old.",
                       "fix": "Install Emscripten 4.0.3 or newer (`emsdk install latest`)."})
    if not find_ninja():
        issues.append({"severity": "error", "message": "Ninja not found.", "fix": "Install Ninja and put it on PATH."})
    try:
        find_cmake(engine_root)
    except BuilderError as error:
        issues.append({"severity": "error", "message": str(error), "fix": "Install CMake 3.22 or newer."})
    _, dotnet_wasm = _import_dotnet_wasm(engine_root)
    dotnet_sdk = dotnet_wasm.find_dotnet_wasm_sdk(engine_root) if dotnet_wasm else None
    if not dotnet_wasm:
        issues.append({"severity": "warning", "message": f"{MONO_PACKAGE} is not installed; games with C# scripts cannot be built.",
                       "fix": f"Install the {MONO_PACKAGE} package."})
    elif not dotnet_sdk:
        issues.append({"severity": "warning", "message": "No .NET 11 SDK with the wasm-tools workload; games with C# scripts cannot be built.",
                       "fix": dotnet_wasm.missing_sdk_fix()})
    if engine_root and not (engine_root / "External" / "dawn" / "src" / "emdawnwebgpu").is_dir():
        issues.append({"severity": "error", "message": "Dawn sources (emdawnwebgpu) are missing.",
                       "fix": "Run scripts/dawn/SetupDawn to fetch Dawn."})
    return {"ok": not any(i["severity"] == "error" for i in issues), "issues": issues,
            "details": {"emsdk": str(emsdk or ""), "emscripten": ".".join(map(str, emscripten_version(emsdk))) if emsdk else "",
                        "dotnetSdk": str(dotnet_sdk.root) if dotnet_sdk else ""}}


def _dotnet_toolchain(sdk, ninja: Path) -> CMakeToolchain:
    """The .NET SDK's own Emscripten: objects must match the runtime the relink links against."""
    return CMakeToolchain(
        key=f"Web/dotnet-emscripten-{sdk.emscripten_version}",
        configure_args=[],
        generator="Ninja",
        make_program=ninja,
        configure_wrapper=[str(sdk.emcmake())],
        env=sdk.emscripten_env(),
    )


def _toolchain(emsdk: Path, ninja: Path) -> CMakeToolchain:
    version = ".".join(map(str, emscripten_version(emsdk)))
    env = {"EMSDK": str(emsdk), "EM_CONFIG": str(emsdk / ".emscripten")}
    return CMakeToolchain(
        key=f"Web/emscripten-{version}",
        configure_args=[],
        generator="Ninja",
        make_program=ninja,
        configure_wrapper=[str(emsdk / "upstream" / "emscripten" / _script("emcmake"))],
        env=env,
    )


def _build_emdawnwebgpu(engine_root: Path, toolchain: CMakeToolchain) -> Path:
    """Dawn's emdawnwebgpu_pkg (port file + webgpu headers) from the engine's Dawn checkout."""
    source = engine_root / "External" / "dawn"
    revision = (engine_root / "scripts" / "dawn" / "dawn-revision.txt").read_text().split()[0]
    out = platform_cache(*toolchain.key.split("/"), f"emdawnwebgpu-{revision[:12]}")
    pkg = out / "build" / "emdawnwebgpu_pkg"
    if (pkg / "emdawnwebgpu.port.py").is_file():
        return pkg
    emit_log("Building emdawnwebgpu (first Web build only)...")
    cmake = find_cmake(engine_root)
    run(toolchain.configure_command(cmake, source, out / "build", [
        "-DCMAKE_BUILD_TYPE=Release", "-DDAWN_BUILD_SAMPLES=OFF", "-DDAWN_BUILD_TESTS=OFF",
        "-DTINT_BUILD_TESTS=OFF", "-DTINT_BUILD_CMD_TOOLS=OFF", "-DDAWN_ENABLE_INSTALL=OFF",
    ]), env=toolchain.env, label="emdawnwebgpu configure")
    run([str(cmake), "--build", str(out / "build"), "--target", "emdawnwebgpu_pkg"], env=toolchain.env,
        label="emdawnwebgpu build")
    if not (pkg / "emdawnwebgpu.port.py").is_file():
        raise BuilderError(f"emdawnwebgpu_pkg was not produced under {out / 'build'}")
    return pkg


def build(request: BuildRequest) -> Path:
    request.output_dir.mkdir(parents=True, exist_ok=True)
    work = platform_cache("Web", "work", hashlib.sha256(str(request.project_root.resolve()).encode()).hexdigest()[:10])
    set_log_file(work / "build.log")
    emit_log(f"Build log: {work / 'build.log'}")

    ninja = find_ninja()
    if not ninja:
        raise BuilderError("Ninja not found.")
    assemblies = _game_assemblies(request.content_dir)
    managed = bool(assemblies)
    if managed:
        emit_progress(0.02, "Checking the .NET WebAssembly SDK")
        mono_package, dotnet_wasm = _import_dotnet_wasm(request.engine_root)
        if not dotnet_wasm:
            raise BuilderError(f"This game has C# scripts; the Web build needs the {MONO_PACKAGE} package.")
        sdk = dotnet_wasm.find_dotnet_wasm_sdk(request.engine_root, request.options.get("dotnetSdk"))
        if not sdk:
            raise BuilderError("This game has C# scripts. " + dotnet_wasm.missing_sdk_fix())
        toolchain = _dotnet_toolchain(sdk, ninja)
        emit_log(f".NET SDK {sdk.sdk_version} at {sdk.root} (Emscripten {sdk.emscripten_version})")
    else:
        emit_progress(0.02, "Checking the Emscripten SDK")
        emsdk = find_emsdk(request.engine_root, request.options.get("emsdk"))
        if not emsdk or emscripten_version(emsdk) < MIN_EMSCRIPTEN:
            raise BuilderError("Emscripten SDK 4.0.3+ not found (set EMSDK or install it to <engine>/vendor/emsdk).")
        toolchain = _toolchain(emsdk, ninja)
        emit_log(f"Emscripten {'.'.join(map(str, emscripten_version(emsdk)))} at {emsdk}")

    emit_progress(0.05, "SDL3 for the Web")
    sdl3 = build_sdl3(request.engine_root, toolchain, shared=False)
    emit_progress(0.15, "WebGPU (emdawnwebgpu)")
    pkg = _build_emdawnwebgpu(request.engine_root, toolchain)

    emit_progress(0.25, "Compiling the engine to WebAssembly")
    cmake = find_cmake(request.engine_root)
    engine_key = hashlib.sha256(str(request.engine_root.resolve()).encode()).hexdigest()[:10]
    flavor = "managed" if managed else "player"
    build_dir = platform_cache(*toolchain.key.split("/"), f"{flavor}-{engine_key}-{request.configuration}-{request.modules_key}")
    includes = [pkg / "webgpu" / "include", pkg / "webgpu_cpp" / "include"]
    port = pkg / "emdawnwebgpu.port.py"
    # The .NET relink builds the P/Invoke table from the package archives, so no symbol table here.
    native_packages = prepare_native_packages(request, work / "native-packages", symbol_table=False)
    args = [
        f"-DFUNKE_NATIVE_PACKAGES={native_packages.json_path.as_posix()}",
        f"-DFUNKE_ROOT={request.engine_root.as_posix()}",
        f"-DFUNKE_SDL3_DIR={sdl3.as_posix()}",
        f"-DFUNKE_EMDAWNWEBGPU_PORT={port.as_posix()}",
        "-DFUNKE_WEBGPU_INCLUDE_DIRS=" + ";".join(p.as_posix() for p in includes if p.is_dir()),
        f"-DFUNKE_CONFIGURATION={request.configuration}",
        f"-DFUNKE_ENTITY_BITS={request.entity_bits}",
        *request.cmake_module_args(),
        # DWARF makes the wasm 10x larger; function names for stack traces survive without it.
        "-DCMAKE_BUILD_TYPE=" + ("RelWithDebInfo" if _wants_debug_symbols(request) else "Release"),
    ]
    _drop_stale_pch(build_dir, Path(toolchain.env["EM_CACHE"]) if toolchain.env and toolchain.env.get("EM_CACHE")
                    else emsdk / "upstream" / "emscripten" / "cache")
    if managed:
        return _build_managed(request, toolchain, cmake, build_dir, args, sdl3, port, sdk, dotnet_wasm,
                              mono_package, assemblies, work, native_packages.targets)

    shell = work / "shell.html"
    shell.write_text((PACKAGE_ROOT / "Template" / "shell.html").read_text(encoding="utf-8")
                     .replace("@APP_NAME@", request.app_name.replace("<", "&lt;")), encoding="utf-8")
    args += [f"-DFUNKE_CONTENT_DIR={request.content_dir.as_posix()}", f"-DFUNKE_SHELL_FILE={shell.as_posix()}"]
    run(toolchain.configure_command(cmake, PACKAGE_ROOT / "Native", build_dir, args), env=toolchain.env,
        label="CMake configure (web)")
    # The content is preloaded at link time; relink every build so asset changes are picked up.
    for stale in build_dir.glob("index.*"):
        stale.unlink()
    run([str(cmake), "--build", str(build_dir), "--target", "index", "--parallel"], env=toolchain.env,
        label="Compile engine (WebAssembly)")

    emit_progress(0.92, "Packaging")
    site = request.output_dir / re.sub(r"[^A-Za-z0-9._-]", "_", request.app_name)
    shutil.rmtree(site, ignore_errors=True)
    site.mkdir(parents=True)
    for suffix in (".html", ".js", ".wasm", ".data"):
        produced = build_dir / f"index{suffix}"
        if not produced.is_file():
            raise BuilderError(f"Emscripten did not produce index{suffix}")
        shutil.copy2(produced, site / produced.name)
    emit_log(f"Web build: {site / 'index.html'} (serve the folder over http; file:// cannot load WebAssembly)")
    return site / "index.html"


def _build_managed(request: BuildRequest, toolchain: CMakeToolchain, cmake: Path, build_dir: Path, args: list[str],
                   sdl3: Path, port: Path, sdk, dotnet_wasm, mono_package: Path, assemblies: list[Path],
                   work: Path, package_targets: list[str]) -> Path:
    args = args + [
        "-DFUNKE_WEB_MANAGED=ON",
        f"-DFUNKE_MONO_PACKAGE_DIR={mono_package.as_posix()}",
        f"-DFUNKE_MONO_INCLUDE_DIR={(sdk.mono_runtime_native / 'include' / 'mono-2.0').as_posix()}",
    ]
    run(toolchain.configure_command(cmake, PACKAGE_ROOT / "Native", build_dir, args), env=toolchain.env,
        label="CMake configure (web, C#)")
    run([str(cmake), "--build", str(build_dir), "--target", "FunkeWeb", "--parallel"], env=toolchain.env,
        label="Compile engine (WebAssembly)")

    archives = [build_dir / "FunkeWeb.a"]
    archives += sorted(p for p in build_dir.glob("*.a") if p.name != "FunkeWeb.a")
    # Named like their [DllImport] library, so the relink resolves the package's P/Invokes from them.
    archives += [build_dir / "packages" / f"{target}.a" for target in package_targets]
    archives.append(sdl3 / "lib" / "libSDL3.a")
    missing = [a for a in archives if not a.is_file()]
    if missing:
        raise BuilderError("Engine archives missing after the build: " + ", ".join(str(m) for m in missing))

    emit_progress(0.70, "Linking with the .NET runtime")
    core = request.content_dir / "Funke-ScriptCore.dll"
    if not core.is_file():
        raise BuilderError("Funke-ScriptCore.dll is missing from the staged content.")
    site = dotnet_wasm.publish_browser_app(
        sdk, work / "dotnet",
        app_name=request.app_name,
        managed_assemblies=[core] + assemblies,
        native_archives=archives,
        ld_flags=[f"--use-port={port.as_posix()}", "-lidbfs.js"],
        template_dir=PACKAGE_ROOT / "Template" / "dotnet",
        content_dir=request.content_dir,
        strip=not _wants_debug_symbols(request))

    emit_progress(0.95, "Packaging")
    out = request.output_dir / re.sub(r"[^A-Za-z0-9._-]", "_", request.app_name)
    shutil.rmtree(out, ignore_errors=True)
    shutil.copytree(site, out)
    emit_log(f"Web build: {out / 'index.html'} (serve the folder over http; file:// cannot load WebAssembly)")
    return out / "index.html"


if __name__ == "__main__":
    sys.exit(run_builder(describe, check, build))
