#!/usr/bin/env python3
"""Run a trusted export command and retain build provenance for the runtime smoke."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from platform_export_smoke import artifact_identity


def validate_source_run(run: dict, repository: str, expected_commit: str) -> None:
    if (run.get("status") != "completed" or run.get("conclusion") != "success"
            or run.get("head_sha") != expected_commit
            or run.get("head_repository", {}).get("full_name") != repository
            or run.get("event") not in {"push", "workflow_dispatch"}):
        raise ValueError("Export evidence requires a successful trusted push/dispatch run at the expected commit.")


def source_identity(root: Path, expected: str) -> None:
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    def git(*args):
        return subprocess.run(["git", "-C", str(root), *args], check=True,
                              capture_output=True, text=True, timeout=30, **options).stdout.strip()
    if git("rev-parse", "HEAD") != expected:
        raise ValueError("Source checkout does not match the expected commit.")
    if git("status", "--porcelain", "--untracked-files=all", "--ignore-submodules=none"):
        raise ValueError("Source checkout has modified or untracked build inputs.")


def build(args: argparse.Namespace) -> dict:
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    receipt = {"formatVersion": 1, "platform": args.platform, "sourceCommit": args.expected_commit,
               "evidenceLevel": "real_build", "passed": False, "buildToolchain": {}}
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    try:
        if output.is_relative_to(args.source_root.resolve()):
            raise ValueError("Evidence output must be outside the source checkout.")
        source_identity(args.source_root, args.expected_commit)
        identity_args = argparse.Namespace(platform=args.platform, output=output,
                                          entry=args.artifact, apk=args.artifact, app=args.artifact)
        export_root = args.artifact.parent if args.platform == "web" else args.artifact
        if output.is_relative_to(export_root.resolve()):
            raise ValueError("Evidence output must be outside the exported artifact.")
        if export_root.exists():
            raise ValueError("Use a fresh export path; existing exports cannot attest this build.")
        for specification in args.tool:
            name, executable = specification.split("=", 1)
            if not name.strip() or not executable.strip() or name in receipt["buildToolchain"]:
                raise ValueError("Tool names must be nonempty and unique.")
            version = subprocess.run([executable, "--version"], check=True, capture_output=True,
                                     text=True, timeout=30, **options)
            text = (version.stdout + version.stderr).strip()
            if not text:
                raise ValueError(f"Tool {name} returned no version information.")
            receipt["buildToolchain"][name] = text
        with (output / "build.log").open("wb") as log:
            subprocess.run(args.command, cwd=args.source_root, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=args.timeout, **options)
        source_identity(args.source_root, args.expected_commit)
        identity = artifact_identity(identity_args)
        receipt.update(artifactSha256=identity["sha256"], artifact=identity,
                       buildLogSha256=hashlib.sha256((output / "build.log").read_bytes()).hexdigest(),
                       passed=True)
    except Exception as error:
        receipt["error"] = str(error)
    finally:
        (output / "build-evidence.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("web", "android", "ios"), required=True)
    parser.add_argument("--artifact", type=Path, required=True, help="Fresh HTML/APK/app path produced by the command.")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory outside checkout/export.")
    parser.add_argument("--tool", action="append", required=True, help="name=executable; queries --version.")
    parser.add_argument("--timeout", type=int, default=7200)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    if (not args.command or args.timeout <= 0 or not re.fullmatch(r"[0-9a-f]{40}", args.expected_commit)
            or any("=" not in item for item in args.tool)):
        parser.error("Supply a command, positive timeout, full commit hash and name=executable tools.")
    args.source_root = args.source_root.resolve()
    args.artifact = args.artifact.resolve()
    try:
        result = build(args)
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps({"passed": result["passed"], "error": result.get("error"),
                      "receipt": str(args.output.resolve() / "build-evidence.json")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
