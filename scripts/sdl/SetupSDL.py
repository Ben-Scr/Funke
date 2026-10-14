#!/usr/bin/env python3
"""Build SDL3 (External/SDL submodule) with CMake for the engine's target platforms.

Output layout (git-ignored):
    External/SDL-build/<target>/build    CMake build tree
    External/SDL-build/<target>/install  headers, libs, shared library

Desktop targets build SDL3 as a shared library so engine.dll and the consumer
executables (ImGui's SDL3 backend) share one copy of SDL's global state.
iOS and Web build a static library because those platforms link everything
into the app binary.

Usage:
    python scripts/sdl/SetupSDL.py                       # host platform, Release
    python scripts/sdl/SetupSDL.py --target android --android-abi arm64-v8a
    python scripts/sdl/SetupSDL.py --target ios
    python scripts/sdl/SetupSDL.py --target web          # needs emcmake on PATH
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SDL_SOURCE = REPO_ROOT / "External" / "SDL"
SDL_OUTPUT_ROOT = REPO_ROOT / "External" / "SDL-build"

TARGETS = ("windows", "linux", "macos", "android", "ios", "web")


def host_target() -> str:
    system = platform.system()
    if system == "Windows":
        return "windows"
    if system == "Darwin":
        return "macos"
    return "linux"


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    return machine or "x64"


def target_dir_name(target: str, arch: str) -> str:
    return f"{target}-{arch}"


def find_cmake() -> str | None:
    vendored = REPO_ROOT / "vendor" / "cmake" / "bin" / ("cmake.exe" if os.name == "nt" else "cmake")
    if vendored.is_file():
        return str(vendored)
    return shutil.which("cmake")


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("[SetupSDL] " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def shared_library_path(target: str, install_dir: Path) -> Path | None:
    if target == "windows":
        return install_dir / "bin" / "SDL3.dll"
    if target == "macos":
        return install_dir / "lib" / "libSDL3.dylib"
    if target in ("linux", "android"):
        return install_dir / "lib" / "libSDL3.so"
    return None


def is_built(target: str, arch: str) -> bool:
    install_dir = SDL_OUTPUT_ROOT / target_dir_name(target, arch) / "install"
    shared = shared_library_path(target, install_dir)
    if shared is not None:
        return shared.is_file()
    return (install_dir / "lib" / "libSDL3.a").is_file()


def configure_args(args: argparse.Namespace, build_dir: Path, install_dir: Path) -> list[str]:
    shared = args.target in ("windows", "linux", "macos", "android")
    cmake_args = [
        "-S", str(SDL_SOURCE),
        "-B", str(build_dir),
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        f"-DSDL_SHARED={'ON' if shared else 'OFF'}",
        f"-DSDL_STATIC={'OFF' if shared else 'ON'}",
        "-DSDL_TEST_LIBRARY=OFF",
        "-DSDL_TESTS=OFF",
        "-DSDL_EXAMPLES=OFF",
        "-DSDL_INSTALL=ON",
        # The engine renders through WebGPU/Dawn; SDL's own renderers and GPU API are unused.
        "-DSDL_RENDER=OFF",
        "-DSDL_GPU=OFF",
        "-DSDL_VULKAN=ON",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
    ]

    if args.target == "windows":
        cmake_args += ["-G", args.generator or "Visual Studio 17 2022", "-A", "ARM64" if args.arch == "arm64" else "x64"]
    elif args.target == "macos":
        cmake_args += [
            f"-DCMAKE_OSX_ARCHITECTURES={'arm64;x86_64' if args.arch == 'universal' else args.arch.replace('x64', 'x86_64')}",
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
        ]
        if args.generator:
            cmake_args += ["-G", args.generator]
    elif args.target == "ios":
        cmake_args += [
            "-G", args.generator or "Xcode",
            "-DCMAKE_SYSTEM_NAME=iOS",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0",
        ]
    elif args.target == "android":
        ndk = args.android_ndk or os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
        if not ndk:
            raise SystemExit("[SetupSDL] Android needs --android-ndk or ANDROID_NDK_HOME/ANDROID_NDK_ROOT.")
        cmake_args += [
            f"-DCMAKE_TOOLCHAIN_FILE={Path(ndk) / 'build' / 'cmake' / 'android.toolchain.cmake'}",
            f"-DANDROID_ABI={args.android_abi}",
            f"-DANDROID_PLATFORM=android-{args.android_api}",
        ]
        cmake_args += ["-G", args.generator or "Ninja"]
    elif args.target == "web":
        # emcmake injects the Emscripten toolchain; SDL selects its Emscripten backends itself.
        cmake_args += ["-G", args.generator or "Ninja"]
    elif args.generator:
        cmake_args += ["-G", args.generator]

    if args.target not in ("windows", "ios"):
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={args.config}")
    return cmake_args


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build SDL3 for the Index engine.")
    parser.add_argument("--target", choices=TARGETS, default=host_target())
    parser.add_argument("--arch", default=None, help="x64, arm64 (macOS also: universal). Defaults to the host arch.")
    parser.add_argument("--config", default="Release", choices=("Release", "RelWithDebInfo", "Debug"))
    parser.add_argument("--generator", default=None, help="Override the CMake generator.")
    parser.add_argument("--android-ndk", default=None)
    parser.add_argument("--android-abi", default="arm64-v8a")
    parser.add_argument("--android-api", default="24")
    parser.add_argument("--if-missing", action="store_true", help="Skip when the target is already built.")
    parser.add_argument("--clean", action="store_true", help="Delete the target's build tree first.")
    args = parser.parse_args()

    if args.arch is None:
        if args.target == "android":
            args.arch = args.android_abi
        elif args.target == "web":
            args.arch = "wasm32"
        elif args.target == "ios":
            args.arch = "arm64"
        else:
            args.arch = host_arch()
    return args


def main() -> int:
    args = parse_args()

    if not (SDL_SOURCE / "CMakeLists.txt").is_file():
        print("[SetupSDL] External/SDL is missing. Run: git submodule update --init External/SDL", file=sys.stderr)
        return 1

    if args.if_missing and is_built(args.target, args.arch):
        print(f"[SetupSDL] SDL3 already built for {target_dir_name(args.target, args.arch)}.")
        return 0

    cmake = find_cmake()
    if not cmake:
        print("[SetupSDL] CMake not found (vendor/cmake or PATH).", file=sys.stderr)
        return 1

    out_dir = SDL_OUTPUT_ROOT / target_dir_name(args.target, args.arch)
    build_dir = out_dir / "build"
    install_dir = out_dir / "install"
    if args.clean and out_dir.exists():
        shutil.rmtree(out_dir)

    configure = [cmake] + configure_args(args, build_dir, install_dir)
    if args.target == "web":
        emcmake = shutil.which("emcmake")
        if not emcmake:
            print("[SetupSDL] Web builds need the Emscripten SDK (emcmake on PATH).", file=sys.stderr)
            return 1
        configure = [emcmake] + configure

    try:
        run(configure)
        run([cmake, "--build", str(build_dir), "--config", args.config, "--parallel"])
        run([cmake, "--install", str(build_dir), "--config", args.config])
    except subprocess.CalledProcessError as error:
        print(f"[SetupSDL] Failed with exit code {error.returncode}.", file=sys.stderr)
        return error.returncode or 1

    print(f"[SetupSDL] SDL3 installed to {install_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
