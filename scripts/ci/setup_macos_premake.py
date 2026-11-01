#!/usr/bin/env python3
"""Build pinned Premake beta5 sources natively for Apple Silicon; no Rosetta bootstrap.

Upstream: https://premake.github.io/docs/Building-Premake/
Source release: https://github.com/premake/premake-core/releases/tag/v5.0.0-beta5
Tag commit: b9ca81a3fc18195ec53aac6d5f32d12c295adffa
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = "5.0.0-beta5"
SOURCE_ROOT = f"premake-{VERSION}-src"
SOURCE_URL = f"https://github.com/premake/premake-core/releases/download/v{VERSION}/{SOURCE_ROOT}.zip"
SOURCE_SHA256 = "660ba0dd472c06be59da60b78c3abaf8cef10a0a4f3b9ad69d8e34fbd190126b"
MAX_ARCHIVE_BYTES = 32 * 1024 * 1024
MAX_EXTRACTED_BYTES = 128 * 1024 * 1024


class HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        if urllib.parse.urlsplit(new_url).scheme != "https":
            raise ValueError("Premake source redirect must remain HTTPS")
        return super().redirect_request(request, fp, code, message, headers, new_url)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def download(destination: Path) -> None:
    opener = urllib.request.build_opener(HttpsRedirect())
    request = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "Funke-Premake-Bootstrap"})
    with opener.open(request, timeout=120) as source, destination.open("xb") as output:
        count = 0
        while chunk := source.read(1024 * 1024):
            count += len(chunk)
            if count > MAX_ARCHIVE_BYTES:
                raise ValueError("Premake source archive exceeds its size limit")
            output.write(chunk)


def extract_source(archive: Path, destination: Path) -> Path:
    if archive.stat().st_size > MAX_ARCHIVE_BYTES or digest(archive) != SOURCE_SHA256:
        raise ValueError("Premake source SHA-256 does not match the pinned official release")
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > 10000 or sum(entry.file_size for entry in entries) > MAX_EXTRACTED_BYTES:
            raise ValueError("Premake source archive exceeds extraction limits")
        seen = set()
        for entry in entries:
            name = entry.filename.rstrip("/")
            parts = name.split("/")
            kind = stat.S_IFMT(entry.external_attr >> 16)
            if (PurePosixPath(name).is_absolute() or "\\" in name or ":" in name or
                    any(part in ("", ".", "..") for part in parts) or parts[0] != SOURCE_ROOT or
                    kind not in (0, stat.S_IFREG, stat.S_IFDIR) or entry.flag_bits & 1 or
                    name.casefold() in seen):
                raise ValueError(f"Unsafe Premake source archive entry: {entry.filename}")
            seen.add(name.casefold())
        for entry in entries:
            path = destination.joinpath(*PurePosixPath(entry.filename).parts)
            if entry.is_dir():
                path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as input_stream, path.open("xb") as output:
                    shutil.copyfileobj(input_stream, output)
                path.chmod(0o600 | ((entry.external_attr >> 16) & 0o755))
    root = destination / SOURCE_ROOT
    for relative in ("Bootstrap.mak", "premake5.lua", "src/host/premake.h", "src/scripts.c"):
        if not (root / relative).is_file():
            raise ValueError(f"Official Premake source payload is incomplete: {relative}")
    return root


def build_environment() -> dict[str, str]:
    removed = {"CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "MAKEFLAGS", "MFLAGS",
               "GNUMAKEFLAGS", "MAKEFILES", "PREMAKE_PATH", "LD_LIBRARY_PATH", "LIBRARY_PATH"}
    environment = {name: value for name, value in os.environ.items()
                   if name.upper() not in removed and not name.upper().startswith("DYLD_")}
    environment["MACOSX_DEPLOYMENT_TARGET"] = "15.0"
    return environment


def run(command: list[str], directory: Path, environment: dict[str, str], timeout: int = 1200) -> str:
    result = subprocess.run(command, cwd=directory, env=environment, text=True, encoding="utf-8",
                            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Premake bootstrap command failed ({result.returncode}): {command}\n{result.stdout}")
    return result.stdout


def verify_binary(binary: Path, directory: Path, environment: dict[str, str]) -> None:
    architectures = run(["/usr/bin/lipo", "-archs", str(binary)], directory, environment).split()
    if architectures != ["arm64"]:
        raise ValueError(f"Premake must be native arm64, got: {' '.join(architectures)}")
    for line in run(["/usr/bin/otool", "-L", str(binary)], directory, environment).splitlines()[1:]:
        dependency = line.strip().split(" (", 1)[0]
        if not dependency.startswith(("/usr/lib/", "/System/Library/")) or any(part in (".", "..") for part in dependency.split("/")):
            raise ValueError(f"Premake depends on a non-system library: {dependency}")
    version = run([str(binary), "--version"], directory, environment)
    if VERSION not in version:
        raise ValueError(f"Compiled Premake does not report {VERSION}: {version.strip()}")
    help_text = run([str(binary), "--help"], directory, environment)
    if not re.search(r"^\s*gmake\s+", help_text, re.MULTILINE):
        raise ValueError("Compiled Premake has no embedded gmake generator")


def build(archive: Path | None, output: Path, verify_source_only: bool = False) -> None:
    if not verify_source_only and (platform.system() != "Darwin" or platform.machine().lower() != "arm64"):
        raise ValueError("The native Premake bootstrap requires macOS running an arm64 Python process")
    output = output.absolute()
    if output.is_symlink() or (output.exists() and not output.is_file()):
        raise ValueError("Refusing to replace a symlink or non-file Premake output")
    with tempfile.TemporaryDirectory(prefix="funke-premake-arm64-") as temporary:
        directory = Path(temporary)
        if archive is None:
            archive = directory / "source.zip"
            download(archive)
        source = extract_source(archive, directory / "source")
        if verify_source_only:
            print(f"[Premake] Official source archive verified: {SOURCE_SHA256}")
            return
        environment = build_environment()
        print(f"[Premake] Bootstrapping {VERSION} for native arm64 from verified sources.", flush=True)
        # Upstream's osx target cleans only this extracted temporary source tree.
        run(["/usr/bin/make", "-f", "Bootstrap.mak", "osx", "PLATFORM=ARM64", "CONFIG=release",
             "CC=/usr/bin/clang -arch arm64", "CXX=/usr/bin/clang++ -arch arm64",
             "PREMAKE_OPTS=--cc=clang --lib-src=contrib"], source, environment)
        probe = directory / "standalone"
        probe.mkdir()
        binary = probe / "premake5"
        shutil.copyfile(source / "bin/release/premake5", binary)
        binary.chmod(0o755)
        verify_binary(binary, probe, environment)
        (probe / "premake5.lua").write_text('workspace "BootstrapProbe"\nconfigurations {"Release"}\n'
                                            'architecture "ARM64"\nproject "Probe"\nkind "ConsoleApp"\n'
                                            'language "C++"\nfiles {"probe.cpp"}\n', encoding="utf-8")
        (probe / "probe.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        run([str(binary), "gmake"], probe, environment)
        if not (probe / "Makefile").is_file():
            raise ValueError("Standalone Premake did not generate a Makefile")
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix=".premake-arm64-", dir=output.parent, delete=False) as stream:
            replacement = Path(stream.name)
        try:
            shutil.copyfile(binary, replacement)
            replacement.chmod(0o755)
            os.replace(replacement, output)
        finally:
            replacement.unlink(missing_ok=True)
        print(f"[Premake] Installed native {VERSION}: {output}; SHA-256 {digest(output)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Reuse the official source ZIP; the pinned SHA-256 remains mandatory")
    parser.add_argument("--output", type=Path, default=ROOT / "vendor/bin/premake5")
    parser.add_argument("--verify-source-only", action="store_true", help="Check the pinned ZIP and extraction on any host")
    args = parser.parse_args()
    try:
        build(args.archive, args.output, args.verify_source_only)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f"[Premake] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
