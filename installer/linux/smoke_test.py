#!/usr/bin/env python3
"""Exercise a real Linux installer in an isolated user home outside the checkout."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import sdk_manifest

FORBIDDEN_TOOLS = ("git", "cmake", "ninja", "msbuild", "clang", "clang++", "gcc", "g++", "cc", "c++", "make")


def isolated_environment(root: Path, dotnet: Path) -> dict[str, str]:
    environment = {name: value for name, value in os.environ.items()
                   if not name.upper().startswith(("FUNKE_", "DOTNET_", "MSBUILD", "NUGET_"))
                   and name not in ("LD_LIBRARY_PATH", "LD_PRELOAD", "CMAKE_PREFIX_PATH", "LIBRARY_PATH")}
    home = root / "Home Prüfung with spaces"
    guards = root / "tool-guards"
    guards.mkdir()
    log = root / "forbidden-tools.log"
    for name in FORBIDDEN_TOOLS:
        path = guards / name
        path.write_text("#!/bin/sh\nprintf '%s\\n' " + shlex.quote(name) + " >> "
                        + shlex.quote(str(log)) + "\nexit 97\n", encoding="utf-8")
        path.chmod(0o755)
    environment.update(HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"),
                       XDG_CONFIG_HOME=str(home / ".config"), XDG_CACHE_HOME=str(home / ".cache"),
                       DOTNET_CLI_HOME=str(home), DOTNET_ROOT=str(dotnet.parent),
                       DOTNET_MULTILEVEL_LOOKUP="0", DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1",
                       NUGET_PACKAGES=str(root / "nuget"), CI="true",
                       FUNKE_USER_DATA_ROOT=str(home / "Funke data"),
                       PATH=os.pathsep.join(map(str, (guards, dotnet.parent, Path(sys.executable).parent, Path("/usr/bin"), Path("/bin")))))
    home.mkdir()
    return environment


def verify(installer: Path, root: Path, report: Path, dotnet: Path) -> None:
    environment = isolated_environment(root, dotnet)
    home = Path(environment["HOME"])
    prefix = home / "Installed Funke Prüfung"
    unrelated = root / "unrelated-cwd"
    unrelated.mkdir()
    logs = root / "logs"
    logs.mkdir()
    evidence = {"schemaVersion": 1, "status": "running", "workRoot": str(root), "phases": [],
                "installer": str(installer), "childPath": environment["PATH"],
                "limitations": ["Tool PATH guards do not intercept absolute paths; this is a host-limited CI test."]}

    def save():
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    def run(label, command, timeout=1200):
        log = logs / f"{len(evidence['phases']):02d}-{label}.log"
        phase = {"name": label, "command": list(map(str, command)), "log": str(log), "cwd": str(unrelated)}
        evidence["phases"].append(phase)
        save()
        start = time.monotonic()
        with log.open("wb") as output:
            process = subprocess.run(command, cwd=unrelated, env=environment, stdout=output,
                                     stderr=subprocess.STDOUT, timeout=timeout)
        phase.update(exitCode=process.returncode, elapsedSeconds=round(time.monotonic() - start, 2))
        save()
        if process.returncode:
            raise RuntimeError(f"{label} failed ({process.returncode}):\n{log.read_text(errors='replace')}")

    save()
    try:
        package = root / installer.name
        shutil.copyfile(installer, package)
        package.chmod(0o755)
        run("verify-installer", [str(package), "--check"])
        run("install", [str(package), "--prefix", str(prefix)])
        current = prefix / "current"
        installed = current.resolve(strict=True)
        manifest = sdk_manifest.verify_bundle(installed, require_binary=True)
        evidence["sdk"] = manifest
        run("cli-version", [str(home / ".local/bin/funke"), "--version"])
        run("premake-version", [str(current / "EngineSDK/premake5"), "--version"])
        desktop = home / ".local/share/applications/dev.funke.Launcher.desktop"
        if not desktop.is_file() or not (home / ".local/bin/funke-launcher").is_file():
            raise RuntimeError("Desktop or launcher command integration is missing")
        helper_dir = root / "harness/scripts/ci"
        helper_dir.mkdir(parents=True)
        for name in ("application_smoke_test.py", "runtime_smoke_test.py", "desktop_export_smoke_test.py"):
            shutil.copyfile(ROOT / "scripts/ci" / name, helper_dir / name)
        python = sys.executable
        run("launcher", [python, str(helper_dir / "application_smoke_test.py"), "--binary", str(current / "Funke-Launcher")])
        run("editor", [python, str(helper_dir / "application_smoke_test.py"), "--binary", str(current / "Funke-Editor"), "--editor-project"])
        for profile in ("Runtime-Development", "Runtime-Dist"):
            run(profile, [python, str(helper_dir / "runtime_smoke_test.py"), "--binary",
                          str(current / "EngineSDK" / profile / "Funke-Runtime"), "--require-scripting"])
        run("export-with-private-dotnet", [python, str(helper_dir / "desktop_export_smoke_test.py"),
                                           "--cli", str(current / "funke")])
        if (root / "forbidden-tools.log").exists():
            raise RuntimeError("An installed application invoked a forbidden source/build tool: "
                               + (root / "forbidden-tools.log").read_text())
        project = home / "Projects/Keep Prüfung/project.json"
        project.parent.mkdir(parents=True)
        project.write_text('{"name":"Preserve me"}\n', encoding="utf-8")
        project_hash = hashlib.sha256(project.read_bytes()).hexdigest()
        unowned = prefix / "Personal project.txt"
        unowned.write_text("preserve this", encoding="utf-8")
        run("reinstall-upgrade", [str(package), "--prefix", str(prefix)])
        sdk_manifest.verify_bundle(current.resolve(strict=True), require_binary=True)
        run("cli-after-upgrade", [str(home / ".local/bin/funke"), "--version"])
        run("uninstall", [python, str(prefix / "uninstall.py"), "--uninstall", "--prefix", str(prefix)])
        if current.exists() or current.is_symlink() or desktop.exists() or (home / ".local/bin/funke").exists():
            raise RuntimeError("Uninstall left the active application or integration behind")
        if hashlib.sha256(project.read_bytes()).hexdigest() != project_hash or unowned.read_text() != "preserve this":
            raise RuntimeError("Uninstall changed a user project")
        evidence["status"] = "passed"
        save()
    except Exception as error:
        evidence.update(status="failed", error=str(error))
        save()
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", type=Path, required=True)
    parser.add_argument("--dotnet", type=Path)
    parser.add_argument("--work-parent", type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("This smoke test requires a native Linux environment")
    dotnet = args.dotnet or (Path(shutil.which("dotnet")) if shutil.which("dotnet") else None)
    if not dotnet or not dotnet.is_file() or not args.installer.is_file():
        parser.error("A real .run installer and a .NET 10 SDK are required")
    work = args.work_parent.resolve()
    if work.is_relative_to(ROOT) or work.is_relative_to(args.installer.parent.resolve()):
        parser.error("--work-parent must be outside the checkout and installer output")
    work.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="Funke Linux Installer Prüfung ", dir=work))
    report = args.report.resolve() if args.report else root / "evidence.json"
    try:
        verify(args.installer.resolve(), root, report, dotnet.resolve())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Linux installer smoke failed: {error}\nEvidence: {report}", file=sys.stderr)
        return 1
    print(f"Linux installer smoke passed: {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
