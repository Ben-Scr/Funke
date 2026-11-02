#!/usr/bin/env python3
"""Validate Dawn's artifact-only cache without requiring its source checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
METADATA = "funke-artifacts.json"


def target_architecture(system: str) -> str:
    if system == "Windows":
        return "x86_64"
    machine = platform.machine().lower()
    if system == "Darwin" and subprocess.run(["sysctl", "-n", "hw.optional.arm64"],
                                               capture_output=True, text=True).stdout.strip() == "1":
        return "arm64"
    return {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)


def identity(system: str, architecture: str, configs: list[str]) -> dict:
    digest = hashlib.sha256()
    for path in sorted((ROOT / "scripts/dawn").rglob("*")):
        if path.is_file() and path.suffix in (".py", ".sh", ".bat", ".ps1", ".txt", ".patch"):
            digest.update(path.relative_to(ROOT).as_posix().encode())
            digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
    return {"schemaVersion": 1, "revision": os.environ.get("DAWN_REVISION") or
            (ROOT / "scripts/dawn/dawn-revision.txt").read_text().strip(),
            "system": system, "architecture": architecture, "configurations": sorted(configs),
            "buildInputs": digest.hexdigest()}


def artifacts(system: str, configs: list[str]) -> list[str]:
    common = ["include/webgpu/webgpu.h", "include/webgpu/webgpu_cpp.h",
              "include/webgpu/webgpu_enum_class_bitmasks.h", "build/gen/include/dawn/webgpu.h",
              "build/gen/include/dawn/webgpu_cpp.h", "build/gen/include/webgpu/webgpu_cpp_chained_struct.h",
              "LICENSE", "third_party/abseil-cpp/LICENSE"]
    if system == "Windows":
        common += ["include/dawn/native/D3DBackend.h", "include/dawn/native/DawnNative.h",
                   "include/dawn/native/dawn_native_export.h", "include/dawn/dawn_proc.h",
                   "build/gen/include/dawn/dawn_proc_table.h", "third_party/directx-shader-compiler/src/LICENSE.TXT"]
    native = "build/src/dawn/native/"
    return common + ([native + config + "/webgpu_dawn.lib" for config in configs] if system == "Windows" else
                     [native + ("libwebgpu_dawn.dylib" if system == "Darwin" else "libwebgpu_dawn.so")])


def validate(directory: Path, expected: dict) -> list[str]:
    errors = []
    try:
        metadata = json.loads((directory / METADATA).read_text())
        if metadata != expected:
            errors.append("revision, architecture, configurations or build inputs differ")
    except (OSError, ValueError):
        errors.append("missing or invalid artifact metadata")
    errors.extend(f"missing/empty artifact: {name}" for name in artifacts(expected["system"], expected["configurations"])
                  if not (directory / name).is_file() or (directory / name).stat().st_size == 0)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "External/dawn")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--ci", action="store_true")
    parser.add_argument("--configs", nargs="+")
    parser.add_argument("--print-architecture", action="store_true")
    args = parser.parse_args()
    system = platform.system()
    architecture = target_architecture(system)
    if args.print_architecture:
        print(architecture)
        return 0
    configs = args.configs or (os.environ.get("DAWN_BUILD_CONFIGS", "Debug Release").split()
                               if system == "Windows" else ["Release"])
    expected = identity(system, architecture, configs)
    if args.write:
        missing = [name for name in artifacts(system, configs)
                   if not (args.directory / name).is_file() or (args.directory / name).stat().st_size == 0]
        if missing:
            raise ValueError(f"Cannot mark incomplete Dawn build as reusable: {missing}")
        (args.directory / METADATA).write_text(json.dumps(expected, indent=2) + "\n")
        return 0
    errors = validate(args.directory, expected)
    print("[Dawn cache] " + ("valid artifact cache" if not errors else "; ".join(errors)))
    if args.ci:
        if errors and args.directory.exists() and not (args.directory / ".git").exists():
            # Preserve restored artifacts; never overlay a fresh checkout onto an unversioned tree.
            directory = args.directory.resolve()
            if directory != (ROOT / "External/dawn").resolve() or (directory / "CMakeLists.txt").exists():
                raise ValueError(f"Refusing to move an unrecognized source directory: {directory}")
            backup = directory.with_name(f"dawn-invalid-cache-{time.time_ns()}")
            directory.rename(backup)
            print(f"[Dawn cache] Preserved invalid cache at {backup}")
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"valid={'false' if errors else 'true'}\n")
        return 0
    return int(bool(errors))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print(f"[Dawn cache ERROR] {error}", file=sys.stderr)
        sys.exit(1)
