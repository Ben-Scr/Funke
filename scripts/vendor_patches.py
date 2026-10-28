#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PATCH_ROOT = REPO_ROOT / "premake" / "patches" / "vendor"


@dataclass(frozen=True)
class PatchedFile:
    path: str
    base_blob: str
    patched_blob: str


@dataclass(frozen=True)
class VendorPatch:
    name: str
    submodule: str
    pinned_commit: str
    patch_file: str
    files: tuple[PatchedFile, ...]


PATCHES = (
    VendorPatch(
        name="Box2D deterministic split-island candidate",
        submodule="External/box2d",
        pinned_commit="8c65dcb91a5e8fbce53492c169cb4460b53b0b54",
        patch_file="box2d-split-island-tie-break.patch",
        files=(
            PatchedFile(
                "src/solver.c",
                "03f0e80e71d6bf9d14b64b87e6bb39bfb466c8e6",
                "759cadc28da742c5261e6fc19a3ba433e35d30ed",
            ),
        ),
    ),
    VendorPatch(
        name="EnTT entity bit split and staged bulk storage",
        submodule="External/entt",
        pinned_commit="1333fa53129e7cfded5a9640c4336a254049917b",
        patch_file="entt-funke-entity-bits.patch",
        files=(
            PatchedFile(
                "src/entt/entity/entity.hpp",
                "7123b780f5cea6b1fa88d94c0f13424589639091",
                "06c6e2887e8c209bb1aeeab12e032f7908f53174",
            ),
            PatchedFile(
                "src/entt/entity/storage.hpp",
                "f6e52dccd40ddeb879e22c7550dba6a04a379f6a",
                "44792fe4f8c9588c469c05280415a5fd6c1b1fde",
            ),
            PatchedFile(
                "src/entt/signal/sigh.hpp",
                "87eff724bc86518290e7d82fdcd9477a54e37f83",
                "9c586afa85573891a41c3a756e4103a146c80f90",
            ),
        ),
    ),
    VendorPatch(
        name="Dear ImGui Funke extensions",
        submodule="External/imgui",
        pinned_commit="dc5c6b3688d27dac69ae9e72f33c8f8fdb6cac69",
        patch_file="imgui-scalar-expression.patch",
        files=(
            PatchedFile(
                "imgui.cpp",
                "cb73b8dcbbfa4f7dfcc91108561e3c84af08ce2f",
                "1160dcb24d7ac83289af67113cd0ce2cffdc5023",
            ),
            PatchedFile(
                "imgui.h",
                "c32a6baf15dea7332cffda09721d272a4a23f673",
                "32bc6b723606e3e0779cabd00e3631b4bdb7e13c",
            ),
            PatchedFile(
                "imgui_widgets.cpp",
                "abcb02f2bbc5c97e968b3ec30a6aeae8f4cd1df9",
                "01f5956656b0bbea6f49f0bbf5fdf318bd9ee516",
            ),
        ),
    ),
    VendorPatch(
        name="ImGuizmo handle outlines",
        submodule="External/ImGuizmo",
        pinned_commit="be8aa4aeab86b402701c8c1df011bd8cd776760b",
        patch_file="imguizmo-handle-outlines.patch",
        files=(
            PatchedFile(
                "src/ImGuizmo.cpp",
                "60bd8d0eb269d1d8b083967d3d340e25fab61c4d",
                "5dbc556ffa3b66ae27801398502c7da2b4c656b7",
            ),
        ),
    ),
)


class VendorPatchError(RuntimeError):
    pass


def run_git(submodule: Path, *args: str) -> str:
    command = ["git", "-C", str(submodule), *args]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise VendorPatchError(
            f"git failed in {submodule.relative_to(REPO_ROOT)}: "
            f"{' '.join(args)}\n{detail}"
        )
    return result.stdout.strip()


def validate_patch_manifest(spec: VendorPatch, patch_path: Path) -> None:
    if not patch_path.is_file():
        raise VendorPatchError(f"Missing vendor patch: {patch_path.relative_to(REPO_ROOT)}")

    text = patch_path.read_text(encoding="utf-8")
    diff_paths = re.findall(r"^diff --git a/(.+) b/(.+)$", text, flags=re.MULTILINE)
    if any(old != new for old, new in diff_paths):
        raise VendorPatchError(f"Renames are not allowed in {spec.patch_file}")

    declared_paths = tuple(old for old, _ in diff_paths)
    expected_paths = tuple(file.path for file in spec.files)
    if declared_paths != expected_paths:
        raise VendorPatchError(
            f"{spec.patch_file} paths are {declared_paths}, expected {expected_paths}"
        )

    index_blobs = re.findall(
        r"^index ([0-9a-f]{40})\.\.([0-9a-f]{40})(?: [0-7]{6})?$",
        text,
        flags=re.MULTILINE,
    )
    expected_blobs = tuple((file.base_blob, file.patched_blob) for file in spec.files)
    if tuple(index_blobs) != expected_blobs:
        raise VendorPatchError(
            f"{spec.patch_file} blob IDs do not match the pinned manifest"
        )


def current_blob(submodule: Path, relative_path: str) -> str:
    file_path = submodule / relative_path
    if not file_path.is_file():
        raise VendorPatchError(
            f"Expected vendor file is missing: {file_path.relative_to(REPO_ROOT)}"
        )
    return run_git(
        submodule,
        "hash-object",
        f"--path={relative_path}",
        "--",
        relative_path,
    )


def changed_paths(submodule: Path) -> tuple[str, ...]:
    output = run_git(submodule, "diff", "--name-only", "HEAD", "--")
    return tuple(line for line in output.splitlines() if line)


def validate_worktree_shape(
    spec: VendorPatch, submodule: Path, expected_changed: tuple[str, ...]
) -> None:
    staged = run_git(submodule, "diff", "--cached", "--name-only", "--")
    if staged:
        raise VendorPatchError(
            f"{spec.submodule} has staged changes; vendor patches must stay unstaged: "
            f"{staged.replace(chr(10), ', ')}"
        )

    untracked = run_git(submodule, "ls-files", "--others", "--exclude-standard")
    if untracked:
        raise VendorPatchError(
            f"{spec.submodule} has unexpected untracked files: "
            f"{untracked.replace(chr(10), ', ')}"
        )

    actual_changed = changed_paths(submodule)
    if actual_changed != expected_changed:
        raise VendorPatchError(
            f"{spec.submodule} changed paths are {actual_changed}, "
            f"expected {expected_changed}"
        )


def check_apply(submodule: Path, patch_path: Path) -> None:
    run_git(
        submodule,
        "apply",
        "--check",
        "--whitespace=error-all",
        str(patch_path),
    )


def verify_one(spec: VendorPatch, mode: str, require_applied: bool) -> str:
    submodule = REPO_ROOT / spec.submodule
    patch_path = PATCH_ROOT / spec.patch_file
    validate_patch_manifest(spec, patch_path)

    if not submodule.is_dir():
        raise VendorPatchError(
            f"Missing submodule {spec.submodule}; run git submodule update --init --recursive"
        )

    gitlink = run_git(REPO_ROOT, "ls-tree", "HEAD", "--", spec.submodule)
    expected_gitlink = f"160000 commit {spec.pinned_commit}\t{spec.submodule}"
    if gitlink != expected_gitlink:
        raise VendorPatchError(
            f"Superproject gitlink for {spec.submodule} does not match "
            f"{spec.pinned_commit}: {gitlink or 'missing'}"
        )

    head = run_git(submodule, "rev-parse", "HEAD")
    if head != spec.pinned_commit:
        raise VendorPatchError(
            f"{spec.submodule} is at {head}, expected pinned commit {spec.pinned_commit}"
        )

    actual_blobs = tuple(current_blob(submodule, file.path) for file in spec.files)
    base_blobs = tuple(file.base_blob for file in spec.files)
    patched_blobs = tuple(file.patched_blob for file in spec.files)
    expected_paths = tuple(file.path for file in spec.files)

    if actual_blobs == patched_blobs:
        validate_worktree_shape(spec, submodule, expected_paths)
        run_git(submodule, "diff", "--check", "HEAD", "--")
        return "applied"

    if actual_blobs == base_blobs:
        validate_worktree_shape(spec, submodule, ())
        check_apply(submodule, patch_path)
        if require_applied:
            raise VendorPatchError(
                f"{spec.name} is reproducible but not applied in {spec.submodule}"
            )
        if mode == "verify":
            return "ready"

        run_git(
            submodule,
            "apply",
            "--whitespace=error-all",
            str(patch_path),
        )
        applied_blobs = tuple(current_blob(submodule, file.path) for file in spec.files)
        if applied_blobs != patched_blobs:
            raise VendorPatchError(
                f"{spec.name} applied, but resulting blob IDs do not match the manifest"
            )
        validate_worktree_shape(spec, submodule, expected_paths)
        run_git(submodule, "diff", "--check", "HEAD", "--")
        return "applied"

    details = ", ".join(
        f"{file.path}={actual} (base {file.base_blob}, patched {file.patched_blob})"
        for file, actual in zip(spec.files, actual_blobs)
    )
    raise VendorPatchError(f"Vendor patch drift in {spec.submodule}: {details}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply or verify the pinned Funke vendor patches without submodule commits."
    )
    parser.add_argument("mode", choices=("apply", "verify"))
    parser.add_argument(
        "--require-applied",
        action="store_true",
        help="In verify mode, reject clean base files even when the patch applies cleanly.",
    )
    args = parser.parse_args()
    if args.require_applied and args.mode != "verify":
        parser.error("--require-applied is only valid with verify")
    return args


def main() -> int:
    args = parse_args()
    try:
        states = [
            verify_one(spec, "verify", args.require_applied)
            for spec in PATCHES
        ]
        if args.mode == "apply":
            states = [
                verify_one(spec, "apply", False) if state == "ready" else state
                for spec, state in zip(PATCHES, states)
            ]
        for spec, state in zip(PATCHES, states):
            print(f"[vendor-patches] {spec.name}: {state}")
    except VendorPatchError as error:
        print(f"[vendor-patches] ERROR: {error}", file=sys.stderr)
        return 2

    action = "verified" if args.mode == "verify" else "applied/verified"
    print(f"[vendor-patches] All pinned vendor patches {action}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
