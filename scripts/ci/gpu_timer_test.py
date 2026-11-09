"""Run each GpuTimer regression in a separate process and retain native evidence."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from gpu_render_test import verify_doctest


CASES = {
    "timer-unsupported": "GPU timer unavailable without timestamp feature",
    "timer-map-error": "GPU timer readback failure disables ring and reinitializes",
    "timer-shutdown": "GPU timer shutdown detaches pending callbacks from replacement timer",
    "timer-device-loss": "GPU timer device loss on isolated device drains pending callbacks",
}


def verify_evidence(stage: str, evidence: dict) -> None:
    if evidence.get("realDawnDevice") is not True or evidence.get("uncapturedErrors") != 0:
        raise ValueError(f"{stage}: missing real device or unexpected WebGPU errors")
    pending = 0 if stage == "timer-unsupported" else 3
    samples = 3 if stage in ("timer-map-error", "timer-shutdown") else 0
    if evidence.get("pendingCallbacksBeforeAction") != pending or evidence.get("gpuSamples") != samples:
        raise ValueError(f"{stage}: incorrect callback/sample evidence")
    if stage == "timer-device-loss":
        if (evidence.get("forceLossInjected") is not True or evidence.get("deviceLostCallback") is not True
                or "GFX-045" not in evidence.get("deviceLostMessage", "")):
            raise ValueError("Device-loss callback was not observed on the isolated Dawn device")
    elif evidence.get("forceLossInjected") or evidence.get("deviceLostCallback"):
        raise ValueError(f"{stage}: unexpected device loss")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", choices=("Direct3D12", "Vulkan", "Metal"), required=True)
    parser.add_argument("--adapter", choices=("hardware",), default="hardware")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    args.binary = args.binary.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="timer-", dir=args.output))
    summary = {"status": "failed", "backend": args.backend, "adapterClass": args.adapter, "stages": []}
    try:
        for stage, case in CASES.items():
            directory = run / stage
            directory.mkdir()
            env = {key: value for key, value in os.environ.items() if not key.startswith("FUNKE_TEST_GPU_")}
            env.update(FUNKE_TEST_GPU_TIMER="1", FUNKE_TEST_GPU_REQUIRED="1",
                       FUNKE_TEST_GPU_BACKEND=args.backend, FUNKE_TEST_GPU_ADAPTER=args.adapter,
                       FUNKE_TEST_GPU_EVIDENCE=str(directory))
            command = [str(args.binary), f"--test-case={case}", "--reporters=xml",
                       f"--out={directory / 'doctest.xml'}", "--no-colors=true", "--no-breaks=true"]
            result = {"stage": stage, "command": command, "status": "failed"}
            summary["stages"].append(result)
            with (directory / "process.log").open("w", encoding="utf-8") as log:
                process = subprocess.run(command, cwd=args.binary.parent, env=env,
                                         stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            result["returncode"] = process.returncode
            if process.returncode != 0:
                raise ValueError(f"{stage}: native test exited {process.returncode}")
            verify_doctest(directory / "doctest.xml", case)
            evidence = json.loads((directory / f"{stage}.json").read_text(encoding="utf-8"))
            verify_evidence(stage, evidence)
            result.update(status="passed", evidence=evidence)
        summary["status"] = "passed"
    except (OSError, ValueError, ET.ParseError, subprocess.TimeoutExpired) as error:
        summary["error"] = str(error)
    finally:
        (run / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"GPU timer: {summary['status']} - {run / 'summary.json'}")
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
