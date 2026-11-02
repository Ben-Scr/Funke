#!/usr/bin/env python3
"""Fetch pinned Dawn sources with checked Git operations and recoverable checkouts."""

from __future__ import annotations

import argparse
import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DAWN_URL = "https://dawn.googlesource.com/dawn"
MINIMAL = [
    "third_party/abseil-cpp", "third_party/directx-shader-compiler/src",
    "third_party/directx-headers/src", "third_party/jinja2", "third_party/EGL-Registry/src",
    "third_party/OpenGL-Registry/src", "third_party/libprotobuf-mutator/src",
    "third_party/protobuf", "third_party/markupsafe", "third_party/glslang/src",
    "third_party/spirv-headers/src", "third_party/spirv-tools/src", "third_party/vulkan-headers/src",
    "third_party/vulkan-loader/src", "third_party/vulkan-utility-libraries/src",
    "third_party/webgpu-headers/src",
]
FULL = MINIMAL + ["third_party/glfw3/src", "third_party/google_benchmark/src", "third_party/googletest"]


class BootstrapError(RuntimeError):
    pass


def git(path: Path, *args: str, check: bool = True, env: dict | None = None) -> subprocess.CompletedProcess:
    result = subprocess.run(["git", "-C", str(path), *args], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", env=env)
    if check and result.returncode:
        raise BootstrapError(f"git {' '.join(args)} in {path} exited {result.returncode}:\n"
                             f"{result.stdout}{result.stderr}")
    return result


def probe(path: Path, *args: str, env: dict | None = None) -> bool:
    result = git(path, *args, check=False, env=env)
    if result.returncode not in (0, 1):
        raise BootstrapError(f"git {' '.join(args)} in {path}: {result.stderr}")
    return result.returncode == 0


def applied_patches(path: Path, patches: list[Path]) -> list[Path]:
    return [patch for patch in patches if git(path, "apply", "--reverse", "--check", str(patch), check=False).returncode == 0]


def check_changes(path: Path, patches: list[Path]) -> list[Path]:
    if not probe(path, "diff", "--cached", "--quiet", "--ignore-submodules=all", "HEAD"):
        raise BootstrapError(f"Staged local changes in {path}; preserve/commit them before setup")
    applied = applied_patches(path, patches)
    with tempfile.TemporaryDirectory(prefix="funke-dawn-index-") as temporary:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        git(path, "read-tree", "HEAD", env=env)
        for patch in applied:
            git(path, "apply", "--cached", str(patch), env=env)
        if not probe(path, "diff", "--quiet", "--ignore-submodules=all", env=env):
            raise BootstrapError(f"Unexpected local changes in {path}; only versioned Funke patches are allowed. "
                                 "No local files were reset or removed.")
    return applied


def ensure_checkout(path: Path, url: str, revision: str, patches: list[Path] | None = None) -> bool:
    patches = patches or []
    try:
        path.mkdir(parents=True, exist_ok=True)
        if not (path / ".git").exists():
            # An artifact-only cache must be moved aside by the cache validator before fetching sources.
            if any(path.iterdir()):
                raise BootstrapError(f"{path} is nonempty but has no Git metadata; refusing to adopt unrelated files")
            git(path, "init", "--quiet")
        top = Path(git(path, "rev-parse", "--show-toplevel").stdout.strip()).resolve()
        if top != path.resolve():
            raise BootstrapError(f"Expected repository root {path}, found {top}")
        remotes = git(path, "remote").stdout.splitlines()
        if "origin" in remotes:
            origin = git(path, "remote", "get-url", "origin").stdout.strip()
            if origin.rstrip("/") != url.rstrip("/"):
                raise BootstrapError(f"Unexpected origin {origin!r}; expected {url!r}")
        else:
            git(path, "remote", "add", "origin", url)
        head = git(path, "rev-parse", "--verify", "HEAD", check=False)
        if head.returncode and head.returncode != 128:
            raise BootstrapError(head.stderr)
        applied = check_changes(path, patches) if head.returncode == 0 else []
        target = git(path, "rev-parse", "--verify", f"{revision}^{{commit}}", check=False)
        if target.returncode:
            git(path, "fetch", "--depth=1", url, revision)
            target = git(path, "rev-parse", "--verify", f"{revision}^{{commit}}")
        changed = head.returncode != 0 or head.stdout.strip() != target.stdout.strip()
        if changed:
            for patch in reversed(applied):
                git(path, "apply", "--reverse", str(patch))
            try:
                git(path, "-c", "advice.detachedHead=false", "checkout", "--no-overwrite-ignore", "--detach", target.stdout.strip())
            except BootstrapError:
                for patch in applied:
                    git(path, "apply", str(patch))
                raise
        for patch in patches:
            if patch not in applied or changed:
                if git(path, "apply", "--reverse", "--check", str(patch), check=False).returncode:
                    git(path, "apply", "--check", str(patch))
                    git(path, "apply", str(patch))
        return changed
    except BootstrapError as error:
        raise BootstrapError(f"Dependency: {path}\nRepository: {url}\nRequired revision: {revision}\n{error}") from error


def read_manifest(path: Path) -> dict[str, tuple[str, str]]:
    assignments = {node.targets[0].id: node.value for node in ast.parse(path.read_text(encoding="utf-8")).body
                   if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)}
    variables = ast.literal_eval(assignments["vars"])

    def value(node):
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.Dict):
            return {value(key): value(item) for key, item in zip(node.keys, node.values)}
        if isinstance(node, (ast.List, ast.Tuple)):
            return [value(item) for item in node.elts]
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "Var":
            return variables[value(node.args[0])]
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return value(node.left) + value(node.right)
        raise BootstrapError(f"Unsupported DEPS expression in {path}: {ast.dump(node)}")

    dependencies = value(assignments["deps"])
    return {name: tuple(entry["url"].format(**variables).rsplit("@", 1))
            for name, entry in dependencies.items() if isinstance(entry, dict) and "url" in entry}


def fetch_dependencies(root: Path, required: list[str]) -> None:
    manifest = read_manifest(root / "DEPS")
    for name in required:
        if name not in manifest:
            raise BootstrapError(f"Required dependency {name} missing from {root / 'DEPS'}")
        url, revision = manifest[name]
        path = root / name
        if not path.resolve().is_relative_to(root.resolve()):
            raise BootstrapError(f"Dependency leaves source root: {name}")
        print(f"[Dawn] {name} @ {revision}", flush=True)
        ensure_checkout(path, url, revision)
        # Verifies every tracked source, including files accidentally omitted from a partial checkout.
        if not git(path, "ls-files").stdout.strip():
            raise BootstrapError(f"Dependency {name} has no tracked sources at {revision}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "External/dawn")
    parser.add_argument("--revision", default=os.environ.get("DAWN_REVISION") or
                        (ROOT / "scripts/dawn/dawn-revision.txt").read_text().strip())
    parser.add_argument("--minimal", action="store_true")
    parser.add_argument("--deps-only", action="store_true")
    args = parser.parse_args()
    try:
        if not args.deps_only:
            ensure_checkout(args.directory, DAWN_URL, args.revision,
                            sorted((ROOT / "scripts/dawn/patches").glob("*.patch")))
        fetch_dependencies(args.directory, MINIMAL if args.minimal else FULL)
        for file in ("CMakeLists.txt", "third_party/abseil-cpp/CMakeLists.txt"):
            if not (args.directory / file).is_file():
                raise BootstrapError(f"Required source missing before CMake: {args.directory / file}")
    except (BootstrapError, OSError, ValueError, KeyError) as error:
        print(f"[Dawn ERROR] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
