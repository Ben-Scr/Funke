#!/usr/bin/env python3
"""Measure the real Launcher OpenProject path through Editor Bridge readiness on Windows."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[3]


def read_json(path: Path):
    try:
        if os.name == "nt":
            import msvcrt
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                          wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
            # Atomic report replacement must remain possible during a polling read.
            handle = kernel.CreateFileW(str(path), 0x80000000, 0x7, None, 3, 0x80, None)
            if handle == wintypes.HANDLE(-1).value:
                return None
            fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
            with os.fdopen(fd, "r", encoding="utf-8-sig") as source:
                return json.load(source)
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def child_environment():
    env = os.environ.copy()
    # Windows PowerShell cannot import PowerShell 7 modules inherited through a native parent.
    env["PSModulePath"] = ";".join(str(path) for path in (
        Path(env["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/Modules",
        Path(env["PROGRAMFILES"]) / "WindowsPowerShell/Modules",
        Path(env["USERPROFILE"]) / "Documents/WindowsPowerShell/Modules"))
    return env


def bridge(instance, command: str, args=None, timeout_ms=2000):
    request = {"id": "project-open-benchmark", "token": instance["token"],
               "command": command, "args": args or {}, "timeoutMs": timeout_ms}
    with open(instance["endpoint"], "r+b", buffering=0) as pipe:
        pipe.write((json.dumps(request) + "\n").encode("utf-8"))
        response = bytearray()
        while b"\n" not in response:
            chunk = pipe.read(65536)
            if not chunk:
                raise RuntimeError("Editor bridge disconnected")
            response.extend(chunk)
            if len(response) > 4 * 1024 * 1024:
                raise RuntimeError("Editor bridge response exceeds its protocol limit")
    return json.loads(response.split(b"\n", 1)[0])


def process_alive(pid: int):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x100000, False, pid)
    if not handle:
        return False
    try:
        return kernel.WaitForSingleObject(handle, 0) == 258
    finally:
        kernel.CloseHandle(handle)


def close_owned_process(pid: int):
    if not process_alive(pid):
        return
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

    @callback_type
    def close_window(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            user.PostMessageW(hwnd, 0x0010, 0, 0)
        return True

    user.EnumWindows(close_window, 0)
    deadline = time.monotonic() + 15
    while process_alive(pid) and time.monotonic() < deadline:
        time.sleep(0.05)
    if process_alive(pid):
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"],
                       capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=False)


def run_once(launcher: Path, project: Path, run_dir: Path, timeout: float, screenshot: bool = False,
             expected_entities: int | None = None, expected_packages: int | None = None):
    run_dir.mkdir(parents=True, exist_ok=False)
    user_data = run_dir / "user-data"
    user_data.mkdir()
    report_path = run_dir / "launcher.json"
    env = child_environment()
    env.update(FUNKE_USER_DATA_ROOT=str(user_data), FUNKE_LAUNCHER_OPEN_PROJECT=str(project),
               FUNKE_LAUNCHER_OPEN_REPORT=str(report_path))
    instance = None
    child_pid = 0
    result = {"project": str(project), "runDirectory": str(run_dir)}
    with (run_dir / "launcher-stdout.log").open("wb") as output:
        launcher_process = subprocess.Popen([str(launcher), "--background"], cwd=launcher.parent,
                                            env=env, stdout=output, stderr=subprocess.STDOUT,
                                            creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + timeout
        try:
            report, report_signature = None, None
            while time.monotonic() < deadline:
                try:
                    info = report_path.stat()
                    signature = (info.st_mtime_ns, info.st_size, info.st_ino)
                    if signature != report_signature:
                        updated = read_json(report_path)
                        if updated is not None:
                            report, report_signature = updated, signature
                except OSError:
                    pass
                if report and report.get("complete"):
                    if not report["success"]:
                        raise RuntimeError(report.get("error") or "Launcher failed to open project")
                    result["launcher"] = report
                    child_pid = report["pid"]
                    break
                if launcher_process.poll() is not None:
                    raise RuntimeError(f"Launcher exited before opening the project: {launcher_process.returncode}")
                time.sleep(0.01)
            if not child_pid:
                raise TimeoutError("Launcher did not finish project preparation")
            descriptor = user_data / "Funke" / "EditorBridge" / f"{child_pid}.json"
            while time.monotonic() < deadline:
                instance = read_json(descriptor)
                if instance:
                    break
                if not process_alive(child_pid):
                    raise RuntimeError("Editor exited before publishing its bridge")
                time.sleep(0.01)
            if not instance:
                raise TimeoutError("Editor did not publish its bridge")
            start_ms = report["startedAtUnixMs"]
            last_status = None
            while time.monotonic() < deadline:
                try:
                    response = bridge(instance, "status", timeout_ms=1000)
                except OSError:
                    if not process_alive(child_pid):
                        raise RuntimeError("Editor exited before becoming ready")
                    time.sleep(0.01)
                    continue
                if response.get("ok"):
                    status = response["result"]
                    observed_at = time.time_ns() / 1_000_000
                    last_status = status
                    ready = status.get("state") == "edit" and status.get("scene") and not status.get("busy")
                    if ready and "editorReadyMs" not in result:
                        result["editorReadyMs"] = observed_at - start_ms
                        result["readyStatus"] = status
                    if ready and not status["scripts"]["compiling"]:
                        result["scriptsReadyMs"] = observed_at - start_ms
                        result["finalStatus"] = status
                        if not status["scripts"]["lastBuildSucceeded"]:
                            raise RuntimeError("Scripts failed to compile: " + status["scripts"]["errors"])
                        if expected_entities is not None and status.get("entityCount") != expected_entities:
                            raise RuntimeError(f"Expected {expected_entities} entities, got {status.get('entityCount')}")
                        break
                time.sleep(0.01)
            else:
                raise TimeoutError(f"Editor did not become ready: {last_status}")
            result["launchToSpawnMs"] = report["spawnedAtUnixMs"] - start_ms
            result["spawnToReadyMs"] = result["editorReadyMs"] - result["launchToSpawnMs"]
            if screenshot:
                shot = bridge(instance, "screenshot", {"path": str(run_dir / "scene.png"), "width": 1280, "height": 720}, timeout_ms=15000)
                if not shot.get("ok") or not (run_dir / "scene.png").is_file():
                    raise RuntimeError(f"Screenshot failed: {shot}")
                result["screenshot"] = str(run_dir / "scene.png")
            result["success"] = True
        except Exception as error:
            result["success"] = False
            result["error"] = str(error)
        finally:
            if not child_pid:
                for descriptor in (user_data / "Funke/EditorBridge").glob("*.json"):
                    candidate = read_json(descriptor)
                    if candidate and Path(candidate["project"]).resolve() == project:
                        child_pid, instance = candidate["pid"], candidate
                        result["recoveredChildPid"] = child_pid
                        break
            if instance and process_alive(child_pid):
                try:
                    for attempt in range(4):
                        try:
                            logs = bridge(instance, "logs", {"tail": 0}, timeout_ms=1000)
                            break
                        except OSError:
                            if attempt == 3:
                                raise
                            time.sleep(0.05 * (attempt + 1))
                    write_json(run_dir / "editor-logs.json", logs)
                    failures = [line for line in logs.get("result", {}).get("lines", [])
                                if line.get("level") in ("error", "critical") or any(text in line.get("message", "")
                                for text in ("Skipping native package", "produced no loadable build"))]
                    if failures:
                        result["success"] = False
                        result["error"] = "Editor startup errors; see editor-logs.json"
                        result["startupErrors"] = failures
                    if expected_packages is not None:
                        expected = f"[PackageHost] Loaded {expected_packages} package(s)."
                        if not any(expected in line.get("message", "") for line in logs.get("result", {}).get("lines", [])):
                            result["success"] = False
                            result["error"] = f"Expected native package count was not logged: {expected_packages}"
                except Exception as error:
                    result["logCaptureError"] = str(error)
                    result["success"] = False
                    result["error"] = "Unable to validate editor startup logs"
            if child_pid:
                close_owned_process(child_pid)
            close_owned_process(launcher_process.pid)
            launcher_process.wait(timeout=20)
            result["launcherExitCode"] = launcher_process.returncode
            native_errors = []
            for log_path in (user_data / "Funke/Logs").glob("*.log"):
                for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
                    if re.search(r"\[(?:error|critical)\]", line) or any(text in line for text in
                            ("Skipping native package", "produced no loadable build")):
                        native_errors.append({"log": str(log_path), "line": line})
            if native_errors:
                result["nativeStartupErrors"] = native_errors
                if result["success"]:
                    result["success"] = False
                    result["error"] = "Native startup errors; see nativeStartupErrors and isolated logs"
            launcher_log = user_data / "Funke/Logs/Funke-Launcher.log"
            log_text = launcher_log.read_text(encoding="utf-8", errors="replace") if launcher_log.exists() else ""
            result["packageCacheHit"] = "Package inputs and outputs unchanged; skipping generation and build." in log_text
            write_json(run_dir / "result.json", result)
    return result


def sha256(path: Path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--launcher", type=Path,
                        default=REPO / "bin/Release-windows-x86_64/Funke-Launcher/Funke-Launcher.exe")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--screenshot", action="store_true", help="Capture a scene screenshot after timing has finished")
    parser.add_argument("--expected-entities", type=int)
    parser.add_argument("--expected-packages", type=int, help="Expected native PackageHost count")
    parser.add_argument("--require-cache-hit", action="store_true", help="Reject recorded runs without a package fast-path log")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This benchmark harness currently uses the Windows named-pipe transport")
    project, launcher, output = args.project.resolve(), args.launcher.resolve(), args.output.resolve()
    if not (project / "project.json").is_file() or not launcher.is_file():
        parser.error("Project or launcher executable does not exist")
    if output.exists():
        parser.error("Use a new output directory so previous measurements stay intact")
    output.mkdir(parents=True)
    metadata = {"project": str(project), "launcher": str(launcher), "runs": args.runs,
                "warmup": args.warmup, "pollIntervalMs": 10,
                "readiness": "Editor Bridge status: edit state, loaded scene, no busy reason",
                "scriptsReadiness": "Editor ready and script build finished successfully",
                "clock": "Launcher system_clock epoch milliseconds to Python time.time_ns",
                "timingResolution": "Includes readiness observation latency: polling interval plus frame/IPC scheduling",
                "reportObservation": "Poll file metadata; read only changed reports with Windows FILE_SHARE_DELETE",
                "userState": "Fresh isolated FUNKE_USER_DATA_ROOT per run; project outputs remain warm",
                "shellEnvironment": "Windows PowerShell module path normalized for native postbuild subprocesses",
                "requireCacheHit": args.require_cache_hit,
                "binaries": {str(p): sha256(p) for p in [launcher, launcher.parent / "Funke-Editor.exe",
                    launcher.parent / "Funke-Engine.dll"] if p.exists()}}
    write_json(output / "metadata.json", metadata)
    samples = []
    for index in range(args.warmup + args.runs):
        warmup = index < args.warmup
        label = f"warmup-{index + 1}" if warmup else f"run-{index - args.warmup + 1}"
        result = run_once(launcher, project, output / label, args.timeout, args.screenshot,
                          args.expected_entities, args.expected_packages)
        if args.require_cache_hit and not warmup and not result["packageCacheHit"]:
            result["success"] = False
            result["error"] = "Required package cache hit was not logged"
            write_json(output / label / "result.json", result)
        print(json.dumps({"run": label, **{key: result[key] for key in
            ("success", "editorReadyMs", "scriptsReadyMs", "launchToSpawnMs", "spawnToReadyMs", "packageCacheHit", "error") if key in result}}), flush=True)
        if not result["success"]:
            return 1
        if not warmup:
            samples.append(result)
    summary = {"samples": len(samples), "metrics": {}}
    for key in ("editorReadyMs", "scriptsReadyMs", "launchToSpawnMs", "spawnToReadyMs"):
        values = [sample[key] for sample in samples]
        summary["metrics"][key] = {"median": statistics.median(values), "min": min(values),
                                    "max": max(values), "values": values}
    write_json(output / "summary.json", summary)
    print(json.dumps(summary), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
