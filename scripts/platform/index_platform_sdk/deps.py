"""Cached per-target builds of the engine's native dependencies (SDL3, Dawn).

Platform packages pass their toolchain as CMake arguments; the engine owns which sources and
options those libraries are built with, so every platform gets the same SDL3 / Dawn revision as
the desktop engine.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .cache import platform_cache
from .protocol import BuilderError, emit_log
from .tools import find_cmake, run


@dataclass
class CMakeToolchain:
    """How a platform compiles: generator plus the CMake arguments that select compiler / sysroot."""
    key: str                       # cache folder, e.g. "Android/arm64-v8a"
    configure_args: list[str]
    generator: str = "Ninja"
    make_program: Path | None = None
    configure_wrapper: list[str] | None = None   # e.g. ["emcmake"] for Emscripten
    env: dict[str, str] | None = None

    def configure_command(self, cmake: Path, source: Path, build: Path, extra: list[str]) -> list[str]:
        cmd = [str(cmake), "-S", str(source), "-B", str(build), "-G", self.generator]
        if self.make_program:
            cmd.append(f"-DCMAKE_MAKE_PROGRAM={self.make_program}")
        cmd += self.configure_args + extra
        return (self.configure_wrapper or []) + cmd


def _git_revision(path: Path) -> str:
    try:
        return subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def _stamp(*parts: object) -> str:
    return hashlib.sha256(json.dumps([str(p) for p in parts]).encode()).hexdigest()[:16]


def _cmake_build(toolchain: CMakeToolchain, engine_root: Path, name: str, source: Path, extra: list[str],
                 targets: list[str], install: bool, revision: str) -> Path:
    out = platform_cache(*toolchain.key.split("/"), f"{name}-{revision[:12]}")
    stamp_file = out / ".index-stamp"
    stamp = _stamp(revision, toolchain.configure_args, extra, targets, install)
    if stamp_file.is_file() and stamp_file.read_text() == stamp:
        return out
    emit_log(f"Building {name} for {toolchain.key} (first build for this target takes a while)...")
    cmake = find_cmake(engine_root)
    build_dir = out / "build"
    run(toolchain.configure_command(cmake, source, build_dir, extra), env=toolchain.env, label=f"{name} configure")
    build_cmd = [str(cmake), "--build", str(build_dir), "--config", "Release", "--parallel"]
    for target in targets:
        build_cmd += ["--target", target]
    run(build_cmd, env=toolchain.env, label=f"{name} build")
    if install:
        run([str(cmake), "--install", str(build_dir), "--config", "Release", "--prefix", str(out / "install")],
            env=toolchain.env, label=f"{name} install")
    stamp_file.write_text(stamp)
    return out


def build_sdl3(engine_root: Path, toolchain: CMakeToolchain, shared: bool) -> Path:
    """Returns the install prefix (include/, lib/) of SDL3 for this target."""
    source = engine_root / "External" / "SDL"
    if not (source / "CMakeLists.txt").is_file():
        raise BuilderError("External/SDL is missing; run `git submodule update --init External/SDL`.")
    extra = [
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DSDL_SHARED={'ON' if shared else 'OFF'}",
        f"-DSDL_STATIC={'OFF' if shared else 'ON'}",
        "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF",
        "-DSDL_RENDER=OFF", "-DSDL_GPU=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
    ]
    out = _cmake_build(toolchain, engine_root, "sdl3", source, extra, [], True, _git_revision(source))
    return out / "install"


@dataclass
class DawnBuild:
    library: Path
    include_dirs: list[Path]


def build_dawn(engine_root: Path, toolchain: CMakeToolchain, extra_args: list[str] | None = None) -> DawnBuild:
    """Monolithic static webgpu_dawn for native targets (Android, iOS)."""
    source = engine_root / "External" / "dawn"
    if not (source / "CMakeLists.txt").is_file():
        raise BuilderError("External/dawn is missing; run scripts/dawn/SetupDawn first.")
    revision_file = engine_root / "scripts" / "dawn" / "dawn-revision.txt"
    revision = revision_file.read_text().split()[0] if revision_file.is_file() else _git_revision(source)
    extra = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC", "-DDAWN_ENABLE_INSTALL=OFF",
        "-DDAWN_BUILD_SAMPLES=OFF", "-DDAWN_BUILD_TESTS=OFF", "-DDAWN_USE_GLFW=OFF",
        "-DDAWN_USE_X11=OFF", "-DDAWN_USE_WAYLAND=OFF",
        "-DTINT_BUILD_TESTS=OFF", "-DTINT_BUILD_CMD_TOOLS=OFF", "-DTINT_BUILD_SPV_READER=OFF",
        "-DDAWN_ENABLE_SPIRV_VALIDATION=OFF",
    ] + (extra_args or [])
    out = _cmake_build(toolchain, engine_root, "dawn", source, extra, ["webgpu_dawn"], False, revision)
    build_dir = out / "build"
    candidates = list(build_dir.glob("src/dawn/native/**/libwebgpu_dawn.a")) + list(build_dir.glob("src/dawn/native/**/webgpu_dawn.lib"))
    if not candidates:
        raise BuilderError(f"Dawn built but libwebgpu_dawn was not found under {build_dir}.")
    return DawnBuild(candidates[0], [source / "include", source / "include" / "dawn", build_dir / "gen" / "include"])
