#!/usr/bin/env python3
"""Archive the Linux / macOS editor bundle (POSIX counterpart of BuildWindows.ps1 -PublishBundle).

The Release Funke-Launcher output already holds everything a user needs: the Launcher, Editor, Runtime,
`funke` CLI, Funke-ScriptCore, the hot-reload compiler and the EngineSDK (scripts/CopyEngineSdk.py). This
script checks that layout and packs it as Funke-Editor-<version>-<Linux|macOS>-<arch>.tar.gz, keeping
executable bits and the shared-library symlinks.

Usage:
    python3 scripts/PackagePosix.py [--configuration Release] [--output-dir bin]
"""

from __future__ import annotations

import argparse
import json
import platform
import sys
import tarfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
IS_MAC = platform.system() == "Darwin"


def architecture() -> tuple[str, str]:
    """(premake folder arch, archive arch label)."""
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64", "arm64"
    return "x86_64", "x64"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configuration", default="Release", choices=("Debug", "Release", "Dist"))
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "bin"))
    args = parser.parse_args()

    folder_arch, label_arch = architecture()
    system = "macosx" if IS_MAC else "linux"
    platform_label = "macOS" if IS_MAC else "Linux"
    launcher_dir = REPO_ROOT / "bin" / f"{args.configuration}-{system}-{folder_arch}" / "Funke-Launcher"

    engine_lib = "libFunke-Engine.dylib" if IS_MAC else "libFunke-Engine.so"
    required = [
        "Funke-Launcher", "Funke-Editor", "Funke-Runtime", engine_lib, "Funke-ScriptCore.dll",
        "Funke-ScriptCore.runtimeconfig.json", "Funke-PackageTool", "FunkeAssets",
        "EngineSDK/Licenses/Funke/LICENSE.txt", "EngineSDK/Funke-Engine/src",
    ]
    missing = [name for name in required if not (launcher_dir / name).exists()]
    if missing:
        print(f"[PackagePosix] error: {launcher_dir} is incomplete; missing: {', '.join(missing)}", file=sys.stderr)
        print("[PackagePosix] Build first: make config=release Funke-Launcher (and config=dist Funke-Runtime "
              "for Release exports).", file=sys.stderr)
        return 1
    if not (launcher_dir / "EngineSDK" / "Runtime-Dist").is_dir():
        print("[PackagePosix] warning: EngineSDK/Runtime-Dist is missing; Release-profile exports will need a "
              "source tree. Build `make config=dist Funke-Runtime` before the Launcher.", file=sys.stderr)

    version = "0.0.0-dev"
    metadata = REPO_ROOT / "build" / "BuildMetadata.json"
    if metadata.is_file():
        version = json.loads(metadata.read_text(encoding="utf-8")).get("version", version)

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    archive = output_dir / f"Funke-Editor-{version}-{platform_label}-{label_arch}.tar.gz"
    root_name = f"Funke-{version}"

    # Files a local run leaves behind must not ship.
    skipped = {"imgui.ini", "funke-project.json"}

    def keep(info: tarfile.TarInfo) -> tarfile.TarInfo | None:
        if Path(info.name).name in skipped:
            return None
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        return info

    with tarfile.open(archive, "w:gz") as tar:
        tar.add(launcher_dir, arcname=root_name, filter=keep)
    print(f"[PackagePosix] {archive} ({archive.stat().st_size / (1024 * 1024):.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
