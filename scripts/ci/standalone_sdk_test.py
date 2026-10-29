#!/usr/bin/env python3
"""Verify a binary Windows installation from unrelated directories; never build the engine."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

TOOLS = ("git", "cmake", "ninja", "msbuild", "cl", "clang", "clang-cl", "gcc", "g++", "make", "nmake", "vswhere")
CONTENT_DENY = ("engineSDK/funke-engine", "engineSDK/external", "engineSDK/bin", "engineSDK/Funke.sln", "engineSDK/premake5.lua",
                "engineSDK/funke-scriptcore/source", "engineSDK/funke-scriptcore/funke-scriptcore.csproj",
                "Funke-Engine", "External", "Funke-ScriptCore/Source", "Funke-ScriptCore/Funke-ScriptCore.csproj", ".git")
ERROR_MARKERS = ("Unhandled exception at app boundary", "CrashReporter wrote minidump",
                 "Failed to initialize .NET runtime", "interop mismatch", "WebGPU [Validation]",
                 "was compiled against other engine headers")
STABLE_MARKER = "CI smoke test reached a stable frame; requesting graceful shutdown."
REPORT_RETRY_DELAYS = (0.025, 0.05, 0.1, 0.2, 0.25, 0.25, 0.25)


def atomic_write_json(path: Path, value: object):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2)
        for attempt in range(len(REPORT_RETRY_DELAYS) + 1):
            try:
                temporary.replace(path)
                return
            except OSError as error:
                # Windows readers/virus scanners may briefly deny replacement without delete sharing.
                if getattr(error, "winerror", None) not in (5, 32, 33) or attempt == len(REPORT_RETRY_DELAYS):
                    raise
                time.sleep(REPORT_RETRY_DELAYS[attempt])
    finally:
        temporary.unlink(missing_ok=True)


def stable_dotnet10_sdk(output: str) -> str | None:
    patches = [int(match.group(1)) for line in output.splitlines()
               if (match := re.fullmatch(r"10\.0\.(\d+)\s+\[.+\]", line.strip())) and int(match.group(1)) >= 100]
    return f"10.0.{max(patches)}" if patches else None


def inside(path: Path, root: Path) -> bool:
    return path.resolve().is_relative_to(root.resolve())


def require_external(path: Path, roots: list[Path]) -> None:
    for root in roots:
        if inside(path, root):
            raise ValueError(f"Test location must be outside forbidden root {root}: {path}")


def sdk_relative(sdk: Path, value: object) -> Path:
    if (not isinstance(value, str) or not value or "\\" in value or ":" in value
            or any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError(f"Invalid SDK-relative path: {value!r}")
    relative = Path(value)
    if relative.is_absolute() or PureWindowsPath(value).drive or ".." in relative.parts:
        raise ValueError(f"SDK path escapes installation: {value}")
    target = sdk / relative
    if not inside(target, sdk) or not target.is_dir():
        raise ValueError(f"SDK directory missing or escaping installation: {target}")
    return target


def installation_inventory(install: Path) -> list[str]:
    if not install.is_dir():
        raise ValueError(f"Installation directory missing: {install}")
    if install.is_symlink() or getattr(install.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
        raise ValueError(f"Installation root must not be a link/reparse point: {install}")
    files: list[str] = []
    denied = tuple(p.lower() for p in CONTENT_DENY)
    for current, directories, names in os.walk(install, followlinks=False):
        for name in directories + names:
            path = Path(current) / name
            relative = path.relative_to(install).as_posix()
            attributes = getattr(path.lstat(), "st_file_attributes", 0)
            if path.is_symlink() or attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
                raise ValueError(f"Links/reparse points are not allowed in the binary payload: {relative}")
            lower = relative.lower()
            if any(lower == item or lower.startswith(item + "/") for item in denied):
                raise ValueError(f"Source/development payload is forbidden: {relative}")
            if path.is_file():
                if path.stem.lower() in TOOLS and path.suffix.lower() in (".exe", ".cmd", ".bat"):
                    raise ValueError(f"Native build tool in binary payload: {relative}")
                files.append(relative)
    return sorted(files)


def validate_installation(install: Path) -> tuple[dict, list[str]]:
    files = installation_inventory(install)
    sdk = install / "EngineSDK"
    manifest = json.loads((sdk / "FunkeSDK.json").read_text(encoding="utf-8-sig"))
    if not isinstance(manifest, dict) or type(manifest.get("schemaVersion")) is not int or manifest["schemaVersion"] != 1:
        raise ValueError("FunkeSDK.json must use schemaVersion 1")
    if not isinstance(manifest.get("engineVersion"), str) or not manifest["engineVersion"].strip():
        raise ValueError("SDK engineVersion is missing")
    if manifest.get("platform") != "windows" or manifest.get("architecture") not in ("x86_64", "arm64"):
        raise ValueError("This end-to-end harness requires a Windows x86_64/arm64 SDK")
    capabilities = manifest.get("capabilities", {})
    if not isinstance(capabilities, dict):
        raise ValueError("SDK capabilities must be an object")
    for key, expected in (("managedProjects", True), ("nativeDevelopment", False), ("sourceBuild", False)):
        if capabilities.get(key) is not expected:
            raise ValueError(f"Binary SDK capability {key} must be {expected}")
    runtime_map = manifest.get("runtimes", {})
    if not isinstance(runtime_map, dict) or set(runtime_map) - {"Development", "Release", "Debug"}:
        raise ValueError("SDK runtimes must be a supported profile map")
    for profile, expected in (("Development", "Runtime-Development"), ("Release", "Runtime-Dist")):
        if runtime_map.get(profile) != expected:
            raise ValueError(f"SDK runtime {profile} must map to {expected}")
        folder = sdk_relative(sdk, runtime_map[profile])
        for name in ("Funke-Runtime.exe", "Funke-Engine.dll", "SDL3.dll", "nethost.dll", "Funke-ScriptCore.dll",
                     "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json"):
            if not (folder / name).is_file():
                raise ValueError(f"Incomplete runtime payload: {folder / name}")
    for name in ("funke.exe", "Funke-Launcher.exe", "Funke-Editor.exe", "Funke-Engine.dll"):
        if not (install / name).is_file():
            raise ValueError(f"Installed executable/library missing: {name}")
    for name in ("Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json"):
        if not (sdk / "Funke-ScriptCore" / name).is_file():
            raise ValueError(f"Managed SDK artifact missing: {name}")
    if not (sdk / "templates").is_dir():
        raise ValueError("Project-creation verification requires the optional SDK templates component")
    for name in ("premake5.exe", "premake/project-packages.lua", "premake/package-loader.lua",
                 "premake/architecture.lua", "premake/compat.lua", "premake/fix_csharp_platforms.lua"):
        if not (sdk / name).is_file():
            raise ValueError(f"Managed package tooling missing: {name}")
    return manifest, files


def child_environment(base: dict[str, str], root: Path, dotnet: Path, guards: Path,
                      player: bool = False) -> dict[str, str]:
    # NuGet resolves machine-wide settings from ProgramFiles(x86), even with cleared package sources.
    permitted = {"systemroot", "systemdrive", "windir", "comspec", "pathext", "os",
                 "processor_architecture", "number_of_processors", "programfiles", "programfiles(x86)",
                 "programw6432", "commonprogramfiles", "commonprogramfiles(x86)", "commonprogramw6432",
                 "programdata", "allusersprofile"}
    env = {key: value for key, value in base.items() if key.lower() in permitted}
    windows = Path(next((v for k, v in base.items() if k.lower() == "systemroot"), r"C:\Windows"))
    profile = root / "user"
    env.update({"USERPROFILE": str(profile), "HOME": str(profile),
                "LOCALAPPDATA": str(profile / "AppData/Local"), "APPDATA": str(profile / "AppData/Roaming"),
                "TEMP": str(root / "temp"), "TMP": str(root / "temp"),
                "DOTNET_CLI_HOME": str(profile), "NUGET_PACKAGES": str(root / "nuget"),
                "DOTNET_MULTILEVEL_LOOKUP": "0", "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
                "DOTNET_NOLOGO": "1", "DOTNET_SKIP_FIRST_TIME_EXPERIENCE": "1",
                "DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE": "true", "CI": "true", "MSBUILDDISABLENODEREUSE": "1",
                "FUNKE_USER_DATA_ROOT": str(root / "funke-user-data"),
                "STANDALONE_TOOL_LOG": str(root / "forbidden-tools.jsonl")})
    roots = (guards, windows / "System32", windows)
    env["PATH"] = os.pathsep.join(map(str, roots if player else (guards, dotnet.parent, *roots[1:])))
    runtime_root = root / "no-system-dotnet" if player else dotnet.parent
    for key in ("DOTNET_ROOT", "DOTNET_ROOT_X64", "DOTNET_ROOT_ARM64", "DOTNET_ROOT(x86)"):
        env[key] = str(runtime_root)
    return env


def kill_tree(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run([str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/taskkill.exe"),
                        "/PID", str(process.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15, check=False)
    else:
        process.kill()
    process.wait(timeout=15)


def check_references(folder: Path, forbidden: list[Path]) -> None:
    needles = [str(path.resolve()).replace("\\", "/").lower() for path in forbidden]
    for path in folder.rglob("*"):
        if path.is_file() and path.suffix.lower() in (".csproj", ".props", ".targets"):
            text = path.read_text(encoding="utf-8-sig", errors="replace").replace("\\", "/").lower()
            if "funke-scriptcore.csproj" in text or any(needle in text for needle in needles):
                raise ValueError(f"Generated build file refers to forbidden engine sources: {path}")


class Harness:
    def __init__(self, root: Path, report: Path, env: dict[str, str], timeout: int):
        self.root, self.report, self.env, self.timeout = root, report, env, timeout
        self.evidence = {"schemaVersion": 1, "status": "running", "workRoot": str(root), "isolation": "host-limited",
                         "machine": {"platform": platform.platform(), "architecture": platform.machine(), "python": sys.version},
                         "childPath": env["PATH"],
                         "phases": [], "limitations": ["PATH guards cannot intercept absolute tool paths on a developer host.",
                         "No-repository machine proof requires the separate Windows Sandbox recipe."]}
        self.cwd = root / "unrelated-cwd"
        self.cwd.mkdir()
        (root / "logs").mkdir()
        (root / "NuGet.Config").write_text('<configuration><packageSources><clear/></packageSources></configuration>', encoding="utf-8")
        for key in ("TEMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA"):
            Path(env[key]).mkdir(parents=True, exist_ok=True)
        self.save()

    def save(self):
        atomic_write_json(self.report, self.evidence)

    def run(self, label: str, command: list[str], *, env=None, allowed=(0,), timeout=None) -> str:
        log = self.root / "logs" / f"{len(self.evidence['phases']):03d}-{label}.log"
        phase = {"name": label, "command": list(map(str, command)), "cwd": str(self.cwd), "log": str(log)}
        self.evidence["phases"].append(phase)
        self.save()
        started = time.monotonic()
        with log.open("wb") as stream:
            process = subprocess.Popen(command, cwd=self.cwd, env=env or self.env, stdout=stream,
                                       stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                phase["exitCode"] = process.wait(timeout=timeout or self.timeout)
            except subprocess.TimeoutExpired:
                phase["timedOut"] = True
                kill_tree(process)
                raise RuntimeError(f"{label} exceeded its timeout; see {log}")
            finally:
                phase["seconds"] = round(time.monotonic() - started, 3)
                self.save()
        with log.open("rb") as stream:
            output = stream.read(8 * 1024 * 1024).decode("utf-8", errors="replace")
        if log.stat().st_size > 8 * 1024 * 1024:
            raise RuntimeError(f"{label} exceeded the parsed log budget; complete output remains at {log}")
        if phase["exitCode"] not in allowed:
            raise RuntimeError(f"{label} failed ({phase['exitCode']}); see {log}")
        if any(marker in output for marker in ERROR_MARKERS):
            raise RuntimeError(f"{label} emitted an error marker; see {log}")
        return output

    def assert_no_tools(self):
        log = Path(self.env["STANDALONE_TOOL_LOG"])
        if log.exists() and log.read_text(encoding="utf-8").strip():
            raise RuntimeError(f"A forbidden native tool was invoked: {log}")


def build_guards(harness: Harness, dotnet: Path, guards: Path):
    source = harness.root / "guard-source"
    source.mkdir()
    (source / "ToolGuard.csproj").write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
        '<OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><UseAppHost>true</UseAppHost>'
        '<AppHostDotNetSearch>AppRelative</AppHostDotNetSearch><AppHostRelativeDotNet>dotnet</AppHostRelativeDotNet>'
        '</PropertyGroup></Project>', encoding="utf-8")
    (source / "NuGet.Config").write_text('<configuration><packageSources><clear/></packageSources></configuration>', encoding="utf-8")
    (source / "Program.cs").write_text('using System; using System.IO; using System.Text.Json;\n'
        'File.AppendAllText(Environment.GetEnvironmentVariable("STANDALONE_TOOL_LOG")!, '
        'JsonSerializer.Serialize(new { tool = Environment.ProcessPath, args }) + "\\n");\nreturn 97;\n', encoding="utf-8")
    harness.run("build-managed-tool-guards", [str(dotnet), "publish", str(source / "ToolGuard.csproj"),
                "-c", "Release", "-o", str(guards), "--self-contained", "false", "--disable-build-servers", "--nologo"])
    for relative in ("host/fxr", "shared/Microsoft.NETCore.App"):
        versions = [p for p in (dotnet.parent / relative).iterdir() if p.is_dir() and p.name.startswith("10.") and "-" not in p.name]
        if not versions:
            raise RuntimeError(f"Tool guards require a stable .NET 10 runtime in {dotnet.parent / relative}")
        latest = max(versions, key=lambda p: tuple(map(int, p.name.split("."))))
        shutil.copytree(latest, guards / "dotnet" / relative / latest.name)
    for name in TOOLS:
        shutil.copy2(guards / "ToolGuard.exe", guards / f"{name}.exe")
    player_env = child_environment(os.environ, harness.root, dotnet, guards, player=True)
    harness.run("guard-self-test", [str(guards / "git.exe"), "--version"], env=player_env, allowed=(97,))
    guard_log = Path(harness.env["STANDALONE_TOOL_LOG"])
    if not guard_log.is_file() or "git.exe" not in guard_log.read_text(encoding="utf-8").lower():
        raise RuntimeError("Native tool guard did not record its self-test")
    guard_log.rename(harness.root / "guard-self-test.jsonl")


def write_package(project: Path):
    package = project / "Packages/Standalone.ManagedProbe"
    (package / "CSharp").mkdir(parents=True)
    (package / "funke-package.lua").write_text('return { name = "Standalone.ManagedProbe", version = "1.0.0", '
        'layers = { csharp = { sources = { "CSharp/**.cs" } } } }\n', encoding="utf-8")
    (package / "CSharp/PackageProbe.cs").write_text('using Funke;\nnamespace Standalone.ManagedProbe;\n'
        'public static class PackageProbe { public static string Verify() => '
        'typeof(EntityScript).Name == "EntityScript" ? "package-ok" : "package-failed"; }\n', encoding="utf-8")


def write_probe(project: Path, revision: str, tween: bool):
    script = project / "Assets/Scripts/StandaloneProbe.cs"
    script.parent.mkdir(parents=True, exist_ok=True)
    package_check = ' if (typeof(Funke.Tween.Ease).Assembly.GetName().Name != "Pkg.Funke.Tween") throw new Exception("Tween assembly missing");' if tween else ''
    script.write_text('using System; using System.IO; using Funke;\n'
        'public sealed class StandaloneProbe : EntityScript { int frames;\n'
        ' public override void OnUpdate() { if (++frames != 10) return;' + package_check +
        f' var marker = "{revision}|" + (Application.IsEditor ? "editor" : Engine.ConfigurationName)'
        ' + "|" + Standalone.ManagedProbe.PackageProbe.Verify();'
        ' File.WriteAllText(Environment.GetEnvironmentVariable("STANDALONE_PROBE_FILE")!, marker);'
        ' Log.Info(marker); }\n}\n', encoding="utf-8")


def build_managed_packages(harness: Harness, dotnet: Path, project: Path):
    generated = project / ".funke/build/packages/generated"
    projects = sorted(generated.rglob("*.csproj"))
    if not any(path.stem == "Pkg.Standalone.ManagedProbe" for path in projects):
        raise RuntimeError("Shipped Premake did not generate the mandatory C# package project")
    for index, package in enumerate(projects):
        harness.run(f"compile-managed-package-{index}", [str(dotnet), "build", str(package),
            "--configuration", "Release", "-p:Platform=x64", "--disable-build-servers", "--nologo"])
    expected = project / ".funke/build/packages/bin/Release-windows-x86_64/Pkg.Standalone.ManagedProbe/Pkg.Standalone.ManagedProbe.dll"
    if not expected.is_file():
        raise RuntimeError(f"Managed package build produced no expected DLL: {expected}")


def require_marker(path: Path, expected: str):
    actual = path.read_text(encoding="utf-8") if path.is_file() else None
    if actual != expected:
        raise RuntimeError(f"Managed update marker mismatch: expected {expected!r}, got {actual!r} at {path}")


def require_clean_log(path: Path, label: str):
    markers = tuple(marker.encode("utf-8") for marker in ERROR_MARKERS)
    overlap = max(map(len, markers)) - 1
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            data = tail + chunk
            if any(marker in data for marker in markers):
                raise RuntimeError(f"{label} emitted an error marker; see {path}")
            tail = data[-overlap:]


def editor_roundtrip(harness: Harness, install: Path, project: Path, tween: bool):
    marker = harness.root / "editor-probe.txt"
    environment = harness.env | {"STANDALONE_PROBE_FILE": str(marker), "FUNKE_EDITOR_BRIDGE": "1"}
    log = harness.root / "logs/editor-session.log"
    with log.open("wb") as stream:
        editor = subprocess.Popen([str(install / "Funke-Editor.exe"), f"--project={project}", "--background", "--prefs-profile=standalone-ci"],
            cwd=harness.cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        harness.evidence["editor"] = {"pid": editor.pid, "log": str(log)}
        harness.save()
        def bridge(command: str, *args: str, allowed=(0,), timeout=None):
            return harness.run(f"editor-{command}", [str(install / "funke.exe"), "editor", command, *args,
                "--pid", str(editor.pid), "--project", str(project), "--json", "--timeout",
                str(timeout or harness.timeout)], env=environment, allowed=allowed,
                timeout=(timeout or harness.timeout) + 10)
        try:
            deadline = time.monotonic() + min(harness.timeout, 120)
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f"Editor exited before its bridge became ready; see {log}")
                output = bridge("status", allowed=(0, 1, 3, 4), timeout=5)
                if harness.evidence["phases"][-1]["exitCode"] == 0:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Editor bridge unavailable; see {log} and status logs")
                time.sleep(1)
            for revision in ("V1", "V2"):
                write_probe(project, revision, tween)
                marker.unlink(missing_ok=True)
                compiled = json.loads(bridge("recompile"))
                if compiled.get("succeeded") is not True:
                    raise RuntimeError(f"Editor script compilation failed: {compiled}")
                bridge("play", "--frames", "20", "--size", "640x360")
                require_marker(marker, revision + "|editor|package-ok")
                bridge("stop")
            bridge("screenshot", str(harness.root / "editor.png"), "--size", "640x360")
            harness.evidence["editor"]["editCompilePlay"] = "passed V1 and V2"
        finally:
            kill_tree(editor)
            harness.evidence["editor"]["cleanup"] = "owned process tree terminated after bridge checks"
            harness.save()
    require_clean_log(log, "Editor")
    harness.evidence["editor"]["processLog"] = "checked: no error markers"
    harness.save()


def launcher_smoke(harness: Harness, install: Path):
    output = harness.run("launcher", [str(install / "Funke-Launcher.exe"), "--ci-smoke-test", "--background"], timeout=90)
    logs = Path(harness.env["FUNKE_USER_DATA_ROOT"]) / "Funke/Logs"
    runtime_log = harness.root / "logs/launcher-runtime.log"
    with runtime_log.open("w", encoding="utf-8") as saved:
        for path in sorted(logs.glob("Funke-Launcher*.log")):
            if path.stat().st_size > 8 * 1024 * 1024:
                raise RuntimeError(f"Launcher runtime log exceeds parsed log budget: {path}")
            text = path.read_text(encoding="utf-8", errors="replace")
            saved.write(text)
            output += text
    if STABLE_MARKER not in output or any(marker in output for marker in (*ERROR_MARKERS, "CI smoke test failed")):
        raise RuntimeError(f"Launcher did not complete a clean stable-frame smoke test; see {runtime_log}")
    harness.evidence["launcher"] = {"status": "passed", "exitCode": 0, "marker": STABLE_MARKER, "runtimeLog": str(runtime_log)}
    harness.save()


def verify(harness: Harness, source: Path, dotnet: Path, forbidden: list[Path], tween: bool, editor: bool):
    manifest, inventory = validate_installation(source)
    harness.evidence["manifest"] = manifest
    harness.evidence["payloadFiles"] = len(inventory)
    install = harness.root / "Installed Funke"
    shutil.copytree(source, install)
    _, copied = validate_installation(install)
    if copied != inventory:
        raise RuntimeError("Copied installation inventory differs from its input")
    harness.evidence["hashes"] = {name: hashlib.sha256((install / name).read_bytes()).hexdigest()
        for name in ("funke.exe", "Funke-Launcher.exe", "Funke-Editor.exe", "Funke-Engine.dll", "EngineSDK/FunkeSDK.json")}
    version = harness.run("dotnet-sdk", [str(dotnet), "--list-sdks"])
    sdk_version = stable_dotnet10_sdk(version)
    if not sdk_version:
        raise RuntimeError("A stable .NET 10.0 SDK (10.0.100 or newer) is required for managed projects and the guard apphost")
    (harness.root / "global.json").write_text(json.dumps({"sdk": {
        "version": sdk_version, "rollForward": "disable", "allowPrerelease": False}}, indent=2), encoding="utf-8")
    harness.evidence["dotnetSdk"] = sdk_version
    guards = Path(harness.env["PATH"].split(os.pathsep)[0])
    build_guards(harness, dotnet, guards)
    launcher_smoke(harness, install)
    cli = str(install / "funke.exe")
    project = harness.root / "Projects/StandaloneProbe"
    harness.run("create", [cli, "create", "StandaloneProbe", "--path", str(project.parent), "--no-git", "--no-register"])
    if (project / ".git").exists():
        raise RuntimeError("--no-git project unexpectedly contains .git")
    if tween:
        if not (install / "EngineSDK/packages/Funke.Tween/funke-package.lua").is_file():
            raise RuntimeError("--with-tween requires the installed managed package; no network fallback is allowed")
        harness.run("managed-package", [cli, "package", "add", "Funke.Tween", "--project", str(project)])
    config_file = project / "project.json"
    config = json.loads(config_file.read_text(encoding="utf-8-sig"))
    config.update(modules=["Funke.2D", "Funke.3D"], startupScene="SampleScene",
                  lastOpenedScene="SampleScene", buildScenes=["SampleScene"])
    config_file.write_text(json.dumps(config, indent=2), encoding="utf-8")
    write_package(project)
    (project / "NuGet.Config").write_text('<configuration><packageSources><clear/></packageSources></configuration>', encoding="utf-8")
    harness.run("enable-managed-package", [cli, "package", "add", "Standalone.ManagedProbe", "--project", str(project)])
    sdk = install / "EngineSDK"
    harness.run("generate-managed-package", [str(sdk / "premake5.exe"),
        "--file=" + str(sdk / "premake/project-packages.lua"), "vs2022", "--funke-project=" + str(project)])
    build_managed_packages(harness, dotnet, project)
    harness.run("refresh-managed-package", [cli, "package", "refresh", "--project", str(project)])
    check_references(project, forbidden)
    harness.evidence["managedPackage"] = {"name": "Standalone.ManagedProbe", "source": "local offline fixture", "additionalTween": tween}
    harness.evidence["managedPackage"]["initialCompilation"] = "Explicit dotnet build of shipped-Premake generated C# projects; package add/refresh do not compile source packages."
    scene_file = project / "Assets/Scenes/SampleScene.scene"
    scene = json.loads(scene_file.read_text(encoding="utf-8-sig"))
    scene["entities"].append({"name": "Standalone Probe", "Scripts": [{"className": "StandaloneProbe", "enabled": True}]})
    scene_file.write_text(json.dumps(scene, indent=2), encoding="utf-8")
    write_probe(project, "V1", tween)
    if editor:
        editor_roundtrip(harness, install, project, tween)
    else:
        harness.evidence["limitations"].append("Editor edit/compile/play was explicitly skipped; this is not the complete release gate.")
        write_probe(project, "V2", tween)
    relocated = Path(tempfile.mkdtemp(prefix="Funke-Standalone-Spielprüfung-", dir=harness.root.parent))
    require_external(relocated, forbidden + [install, project])
    harness.evidence["playerRoot"] = str(relocated)
    harness.evidence["players"] = []
    for profile in ("Development", "Release"):
        output = harness.root / "Exports" / profile
        harness.run("export-" + profile, [cli, "build", "--project", str(project),
                    "--" + profile.lower(), "--output", str(output), "--verbose"])
        check_references(project, forbidden)
        shutil.copytree(output, relocated / profile)
        if not list((relocated / profile).rglob("Pkg.Standalone.ManagedProbe.dll")):
            raise RuntimeError(f"{profile} export did not include the mandatory managed package")
        if tween and not list((relocated / profile).rglob("Pkg.Funke.Tween.dll")):
            raise RuntimeError(f"{profile} export did not include the enabled managed package")
    harness.assert_no_tools()
    # Both old absolute paths disappear before the exported player starts.
    retired_install = install.with_name("Retired installation")
    retired_project = project.with_name("Retired project")
    install.rename(retired_install)
    project.rename(retired_project)
    try:
        player_env = child_environment(os.environ, relocated, dotnet, guards, player=True)
        for key in ("TEMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA"):
            Path(player_env[key]).mkdir(parents=True, exist_ok=True)
        player_env["STANDALONE_TOOL_LOG"] = harness.env["STANDALONE_TOOL_LOG"]
        for profile in ("Development", "Release"):
            folder = relocated / profile
            binary = folder / "StandaloneProbe.exe"
            if not binary.is_file() or not (folder / "dotnet/host/fxr").is_dir():
                raise RuntimeError(f"{profile} exported player or bundled .NET is missing")
            marker = relocated / f"{profile}-probe.txt"
            output = harness.run("player-" + profile, [str(binary), "--background", "--quit-after=8",
                "--prefs-profile=standalone-ci"], env=player_env | {"STANDALONE_PROBE_FILE": str(marker)}, timeout=90)
            require_marker(marker, "V2|" + profile + "|package-ok")
            harness.evidence["players"].append({"profile": profile, "directory": str(folder),
                "marker": str(marker), "markerValue": marker.read_text(encoding="utf-8"),
                "systemDotnetOnPath": False, "originalInstallAndProjectPathsAbsent": True})
            harness.save()
            if any(item in output for item in ERROR_MARKERS):
                raise RuntimeError(f"{profile} runtime emitted an error marker")
        harness.assert_no_tools()
    finally:
        retired_install.rename(install)
        retired_project.rename(project)
    harness.evidence["status"] = "passed" if editor else "partial"
    harness.save()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True, help="Complete installed layout containing EngineSDK and funke.exe")
    parser.add_argument("--dotnet", type=Path, required=True, help="Absolute dotnet.exe from a .NET 10 SDK")
    parser.add_argument("--forbidden-root", type=Path, action="append", default=[])
    parser.add_argument("--work-parent", type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument("--report", type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--with-tween", action="store_true", help="Also test shipped Tween; local managed package is always mandatory")
    parser.add_argument("--skip-editor", action="store_true", help="Partial diagnostic only, exit 2 even when successful")
    args = parser.parse_args(argv)
    if os.name != "nt":
        parser.error("End-to-end execution currently supports Windows; unit tests are portable")
    if not 30 <= args.timeout <= 1800:
        parser.error("--timeout must be between 30 and 1800 seconds")
    source, dotnet = args.install.resolve(), args.dotnet.resolve()
    forbidden = [p.resolve() for p in args.forbidden_root]
    candidate = Path(__file__).resolve().parents[2]
    if (candidate / "Funke-Engine/src").is_dir():
        forbidden.append(candidate)
    try:
        require_external(args.work_parent, forbidden + [source])
        args.work_parent.mkdir(parents=True, exist_ok=True)
    except Exception as error:
        if args.report:
            atomic_write_json(args.report, {"schemaVersion": 1, "status": "failed", "isolation": "host-limited",
                "phase": "work-directory-preflight", "error": str(error)})
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    root = Path(tempfile.mkdtemp(prefix="Funke-Standalone-Prüfung-", dir=args.work_parent))
    report = args.report.resolve() if args.report else root / "evidence.json"
    env = child_environment(os.environ, root, dotnet, root / "tool-guards")
    harness = Harness(root, report, env, args.timeout)
    try:
        if not source.is_dir() or not dotnet.is_file():
            raise ValueError("--install and --dotnet must identify existing inputs")
        verify(harness, source, dotnet, forbidden, args.with_tween, not args.skip_editor)
    except Exception as error:
        harness.evidence.update(status="failed", error=str(error))
        harness.save()
        print(f"FAILED: {error}\nEvidence: {report}", file=sys.stderr)
        return 1
    print(f"{harness.evidence['status'].upper()}: {report}")
    return 2 if args.skip_editor else 0


if __name__ == "__main__":
    raise SystemExit(main())
