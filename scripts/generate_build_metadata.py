#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_PROPS = REPO_ROOT / "Version.props"
NATIVE_HEADER = REPO_ROOT / "Index-Engine" / "src" / "Core" / "BuildMetadata.generated.hpp"
# Kept apart from NATIVE_HEADER: that one reaches pch.hpp and may only change on a
# version bump, while the build identity changes with every working-tree edit.
BUILD_IDENTITY_HEADER = REPO_ROOT / "Index-Engine" / "src" / "Core" / "BuildIdentity.generated.hpp"
MANAGED_PROPS = REPO_ROOT / "build" / "BuildMetadata.props"
MANAGED_RC = REPO_ROOT / "build" / "ManagedVersion.rc"
METADATA_JSON = REPO_ROOT / "build" / "BuildMetadata.json"
CHANGELOG = REPO_ROOT / "CHANGELOG.md"
PATCH_MANAGED_SUBMODULES = {
    "External/ImGuizmo",
    "External/entt",
    "External/imgui",
}
SEMVER_PATTERN = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate immutable Index build metadata.")
    parser.add_argument("--tag", help="Require this release tag to equal v<IndexVersion>.")
    parser.add_argument(
        "--require-clean",
        action="store_true",
        help="Fail when tracked or untracked repository files are dirty.",
    )
    parser.add_argument(
        "--github-output",
        type=Path,
        help="Append version, build_id, commit, and artifact names to this GitHub output file.",
    )
    return parser.parse_args()


def run_git(*arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or "unknown git error"
        raise RuntimeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout.rstrip()


def run_git_bytes(*arguments: str) -> bytes:
    result = subprocess.run(
        ["git", *arguments],
        cwd=REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip() or "unknown git error"
        raise RuntimeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout


def repository_status() -> str:
    status = run_git("status", "--porcelain=v1", "--untracked-files=all")
    visible_lines: list[str] = []
    for line in status.splitlines():
        path = line[3:] if len(line) > 3 else ""
        patch_managed = line[:2] in {" M", " m"} and path in PATCH_MANAGED_SUBMODULES
        if not patch_managed:
            visible_lines.append(line)
    return "\n".join(visible_lines)


def calculate_dirty_fingerprint(status: str) -> str:
    digest = hashlib.sha256()
    digest.update(status.encode("utf-8"))
    digest.update(run_git_bytes("diff", "--binary", "--no-ext-diff", "--ignore-submodules=dirty", "HEAD"))
    untracked = run_git_bytes("ls-files", "--others", "--exclude-standard", "-z")
    for raw_path in sorted(path for path in untracked.split(b"\0") if path):
        digest.update(b"\0path\0")
        digest.update(raw_path)
        path = REPO_ROOT / raw_path.decode("utf-8", errors="surrogateescape")
        if path.is_symlink():
            digest.update(os.readlink(path).encode("utf-8", errors="surrogateescape"))
            continue
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
    return digest.hexdigest()[:12]


def read_version() -> tuple[str, int, int, int]:
    try:
        root = ET.parse(VERSION_PROPS).getroot()
    except (OSError, ET.ParseError) as error:
        raise RuntimeError(f"Cannot read {VERSION_PROPS}: {error}") from error

    properties = {
        element.tag: element.text.strip()
        for element in root.iter()
        if element.text and element.text.strip()
    }
    version = properties.get("IndexVersion", "")
    match = SEMVER_PATTERN.fullmatch(version)
    if not match:
        raise RuntimeError(f"IndexVersion must be SemVer; found {version!r} in {VERSION_PROPS}")
    major, minor, patch = (int(match.group(index)) for index in range(1, 4))
    expected_managed_version = f"{major}.{minor}.{patch}.0"
    required_properties = {
        "Version": "$(IndexVersion)",
        "AssemblyVersion": expected_managed_version,
        "FileVersion": expected_managed_version,
        "Product": "Index",
    }
    for name, expected in required_properties.items():
        actual = properties.get(name, "")
        if actual != expected:
            raise RuntimeError(
                f"{name} must be {expected!r} for IndexVersion {version!r}; found {actual!r}."
            )
    return version, major, minor, patch


def xml_escape(value: str) -> str:
    return (
        value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&apos;")
    )


def c_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_if_changed(path: Path, content: str) -> None:
    encoded = content.encode("utf-8")
    if path.is_file() and path.read_bytes() == encoded:
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(encoded)
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    try:
        version, major, minor, patch = read_version()
        commit = run_git("rev-parse", "HEAD")
        if not re.fullmatch(r"[0-9a-fA-F]{40,64}", commit):
            raise RuntimeError(f"Build commit must be a full Git object ID; found {commit!r}")
        commit = commit.lower()

        status = repository_status()
        dirty = bool(status)
        if args.require_clean and dirty:
            raise RuntimeError("Release metadata requires a clean repository checkout.")

        if args.tag and args.tag != f"v{version}":
            raise RuntimeError(
                f"Release tag {args.tag!r} does not match IndexVersion {version!r}; expected v{version}."
            )
        if args.tag:
            tag_commit = run_git("rev-list", "-n", "1", args.tag).lower()
            if tag_commit != commit:
                raise RuntimeError(
                    f"Release tag {args.tag!r} points to {tag_commit}, not build commit {commit}."
                )
            try:
                changelog = CHANGELOG.read_text(encoding="utf-8")
            except OSError as error:
                raise RuntimeError(f"Cannot read release notes at {CHANGELOG}: {error}") from error
            heading = re.compile(rf"^## \[{re.escape(version)}\](?:\s|$)", re.MULTILINE)
            if not heading.search(changelog):
                raise RuntimeError(f"CHANGELOG.md has no release section for {version}.")

        short_commit = commit[:12]
        separator = "." if "+" in version else "+"
        build_id = f"{version}{separator}{short_commit}"
        dirty_fingerprint = ""
        if dirty:
            dirty_fingerprint = calculate_dirty_fingerprint(status)
            build_id += f".dirty.{dirty_fingerprint}"

        archive_name = f"Index-Editor-{version}-Windows-x64.zip"
        symbols_name = f"Index-Editor-{build_id}-Windows-x64-symbols.zip"

        native = (
            "#ifndef IDX_BUILD_METADATA_GENERATED\n"
            "#define IDX_BUILD_METADATA_GENERATED\n\n"
            f"#define IDX_VERSION \"{c_escape(version)}\"\n"
            f"#define IDX_VERSION_MAJOR {major}\n"
            f"#define IDX_VERSION_MINOR {minor}\n"
            f"#define IDX_VERSION_PATCH {patch}\n"
            f"#define IDX_VERSION_RESOURCE {major},{minor},{patch},0\n\n"
            "#endif\n"
        )
        build_identity = (
            "#ifndef IDX_BUILD_IDENTITY_GENERATED\n"
            "#define IDX_BUILD_IDENTITY_GENERATED\n\n"
            f"#define IDX_BUILD_ID \"{c_escape(build_id)}\"\n"
            f"#define IDX_BUILD_COMMIT \"{c_escape(commit)}\"\n"
            f"#define IDX_BUILD_DIRTY {1 if dirty else 0}\n\n"
            "#endif\n"
        )
        managed = (
            "<Project>\n"
            "  <PropertyGroup>\n"
            "    <IndexBuildMetadataGenerated>true</IndexBuildMetadataGenerated>\n"
            f"    <InformationalVersion>{xml_escape(build_id)}</InformationalVersion>\n"
            "    <IncludeSourceRevisionInInformationalVersion>false</IncludeSourceRevisionInInformationalVersion>\n"
            f"    <RepositoryCommit>{xml_escape(commit)}</RepositoryCommit>\n"
            f"    <SourceRevisionId>{xml_escape(commit)}</SourceRevisionId>\n"
            "  </PropertyGroup>\n"
            "</Project>\n"
        )
        managed_rc = (
            '#define IDX_FILE_DESCRIPTION "Index Managed Assembly"\n'
            '#define IDX_INTERNAL_NAME "Index-Managed"\n'
            '#define IDX_ORIGINAL_FILENAME "Index-Managed.dll"\n'
            '#include "../Index-Engine/src/Core/VersionResource.rcinc"\n'
        )
        metadata = {
            "schemaVersion": 1,
            "product": "Index",
            "version": version,
            "buildId": build_id,
            "commit": commit,
            "dirty": dirty,
            "dirtyFingerprint": dirty_fingerprint or None,
            "archiveName": archive_name,
            "symbolsArchiveName": symbols_name,
        }

        write_if_changed(NATIVE_HEADER, native)
        write_if_changed(BUILD_IDENTITY_HEADER, build_identity)
        write_if_changed(MANAGED_PROPS, managed)
        write_if_changed(MANAGED_RC, managed_rc)
        write_if_changed(METADATA_JSON, json.dumps(metadata, indent=2) + "\n")

        if args.github_output:
            output = (
                f"version={version}\n"
                f"build_id={build_id}\n"
                f"commit={commit}\n"
                f"archive_name={archive_name}\n"
                f"symbols_archive_name={symbols_name}\n"
            )
            args.github_output.parent.mkdir(parents=True, exist_ok=True)
            with args.github_output.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(output)

        print(f"[build-metadata] {build_id} ({commit})")
        return 0
    except RuntimeError as error:
        print(f"[build-metadata] ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
