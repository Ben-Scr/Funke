#!/usr/bin/env python3
"""Run an exported Funke game on an explicitly selected device or real browser."""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


ERROR_PATTERN = re.compile(
    r"\[(?:error|critical)\]|FATAL EXCEPTION|Fatal signal|Unhandled exception|Exception in "
    r"|interop mismatch|Failed to initialize the .NET runtime|Aborted\(", re.I)
IDENTIFIER = re.compile(r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+")


def run(command: list[str], *, timeout: float = 30, check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=timeout, **options, **kwargs)
    if check and result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}")
    return result


def require_tool(name: str) -> str:
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f"Required tool is unavailable: {name}")
    return found


def identifier(value: str) -> str:
    if not IDENTIFIER.fullmatch(value):
        raise argparse.ArgumentTypeError("Use a dotted application identifier, for example dev.funke.exportsmoke.")
    return value


def pixel(value: str) -> tuple[float, float, int, int, int]:
    try:
        x, y, red, green, blue = value.split(",")
        result = (float(x), float(y), int(red), int(green), int(blue))
        if not (0 <= result[0] <= 1 and 0 <= result[1] <= 1 and all(0 <= c <= 255 for c in result[2:])):
            raise ValueError()
        return result
    except ValueError as error:
        raise argparse.ArgumentTypeError("Expected normalized x,y and byte RGB: 0.5,0.5,255,0,255") from error


def check_log(log: str, markers: list[str]) -> bool:
    failure = ERROR_PATTERN.search(log)
    if failure:
        line_start = log.rfind("\n", 0, failure.start()) + 1
        line_end = log.find("\n", failure.end())
        raise RuntimeError("Game reported an error: " + log[line_start:line_end if line_end >= 0 else None])
    return all(marker in log for marker in markers)


def check_pixels(screenshot: Path, probes: list[tuple[float, float, int, int, int]], tolerance: int) -> None:
    from PIL import Image

    with Image.open(screenshot) as source:
        image = source.convert("RGB")
        if image.width < 2 or image.height < 2:
            raise RuntimeError("The game screenshot has no usable render surface.")
        for x, y, *expected in probes:
            actual = image.getpixel((round(x * (image.width - 1)), round(y * (image.height - 1))))
            if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
                raise RuntimeError(f"Rendered pixel at ({x}, {y}) is {actual}; expected {tuple(expected)} ±{tolerance}.")


class WebHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *_args) -> None:
        pass


def web(args: argparse.Namespace, output: Path) -> None:
    entry = args.entry.resolve()
    if not entry.is_file():
        raise FileNotFoundError(entry)
    node = require_tool(args.node)
    handler = functools.partial(WebHandler, directory=str(entry.parent))
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            from urllib.parse import quote

            options = {"url": f"http://127.0.0.1:{server.server_port}/{quote(entry.name)}",
                       "output": str(output), "timeout": args.timeout, "settle": args.settle,
                       "markers": args.expect_log, "executable": str(args.browser_executable) if args.browser_executable else None,
                       "channel": args.browser_channel, "headed": args.headed,
                       "browserArgs": args.browser_arg, "width": args.width, "height": args.height}
            result = run([node, str(Path(__file__).with_suffix(".cjs"))], input=json.dumps(options),
                         timeout=args.timeout + args.settle + 30, check=False)
            (output / "browser-driver.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode:
                raise RuntimeError("Browser execution failed: " + result.stderr.strip())
            if not check_log((output / "game.log").read_text(encoding="utf-8"), args.expect_log):
                raise RuntimeError("The browser did not report every required script marker.")
        finally:
            server.shutdown()
            thread.join(timeout=5)


def android(args: argparse.Namespace, output: Path) -> None:
    if not args.apk.is_file():
        raise FileNotFoundError(args.apk)
    adb = [require_tool(args.adb), "-s", args.serial]
    if run(adb + ["get-state"]).stdout.strip() != "device":
        raise RuntimeError("The selected Android device is not authorized and online.")
    existing = run(adb + ["shell", "pm", "path", args.package], check=False)
    if "package:" in existing.stdout and not args.replace:
        raise RuntimeError("This package is already installed. Use a dedicated test package or explicitly pass --replace.")
    installed = run(adb + ["install"] + (["-r"] if args.replace else []) + [str(args.apk.resolve())], timeout=120)
    if "Success" not in installed.stdout:
        raise RuntimeError("APK installation did not report Success: " + installed.stdout + installed.stderr)
    activity = run(adb + ["shell", "cmd", "package", "resolve-activity", "--brief", args.package]).stdout.strip().splitlines()[-1]
    if not re.fullmatch(re.escape(args.package) + r"/[A-Za-z0-9_.$]+", activity):
        raise RuntimeError("The APK does not provide a launchable activity for " + args.package)
    run(adb + ["shell", "am", "force-stop", args.package])
    since = run(adb + ["shell", "date", "+%s"]).stdout.strip() + ".000"
    try:
        launched = run(adb + ["shell", "am", "start", "-W", "-n", activity])
        (output / "launch.log").write_text(launched.stdout + launched.stderr, encoding="utf-8")
        deadline = time.monotonic() + args.timeout
        ready_since = None
        pid = ""
        while time.monotonic() < deadline:
            current_pid = run(adb + ["shell", "pidof", args.package], check=False).stdout.strip().split()
            if not current_pid:
                raise RuntimeError("The exported Android game exited before validation completed.")
            if pid and pid != current_pid[0]:
                raise RuntimeError("The exported Android game restarted during validation.")
            pid = current_pid[0]
            log = run(adb + ["logcat", "-d", "-v", "brief", "-T", since, "--pid=" + pid]).stdout
            (output / "game.log").write_text(log, encoding="utf-8")
            if check_log(log, args.expect_log):
                ready_since = ready_since or time.monotonic()
                if time.monotonic() - ready_since >= args.settle:
                    break
            time.sleep(0.25)
        else:
            raise RuntimeError("Timed out waiting for Android script markers: " + repr(args.expect_log))
        options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        shot = subprocess.run(adb + ["exec-out", "screencap", "-p"], capture_output=True, timeout=15, **options)
        if shot.returncode:
            raise RuntimeError("Android screenshot capture failed.")
        (output / "game.png").write_bytes(shot.stdout)
    finally:
        run(adb + ["shell", "am", "force-stop", args.package], check=False)


def ios(args: argparse.Namespace, output: Path) -> None:
    xcrun = require_tool("xcrun")
    info = args.app / "Info.plist"
    if not info.is_file():
        raise FileNotFoundError(info)
    with info.open("rb") as stream:
        if plistlib.load(stream).get("CFBundleIdentifier") != args.bundle_id:
            raise RuntimeError("The app's bundle identifier does not match --bundle-id.")
    devices = json.loads(run([xcrun, "simctl", "list", "devices", "booted", "--json"]).stdout)["devices"]
    if not any(device["udid"] == args.device for group in devices.values() for device in group):
        raise RuntimeError("Select an already booted simulator by its exact UDID.")
    existing = run([xcrun, "simctl", "get_app_container", args.device, args.bundle_id], check=False)
    if existing.returncode == 0 and not args.replace:
        raise RuntimeError("This app is already installed. Use a dedicated test bundle or explicitly pass --replace.")
    run([xcrun, "simctl", "install", args.device, str(args.app.resolve())], timeout=120)
    log_path = output / "game.log"
    with log_path.open("wb") as log_file:
        process = subprocess.Popen([xcrun, "simctl", "launch", "--terminate-running-process", "--console",
                                    args.device, args.bundle_id], stdout=log_file, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.timeout
            ready_since = None
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("The iOS simulator game exited before validation completed.")
                log = log_path.read_text(encoding="utf-8", errors="replace")
                if check_log(log, args.expect_log):
                    ready_since = ready_since or time.monotonic()
                    if time.monotonic() - ready_since >= args.settle:
                        break
                time.sleep(0.25)
            else:
                raise RuntimeError("Timed out waiting for iOS script markers: " + repr(args.expect_log))
            run([xcrun, "simctl", "io", args.device, "screenshot", str(output / "game.png")])
        finally:
            run([xcrun, "simctl", "terminate", args.device, args.bundle_id], check=False)
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory (must not exist).")
    parser.add_argument("--expect-log", action="append", required=True, help="Marker emitted by game code after successful startup; repeatable.")
    parser.add_argument("--expect-pixel", type=pixel, action="append", required=True, help="Render assertion: normalized x,y,R,G,B; repeatable.")
    parser.add_argument("--tolerance", type=int, default=12)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--settle", type=float, default=2, help="Keep checking for failures after the markers appear.")
    commands = parser.add_subparsers(dest="platform", required=True)
    browser = commands.add_parser("web")
    browser.add_argument("--entry", type=Path, required=True)
    browser.add_argument("--node", default="node")
    browser.add_argument("--browser-executable", type=Path)
    browser.add_argument("--browser-channel", choices=["chrome", "msedge", "chromium"])
    browser.add_argument("--browser-arg", action="append", default=[])
    browser.add_argument("--headed", action="store_true")
    browser.add_argument("--width", type=int, default=960)
    browser.add_argument("--height", type=int, default=540)
    device = commands.add_parser("android")
    device.add_argument("--apk", type=Path, required=True)
    device.add_argument("--adb", default="adb")
    device.add_argument("--serial", required=True)
    device.add_argument("--package", type=identifier, required=True)
    device.add_argument("--replace", action="store_true")
    simulator = commands.add_parser("ios")
    simulator.add_argument("--app", type=Path, required=True)
    simulator.add_argument("--device", required=True, help="Exact UDID of an already booted simulator.")
    simulator.add_argument("--bundle-id", type=identifier, required=True)
    simulator.add_argument("--replace", action="store_true")
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.settle < 0 or not 0 <= args.tolerance <= 255:
        parser.error("timeout must be positive, settle nonnegative, and tolerance between 0 and 255.")
    if any(not marker.strip() for marker in args.expect_log):
        parser.error("Log markers must not be empty.")
    if args.platform == "web" and (args.width < 2 or args.height < 2):
        parser.error("Browser dimensions must be at least 2 pixels.")
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    output = args.output.resolve()
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("Pixel validation requires Pillow: python -m pip install Pillow", file=sys.stderr)
        return 2
    try:
        output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        print(f"Cannot create a new evidence directory: {error}", file=sys.stderr)
        return 2
    result = {"platform": args.platform, "passed": False, "markers": args.expect_log,
              "pixels": args.expect_pixel, "screenshot": "game.png"}
    try:
        {"web": web, "android": android, "ios": ios}[args.platform](args, output)
        check_pixels(output / "game.png", args.expect_pixel, args.tolerance)
        result["passed"] = True
        print(f"Export smoke passed. Evidence: {output}")
        return 0
    except Exception as error:
        result["error"] = str(error)
        print(f"Export smoke failed: {error}\nEvidence: {output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
