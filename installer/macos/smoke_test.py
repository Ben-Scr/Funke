#!/usr/bin/env python3
"""Mount the finished DMG and execute its app from a relocated, repository-free directory."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import BuildMacInstaller as builder
import sdk_manifest


def checked(command: list[str], cwd: Path, timeout: int = 180, environment: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{Path(command[0]).name} failed ({result.returncode}):\n{result.stdout}")
    return result.stdout


def isolated_environment(root: Path) -> dict[str, str]:
    dotnet = shutil.which("dotnet")
    if not dotnet:
        raise ValueError("The installer smoke test requires the .NET 10 SDK")
    dotnet_root = Path(dotnet).resolve().parent
    environment = {name: value for name, value in os.environ.items()
                   if not name.upper().startswith(("FUNKE_", "DOTNET_", "MSBUILD", "NUGET_", "DYLD_", "LD_"))
                   and name.upper() not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH")}
    home = root / "Fresh Home Prüfung"
    home.mkdir()
    environment.update(HOME=str(home), DOTNET_CLI_HOME=str(home), DOTNET_ROOT=str(dotnet_root),
                       DOTNET_MULTILEVEL_LOOKUP="0", DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1",
                       NUGET_PACKAGES=str(root / "NuGet packages"), CI="true",
                       XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"),
                       XDG_CACHE_HOME=str(home / ".cache"),
                       FUNKE_USER_DATA_ROOT=str(home / "Library/Application Support"),
                       PATH=os.pathsep.join(map(str, (dotnet_root, Path(sys.executable).parent,
                                                     Path("/usr/bin"), Path("/bin"), Path("/usr/sbin"), Path("/sbin")))))
    return environment


def verify_applications(payload: Path, harness: Path, directory: Path, environment: dict[str, str]) -> None:
    helper = harness / "application_smoke_test.py"
    for executable, options in (("Funke-Launcher", []), ("Funke-Editor", ["--editor-project"])):
        print(checked([sys.executable, str(helper), "--binary", str(payload / executable),
                       "--working-directory", str(directory), *options],
                      directory, environment=environment).strip())


def verify_inventory(app: Path, inventory: dict) -> None:
    files = inventory.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("Missing installer file inventory")
    links = {path.relative_to(app).as_posix(): path.readlink().as_posix()
             for path in app.rglob("*") if path.is_symlink()}
    if links != inventory.get("symlinks", {}):
        raise ValueError("Installed app symlinks do not match the build inventory")
    for name in links:
        if not (app / name).resolve().is_relative_to(app.resolve()):
            raise ValueError("Installed app symlink escapes its bundle")
    actual = {path.relative_to(app).as_posix() for path in app.rglob("*") if path.is_file() and not path.is_symlink()}
    if actual != set(files):
        raise ValueError("Installed app does not match the build file inventory")
    for name, digest in files.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or "\\" in name or any(part in ("", ".", "..") for part in name.split("/")):
            raise ValueError("Invalid installer inventory path")
        path = app.joinpath(*relative.parts)
        if not path.resolve().is_relative_to(app.resolve()) or builder.sha256(path) != digest:
            raise ValueError(f"Installed app checksum mismatch: {name}")


def verify(dmg: Path, require_scripting: bool) -> None:
    inventory = json.loads(Path(str(dmg) + ".manifest.json").read_text(encoding="utf-8"))
    if builder.sha256(dmg) != inventory.get("sha256"):
        raise ValueError("DMG checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="Funke DMG Prüfung ") as temporary:
        directory = Path(temporary)
        mount = directory / "Mounted Installer"
        mount.mkdir()
        app = directory / "Relocated Applications/Funke.app"
        mounted = False
        try:
            checked(["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint", str(mount), str(dmg)], directory)
            mounted = True
            checked(["ditto", str(mount / "Funke.app"), str(app)], directory)
        finally:
            if mounted or os.path.ismount(mount):
                checked(["hdiutil", "detach", str(mount)], directory)
        verify_inventory(app, inventory)
        checked(["codesign", "--verify", "--deep", "--strict", str(app)], directory)
        if inventory.get("notarized"):
            checked(["xcrun", "stapler", "validate", str(app)], directory)
            checked(["spctl", "--assess", "--type", "execute", str(app)], directory)
        payload = app / "Contents/Resources"
        sdk_manifest.verify_bundle(payload, require_binary=not inventory["nativeSdkIncluded"], macos_app=True)
        environment = isolated_environment(directory)
        version = checked([str(payload / "funke"), "--version"], directory, environment=environment)
        if inventory["engine"]["version"] not in version:
            raise ValueError("Installed CLI version does not match the DMG")
        checked([str(payload / "EngineSDK/premake5"), "--version"], directory, environment=environment)
        harness = directory / "Standalone Verification/scripts/ci"
        harness.mkdir(parents=True)
        for name in ("application_smoke_test.py", "runtime_smoke_test.py", "desktop_export_smoke_test.py"):
            shutil.copyfile(ROOT / "scripts/ci" / name, harness / name)
        verify_applications(payload, harness, directory, environment)
        runtime = [sys.executable, str(harness / "runtime_smoke_test.py"),
                   "--binary", str(payload / "Funke-Runtime")]
        if require_scripting:
            runtime.append("--require-scripting")
        print(checked(runtime, directory, environment=environment).strip())
        print(checked([sys.executable, str(harness / "desktop_export_smoke_test.py"),
                       "--cli", str(payload / "funke")], directory, timeout=1800, environment=environment).strip())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", "--dmg", dest="dmg", type=Path, required=True)
    parser.add_argument("--require-scripting", action="store_true")
    args = parser.parse_args()
    try:
        if platform.system() != "Darwin":
            raise ValueError("DMG smoke test requires macOS")
        verify(args.dmg.resolve(), args.require_scripting)
    except (OSError, KeyError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"[Mac smoke] {error}", file=sys.stderr)
        return 1
    print("[Mac smoke] Finished DMG passed integrity, relocation, Launcher, Editor, CLI, Premake, Runtime, project creation and export checks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
