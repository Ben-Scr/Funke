"""Run required native GPU cases and retain independently checked image evidence."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zlib


CASES = {
    "preflight": ("GPU adapter preflight", ()),
    "windows": ("GPU sprite buffer windows preserve alpha masks and all instances", ("sprite-windows-two", "sprite-windows-one", "ui-focus-order")),
    "precision": (
        "GPU large-coordinate hierarchy preserves small relative motion",
        tuple(f"precision-{offset}-{frame}" for offset in (1000, 10000, 1000000, 10000000) for frame in range(3)),
    ),
    "postprocess": (
        "GPU custom post processing preserves empty additive scenes and consecutive uniform uploads",
        ("postprocess-main", "postprocess-additive", "postprocess-red", "postprocess-green"),
    ),
    "basics": ("GPU fixed sprite and mesh depth readbacks", ("sprite2d", "depth3d")),
    "async": (
        "Async asset rendering publishes textures fonts and audio through the Application loop",
        ("async-texture",),
    ),
}


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def png(path: Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def verify_image(root: Path, name: str) -> dict:
    result = json.loads((root / f"{name}.json").read_text(encoding="utf-8"))
    width, height, tolerance = result["width"], result["height"], result["tolerance"]
    if not (0 < width <= 4096 and 0 < height <= 4096 and tolerance == 2):
        raise ValueError(f"{name}: invalid dimensions or changed tolerance")
    actual = (root / f"{name}-actual.rgba").read_bytes()
    reference = (root / f"{name}-reference.rgba").read_bytes()
    if len(actual) != width * height * 4 or len(reference) != len(actual):
        raise ValueError(f"{name}: incomplete readback")
    errors = [abs(a - b) for a, b in zip(actual, reference)]
    mismatches = sum(max(errors[offset:offset + 4]) > tolerance for offset in range(0, len(errors), 4))
    passed = mismatches == 0
    if (result.get("gpuReadback") is not True or result.get("passed") != passed
            or result.get("mismatchedPixels") != mismatches or result.get("maximumChannelError") != max(errors)):
        raise ValueError(f"{name}: native comparison does not match retained bytes")
    diff = bytearray()
    for offset in range(0, len(errors), 4):
        # Alpha differences are visible in all three channels of the opaque diff.
        diff.extend(min(255, max(errors[offset + channel], errors[offset + 3]) * 8) for channel in range(3))
        diff.append(255)
    png(root / f"{name}-actual.png", width, height, actual)
    png(root / f"{name}-reference.png", width, height, reference)
    png(root / f"{name}-diff.png", width, height, bytes(diff))
    return {**result, "actualSha256": hashlib.sha256(actual).hexdigest(),
            "referenceSha256": hashlib.sha256(reference).hexdigest()}


def verify_doctest(path: Path, case: str, expected_failure: bool = False) -> None:
    root = ET.parse(path).getroot()
    cases = [node for node in root.iter("TestCase") if node.get("skipped", "false") != "true"]
    if len(cases) != 1 or cases[0].get("name") != case:
        raise ValueError("Required GPU case was missing, skipped, or not selected exactly once")
    totals = root.find("OverallResultsTestCases")
    assertions = root.find("OverallResultsAsserts")
    expected = ("0", "1") if expected_failure else ("1", "0")
    if totals is None or (totals.get("successes"), totals.get("failures")) != expected:
        raise ValueError("Unexpected doctest case outcome")
    if assertions is None or int(assertions.get("successes", "0")) == 0:
        raise ValueError("GPU case produced no successful assertions")
    failures = int(assertions.get("failures", "0"))
    if failures != (1 if expected_failure else 0) or list(root.iter("Exception")):
        raise ValueError("Unexpected assertion failure or exception")


def verify_adapter(path: Path, expected: str, backend: str, previous: dict | None = None) -> dict:
    adapter = json.loads(path.read_text(encoding="utf-8"))
    version = adapter.get("version", "").split(" / ")
    expected_types = {"CPU"} if expected == "software" else {"DiscreteGPU", "IntegratedGPU"}
    if (adapter.get("initialized") is not True or adapter.get("valid") is not True
            or adapter.get("adapterClass") != expected or adapter.get("expectedClass") != expected
            or not adapter.get("renderer") or len(version) != 3
            or version[1] != backend or version[2] not in expected_types):
        raise ValueError("Adapter identity does not match the required class/backend")
    if previous and any(adapter.get(key) != previous.get(key) for key in ("renderer", "vendor", "version", "driver")):
        raise ValueError("GPU case used a different adapter from preflight")
    return adapter


def child_environment(output: Path, adapter: str, backend: str, negative: bool = False) -> dict:
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("FUNKE_TEST_GPU_") and key not in ("FUNKE_TEST_RENDER_GPU", "FUNKE_TEST_ASYNC_ASSET_GPU")}
    env.update(FUNKE_TEST_RENDER_GPU="1", FUNKE_TEST_ASYNC_ASSET_GPU="1", FUNKE_TEST_GPU_REQUIRED="1",
               FUNKE_TEST_GPU_ADAPTER=adapter, FUNKE_TEST_GPU_BACKEND=backend,
               FUNKE_TEST_GPU_EVIDENCE=str(output), FUNKE_TEST_GPU_NEGATIVE="1" if negative else "0")
    return env


def run_process(command: list[str], root: Path, env: dict, timeout: int) -> int:
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with (root / "process.log").open("w", encoding="utf-8") as log:
        completed = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   timeout=timeout, creationflags=flags, check=False)
    return completed.returncode


def driver_inventory(output: Path, env: dict) -> dict:
    if os.name == "nt":
        command = ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                   "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | ConvertTo-Json"]
    elif sys.platform == "darwin":
        command = ["system_profiler", "SPDisplaysDataType", "-json"]
    else:
        command = ["vulkaninfo", "--summary"]
    try:
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30,
                                creationflags=flags, check=False)
        (output / "driver-inventory.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        return {"command": command, "returncode": result.returncode}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "error": str(error)}


def prepare_assets(binary: Path, source: Path) -> dict:
    destination = binary.parent / "FunkeAssets"
    if not (source / "Textures/Default/Square.png").is_file():
        raise ValueError(f"Required default mask texture missing from repository: {source}")
    copied = []
    for folder in ("Textures/Default", "Fonts"):
        for path in sorted((source / folder).rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            target = destination / relative
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if target.exists():
                if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                    raise ValueError(f"Refusing to overwrite different test assets: {target}")
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
            copied.append({"path": relative.as_posix(), "sha256": digest})
    return {"source": str(source), "destination": str(destination), "files": copied}


def run_case(args: argparse.Namespace, stage: str, summary: dict, negative: bool = False) -> None:
    case, images = CASES[stage]
    directory = args.output / ("negative" if negative else stage)
    directory.mkdir()
    result = {"stage": directory.name, "status": "failed", "case": case}
    summary["stages"].append(result)
    env = child_environment(directory, args.adapter, args.backend, negative)
    command = [str(args.binary), f"--test-case={case}", "--reporters=xml", f"--out={directory / 'doctest.xml'}",
               "--no-colors=true", "--no-breaks=true"]
    result["command"] = command
    started = time.monotonic()
    result["returncode"] = run_process(command, directory, env, args.timeout)
    result["elapsedSeconds"] = round(time.monotonic() - started, 3)
    # Process exit alone is insufficient: a filtered-out or skipped doctest can exit zero.
    adapter = verify_adapter(directory / f"{stage}-adapter.json", args.adapter, args.backend, summary.get("adapter"))
    summary.setdefault("adapter", adapter)
    result["images"] = [verify_image(directory, name) for name in images]
    write_json(directory / "verified-images.json", result["images"])
    verify_doctest(directory / "doctest.xml", case, negative)
    if negative:
        failed = [image["name"] for image in result["images"] if not image["passed"]]
        if result["returncode"] != 1 or failed != ["sprite2d"] or not result["images"][0]["negativeProbe"]:
            raise ValueError("Negative probe did not fail specifically at the intentionally wrong sprite reference")
        result["status"] = "expected-failure"
    else:
        if result["returncode"] != 0 or any(not image["passed"] or image["negativeProbe"] for image in result["images"]):
            raise ValueError(f"{stage}: GPU regression failed")
        result["status"] = "passed"
    log = (directory / "process.log").read_text(encoding="utf-8", errors="replace")
    if any(marker in log for marker in ("WebGPU [Validation]", "WebGPU [OutOfMemory]", "WebGPU [Internal]", "Device lost (")):
        result["status"] = "failed"
        raise ValueError(f"{stage}: WebGPU validation/device error in process log")


def write_gallery(output: Path, summary: dict) -> None:
    parts = ["<!doctype html><meta charset='utf-8'><title>Funke GPU evidence</title>",
             "<style>body{font:16px sans-serif;background:#181b20;color:#eee;margin:32px}img{width:256px;image-rendering:pixelated;border:1px solid #777;margin-right:16px}pre{white-space:pre-wrap}</style>",
             "<h1>Funke GPU evidence</h1><pre>" + html.escape(json.dumps(summary, indent=2)) + "</pre>"]
    for stage in summary["stages"]:
        for image in stage.get("images", []):
            parts.append(f"<h2>{html.escape(stage['stage'] + ' / ' + image['name'])}</h2><p>Reference · Actual · Diff (8×)</p>")
            for kind in ("reference", "actual", "diff"):
                parts.append(f"<img alt='{kind}' src='{stage['stage']}/{image['name']}-{kind}.png'>")
    (output / "index.html").write_text("\n".join(parts), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Fresh evidence directory (refuses stale files)")
    parser.add_argument("--adapter", choices=("hardware", "software"), required=True)
    parser.add_argument("--backend", choices=("Direct3D12", "Vulkan", "Metal"), required=True)
    parser.add_argument("--negative-probe", action="store_true")
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args(argv)
    args.output, args.binary = args.output.resolve(), args.binary.resolve()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Evidence directory must be empty; use a new path for each run")
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {"status": "failed", "adapterClass": args.adapter, "backend": args.backend,
               "platform": platform.platform(), "binary": str(args.binary), "stages": [],
               "negativeProbeRequested": args.negative_probe,
               "vulkanDriverFiles": os.environ.get("VK_DRIVER_FILES", os.environ.get("VK_ICD_FILENAMES", ""))}
    try:
        if not args.binary.is_file():
            raise ValueError(f"Native test binary missing: {args.binary}")
        summary["binarySha256"] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        summary["assets"] = prepare_assets(args.binary, Path(__file__).resolve().parents[2] / "Funke-Runtime/FunkeAssets")
        env = child_environment(args.output, args.adapter, args.backend)
        summary["driverInventory"] = driver_inventory(args.output, env)
        for stage in CASES:
            run_case(args, stage, summary)
        if not summary["adapter"].get("driver") and summary["driverInventory"].get("returncode") != 0:
            raise ValueError("No driver description or host driver inventory available")
        if args.negative_probe:
            run_case(args, "basics", summary, negative=True)
        summary["status"] = "passed"
    except (OSError, ValueError, KeyError, ET.ParseError, subprocess.TimeoutExpired) as error:
        summary["error"] = str(error)
    finally:
        write_json(args.output / "summary.json", summary)
        write_gallery(args.output, summary)
    print(f"GPU {args.adapter}/{args.backend}: {summary['status']} — {args.output / 'summary.json'}")
    if summary.get("error"):
        print(summary["error"], file=sys.stderr)
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
