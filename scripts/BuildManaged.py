#!/usr/bin/env python3
"""Build the engine's managed (.NET) projects into the premake output tree.

Windows builds them through Index.sln. gmake2 cannot build SDK-style .NET projects, so on Linux and
macOS premake's Index-Managed utility project runs this script before the native consumers link.

Outputs land where the native binaries look for them:
    bin/<cfg>-<system>-<arch>/Index-ScriptCore/      Index-ScriptCore.dll (+ runtimeconfig, deps, xml)
    bin/<cfg>-<system>-<arch>/Index-ScriptCompiler/  editor-only hot-reload compiler
    bin/<cfg>-<system>-<arch>/Index-PackageTool/     package registry / asset library tool

Usage:
    python3 scripts/BuildManaged.py --configuration Release --output-dir bin/Release-linux-x86_64
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

PROJECTS = {
    "ScriptCore": REPO_ROOT / "Index-ScriptCore" / "Index-ScriptCore.csproj",
    "ScriptCompiler": REPO_ROOT / "Index-ScriptCompiler" / "Index-ScriptCompiler.csproj",
    "PackageTool": REPO_ROOT / "Index-PackageTool" / "Index-PackageTool.csproj",
}

# Where dotnet lands when the editor or make runs without a login shell's PATH (Finder, desktop entries).
DOTNET_FALLBACKS = (
    "/usr/local/share/dotnet/dotnet",
    "/opt/homebrew/bin/dotnet",
    "/usr/share/dotnet/dotnet",
    "/usr/lib/dotnet/dotnet",
    "~/.dotnet/dotnet",
)


def find_dotnet() -> str | None:
    root = os.environ.get("DOTNET_ROOT")
    if root:
        candidate = Path(root) / ("dotnet.exe" if os.name == "nt" else "dotnet")
        if candidate.is_file():
            return str(candidate)
    found = shutil.which("dotnet")
    if found:
        return found
    for fallback in DOTNET_FALLBACKS:
        candidate = Path(fallback).expanduser()
        if candidate.is_file():
            return str(candidate)
    return None


def is_arm64_host() -> bool:
    return platform.machine().lower() in ("arm64", "aarch64")


def build(dotnet: str, name: str, csproj: Path, configuration: str, output_dir: Path, intermediate_dir: Path) -> bool:
    out = output_dir / f"Index-{name}"
    obj = intermediate_dir / f"Index-{name}"
    command = [
        dotnet, "build", str(csproj),
        "-c", configuration,
        "-p:Platform=x64",
        # Trailing separators: MSBuild treats OutputPath / IntermediateOutputPath as directories.
        f"-p:OutputPath={out}{os.sep}",
        f"-p:IntermediateOutputPath={obj}{os.sep}",
        "--nologo",
        "-v:q",
    ]
    # The checked-in projects target x64; an arm64 CoreCLR refuses to load x64-marked IL.
    if is_arm64_host():
        command.append("-p:PlatformTarget=AnyCPU")
    print(f"[BuildManaged] Index-{name} ({configuration}) -> {out}", flush=True)
    return subprocess.run(command, cwd=REPO_ROOT).returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configuration", required=True, choices=("Debug", "Release", "Dist"))
    parser.add_argument("--output-dir", required=True, help="bin/<cfg>-<system>-<arch>")
    parser.add_argument("--intermediate-dir", help="bin-int/<cfg>-<system>-<arch> (derived from --output-dir by default)")
    parser.add_argument("--projects", default=",".join(PROJECTS), help="Comma-separated subset of: " + ", ".join(PROJECTS))
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = REPO_ROOT / output_dir
    if args.intermediate_dir:
        intermediate_dir = Path(args.intermediate_dir)
        if not intermediate_dir.is_absolute():
            intermediate_dir = REPO_ROOT / intermediate_dir
    else:
        intermediate_dir = output_dir.parent.parent / "bin-int" / output_dir.name

    dotnet = find_dotnet()
    if not dotnet:
        print("[BuildManaged] error: the .NET 10 SDK (dotnet) was not found. Install it from https://dot.net "
              "or set DOTNET_ROOT.", file=sys.stderr)
        return 1

    selected = [name.strip() for name in args.projects.split(",") if name.strip()]
    for name in selected:
        if name not in PROJECTS:
            print(f"[BuildManaged] error: unknown project '{name}'.", file=sys.stderr)
            return 2
        if not build(dotnet, name, PROJECTS[name], args.configuration, output_dir, intermediate_dir):
            print(f"[BuildManaged] error: Index-{name} failed to build.", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
