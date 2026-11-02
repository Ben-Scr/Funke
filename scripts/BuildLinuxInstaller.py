#!/usr/bin/env python3
"""Build the per-user Linux installer from an already staged binary bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import tarfile
import tempfile
import zipfile
from pathlib import Path

try:
    from scripts import sdk_manifest
except ImportError:
    import sdk_manifest

ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP = b'''#!/bin/sh
command -v python3 >/dev/null 2>&1 || { printf '%s\\n' 'Funke setup requires Python 3.10 or newer. Install python3 with your distribution package manager.' >&2; exit 1; }
exec python3 "$0" "$@"
'''


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            value.update(chunk)
    return value.hexdigest()


def build(bundle: Path, output: Path, *, require_release: bool = False) -> Path:
    sdk = sdk_manifest.verify_bundle(bundle)
    if sdk["platform"] != "linux" or sdk["architecture"] not in ("x86_64", "arm64"):
        raise ValueError("A staged Linux x64 or arm64 bundle is required")
    metadata = json.loads((bundle / "BuildMetadata.json").read_text(encoding="utf-8-sig"))
    version = metadata["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("Invalid bundle version")
    if require_release:
        commit, build_id = metadata.get("commit"), metadata.get("buildId")
        if (metadata.get("schemaVersion") != 1 or metadata.get("dirty") is not False
                or metadata.get("identityScope") != "repository" or not isinstance(commit, str)
                or not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", commit)
                or build_id != f"{version}+{commit[:12]}"):
            raise ValueError("A public installer requires a clean bundle with repository-scoped release metadata and a full commit identity")
    output = output.absolute()
    if output.resolve().is_relative_to(bundle.resolve()):
        raise ValueError("Installer output must be outside the input bundle")
    output.mkdir(parents=True, exist_ok=True)
    label = "x64" if sdk["architecture"] == "x86_64" else "arm64"
    destination = output / f"FunkeSetup-{version}-Linux-{label}.run"
    with tempfile.TemporaryDirectory(prefix="funke-linux-package-", dir=output) as directory:
        temporary = Path(directory)
        payload = temporary / "payload.tar.gz"
        sdk_manifest.archive_bundle(bundle, payload)
        files = {}
        executables = {"Funke-Launcher", "Funke-Editor", "Funke-Runtime", "Funke-PackageTool", "funke", "premake5"}
        with tarfile.open(payload, "r:gz") as archive:
            for member in archive:
                if not member.isfile():
                    raise ValueError("The binary payload must contain only materialized regular files")
                stream = archive.extractfile(member)
                assert stream is not None
                files[member.name] = {"sha256": hashlib.file_digest(stream, "sha256").hexdigest()
                                     if hasattr(hashlib, "file_digest") else hashlib.sha256(stream.read()).hexdigest(),
                                     "size": member.size,
                                     "mode": 0o755 if Path(member.name).name in executables or member.mode & 0o111 else 0o644}
        manifest = {"schemaVersion": 1, "product": "dev.funke.Launcher", "version": version,
                    "architecture": sdk["architecture"], "engine": metadata,
                    "payloadSha256": file_digest(payload), "files": files}
        candidate = temporary / destination.name
        candidate.write_bytes(BOOTSTRAP)
        with zipfile.ZipFile(candidate, "a", compression=zipfile.ZIP_STORED) as archive:
            archive.write(ROOT / "installer/linux/install.py", "__main__.py")
            archive.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
            archive.write(payload, "payload.tar.gz")
        candidate.chmod(0o755)
        os.replace(candidate, destination)
    digest = file_digest(destination)
    destination.with_suffix(".run.sha256").write_text(f"{digest}  {destination.name}\n", encoding="utf-8")
    destination.with_suffix(".run.manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "bin/installer")
    parser.add_argument("--require-release", action="store_true")
    args = parser.parse_args()
    machine = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x86_64"
    bundle = args.bundle_dir or ROOT / f"bin/Release-linux-{machine}/Funke-Launcher"
    try:
        print(build(bundle, args.output_dir, require_release=args.require_release))
        return 0
    except (OSError, ValueError, tarfile.TarError) as error:
        parser.exit(1, f"[Linux installer] {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
