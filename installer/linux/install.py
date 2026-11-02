"""Self-contained installer runtime; embedded in the executable .run archive."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zipfile
from pathlib import Path, PurePosixPath

PRODUCT = "dev.funke.Launcher"
STATE = ".funke-installation.json"
PAYLOAD_STATE = ".funke-payload.json"
MAX_BYTES = 12 * 1024**3
MAX_FILES = 200000


def digest(stream) -> str:
    value = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        value.update(chunk)
    return value.hexdigest()


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return digest(stream)


def safe_relative(name: str) -> Path:
    if (not isinstance(name, str) or not name or "\\" in name or "\x00" in name
            or any(ord(c) < 32 for c in name) or PurePosixPath(name).is_absolute()
            or ":" in name
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"Unsafe payload path: {name!r}")
    return Path(*name.split("/"))


def safe_directory(path: Path, *, create: bool = False) -> Path:
    path = Path(os.path.abspath(path.expanduser()))
    for part in reversed((path, *path.parents)):
        if os.path.lexists(part):
            if part.is_symlink() or not part.is_dir():
                raise ValueError(f"Directory is a link or not a directory: {part}")
        elif create:
            part.mkdir(mode=0o700)
    return path


def atomic_write(path: Path, contents: bytes, mode: int = 0o644) -> None:
    safe_directory(path.parent, create=True)
    if os.path.lexists(path) and (path.is_symlink() or not path.is_file()):
        raise ValueError(f"Refusing linked/non-file destination: {path}")
    temporary = path.with_name(".funke-write-" + uuid.uuid4().hex)
    try:
        with temporary.open("xb") as stream:
            stream.write(contents)
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def validate_manifest(manifest: dict) -> dict:
    if (manifest.get("schemaVersion") != 1 or manifest.get("product") != PRODUCT
            or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", manifest.get("version", ""))
            or manifest.get("architecture") not in ("x86_64", "arm64")
            or not re.fullmatch(r"[a-f0-9]{64}", manifest.get("payloadSha256", ""))):
        raise ValueError("Invalid Funke installer manifest")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files or len(files) > MAX_FILES:
        raise ValueError("Invalid payload file inventory")
    total = 0
    for name, item in files.items():
        relative = safe_relative(name)
        if name in (PAYLOAD_STATE, STATE) or relative.parts[0] in ("current", "versions", "uninstall.py"):
            raise ValueError(f"Reserved installer path: {name}")
        if (not isinstance(item, dict) or type(item.get("size")) is not int or item["size"] < 0
                or item.get("mode") not in (0o644, 0o755)
                or not re.fullmatch(r"[a-f0-9]{64}", item.get("sha256", ""))):
            raise ValueError(f"Invalid payload file metadata: {name}")
        total += item["size"]
        for parent in relative.parents:
            if parent.as_posix() in files:
                raise ValueError(f"Payload file is also used as a directory: {parent}")
    required = {"Funke-Launcher", "Funke-Editor", "Funke-Runtime", "funke", "Funke-PackageTool",
                "EngineSDK/FunkeSDK.json", "EngineSDK/premake5"}
    if total > MAX_BYTES or not required.issubset(files):
        raise ValueError("Payload is oversized or incomplete")
    return manifest


def read_package(package: Path):
    archive = zipfile.ZipFile(package)
    if sorted(archive.namelist()) != ["__main__.py", "manifest.json", "payload.tar.gz"]:
        archive.close()
        raise ValueError("Unexpected or duplicate installer archive entries")
    if archive.getinfo("manifest.json").file_size > 64 * 1024**2:
        archive.close()
        raise ValueError("Oversized installer manifest")
    try:
        manifest = validate_manifest(json.loads(archive.read("manifest.json")))
        with archive.open("payload.tar.gz") as stream:
            if digest(stream) != manifest["payloadSha256"]:
                raise ValueError("Installer payload checksum mismatch; download a fresh copy")
        return archive, manifest
    except Exception:
        archive.close()
        raise


def extract_payload(archive, manifest: dict, destination: Path) -> None:
    seen = set()
    with archive.open("payload.tar.gz") as stream, tarfile.open(fileobj=stream, mode="r|gz") as payload:
        for member in payload:
            relative = safe_relative(member.name)
            if not member.isfile() or member.name in seen or member.name not in manifest["files"]:
                raise ValueError(f"Unexpected payload member: {member.name}")
            item = manifest["files"][member.name]
            if member.size != item["size"]:
                raise ValueError(f"Payload size mismatch: {member.name}")
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            source = payload.extractfile(member)
            if source is None:
                raise ValueError(f"Unreadable payload member: {member.name}")
            value = hashlib.sha256()
            with target.open("xb") as output:
                while block := source.read(1024 * 1024):
                    value.update(block)
                    output.write(block)
            if value.hexdigest() != item["sha256"]:
                raise ValueError(f"Payload checksum mismatch: {member.name}")
            target.chmod(item["mode"])
            seen.add(member.name)
    if seen != set(manifest["files"]):
        raise ValueError("Payload is missing inventoried files")


def load_state(prefix: Path) -> dict:
    state_path = prefix / STATE
    if state_path.is_symlink() or not state_path.is_file():
        raise ValueError(f"Not an installer-managed Funke directory: {prefix}")
    state = json.loads(state_path.read_text(encoding="utf-8"))
    if state.get("product") != PRODUCT or state.get("schemaVersion") != 1:
        raise ValueError("Unrecognized installation state")
    if not isinstance(state.get("versions"), list) or any(not isinstance(name, str) or not re.fullmatch(
            r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?-[a-f0-9]{12}", name) for name in state["versions"]):
        raise ValueError("Invalid installed version paths")
    if len(set(state["versions"])) != len(state["versions"]):
        raise ValueError("Duplicate installed versions")
    for field in ("registered", "pendingPreviousRegistered"):
        values = state.get(field, {})
        if not isinstance(values, dict) or any(not isinstance(name, str)
                or not isinstance(value, str) or not re.fullmatch(r"[a-f0-9]{64}", value)
                for name, value in values.items()):
            raise ValueError("Invalid desktop integration inventory")
    return state


def check_prerequisites(manifest: dict) -> None:
    if platform.system() != "Linux":
        raise ValueError("This installer targets Linux")
    machine = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else platform.machine().lower()
    if machine != manifest["architecture"]:
        raise ValueError(f"This installer targets {manifest['architecture']}; this computer reports {machine}")
    dotnet = shutil.which("dotnet")
    if not dotnet:
        raise ValueError(".NET 10 SDK is required. Install it with your distribution package manager or from "
                         "https://dotnet.microsoft.com/download/dotnet/10.0 and rerun setup. No engine repository is needed.")
    result = subprocess.run([dotnet, "--list-sdks"], capture_output=True, text=True, timeout=30)
    if result.returncode or not any((match := re.fullmatch(r"10\.0\.(\d+)\s+\[.+\]", line.strip()))
                                    and int(match.group(1)) >= 100 for line in result.stdout.splitlines()):
        raise ValueError("A stable .NET 10 SDK is required; a runtime alone is insufficient. "
                         "See https://dotnet.microsoft.com/download/dotnet/10.0")


def integration_files(prefix: Path, manifest: dict) -> dict[Path, bytes]:
    current = prefix / "current"
    bin_dir = Path.home() / ".local/bin"
    data_home = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    if not data_home.is_absolute():
        raise ValueError("XDG_DATA_HOME must be absolute")
    files = {}
    for command, binary in (("funke-launcher", "Funke-Launcher"), ("funke", "funke")):
        if binary not in manifest["files"]:
            continue
        files[bin_dir / command] = ("#!/bin/sh\ncd " + shlex.quote(str(current)) + " || exit 1\nexec "
                                   + shlex.quote(str(current / binary)) + ' "$@"\n').encode()
    executable = str(bin_dir / "funke-launcher")
    if any(c in str(prefix) + executable for c in "\n\r\x00"):
        raise ValueError("Desktop paths cannot contain newlines")
    quoted = '"' + executable.replace("\\", "\\\\").replace('"', '\\"').replace("`", "\\`").replace("$", "\\$").replace("%", "%%") + '"'
    quoted = quoted.replace("\\", "\\\\")
    icon = current / "FunkeAssets/Textures/icon_256.png"
    entry = f"[Desktop Entry]\nType=Application\nName=Funke\nComment=Create games with Funke\nExec=/bin/sh {quoted}\nTerminal=false\nCategories=Development;IDE;\n"
    if "FunkeAssets/Textures/icon_256.png" in manifest["files"]:
        entry += "Icon=" + str(icon).replace("\\", "\\\\") + "\n"
    files[data_home / "applications/dev.funke.Launcher.desktop"] = entry.encode()
    return files


def save_state(prefix: Path, state: dict) -> None:
    atomic_write(prefix / STATE, (json.dumps(state, indent=2) + "\n").encode(), 0o600)


def install(package: Path, prefix: Path, *, skip_prerequisites: bool = False) -> Path:
    archive, manifest = read_package(package)
    with archive:
        if not skip_prerequisites:
            check_prerequisites(manifest)
        prefix = safe_directory(prefix)
        if prefix == Path(prefix.anchor) or prefix == Path.home().absolute():
            raise ValueError("Choose a dedicated installation directory")
        if prefix.exists() and not (prefix / STATE).exists() and any(prefix.iterdir()):
            raise ValueError("Refusing a non-empty directory that is not managed by Funke setup")
        safe_directory(prefix, create=True)
        lock = prefix / ".install.lock"
        with lock.open("x"):
            pass
        try:
            state = load_state(prefix) if (prefix / STATE).exists() else {
                "product": PRODUCT, "schemaVersion": 1, "versions": [], "registered": {}}
            current = prefix / "current"
            if os.path.lexists(current) and (not current.is_symlink() or os.readlink(current) not in
                                            {"versions/" + name for name in state["versions"]}):
                raise ValueError("Refusing an unmanaged current-version link")
            integration = integration_files(prefix, manifest)
            for path, contents in integration.items():
                safe_directory(path.parent)
                if os.path.lexists(path) and (path.is_symlink() or not path.is_file()
                        or file_digest(path) not in {state.get("registered", {}).get(str(path)),
                                                    state.get("pendingPreviousRegistered", {}).get(str(path))}):
                    raise ValueError(f"Refusing to overwrite an existing or modified desktop/command entry: {path}")
            uninstaller = prefix / "uninstall.py"
            if os.path.lexists(uninstaller) and (uninstaller.is_symlink() or not uninstaller.is_file()
                    or file_digest(uninstaller) not in {state.get("uninstallerSha256"), state.get("pendingPreviousUninstallerSha256")}):
                raise ValueError(f"Refusing to overwrite a modified uninstaller: {uninstaller}")
            save_state(prefix, state)
            versions = safe_directory(prefix / "versions", create=True)
            version = manifest["version"] + "-" + manifest["payloadSha256"][:12]
            destination = versions / version
            if destination.exists() or destination.is_symlink():
                safe_directory(destination)
                inventory = destination / PAYLOAD_STATE
                if inventory.is_symlink() or not inventory.is_file() or json.loads(inventory.read_text()) != manifest:
                    raise ValueError("Existing version does not match this installer")
                for name, item in manifest["files"].items():
                    file = destination / safe_relative(name)
                    safe_directory(file.parent)
                    if file.is_symlink() or not file.is_file() or file_digest(file) != item["sha256"]:
                        raise ValueError(f"Installed file was changed; preserve it before reinstalling: {file}")
            else:
                with tempfile.TemporaryDirectory(prefix=".stage-", dir=prefix) as directory:
                    stage = Path(directory)
                    extract_payload(archive, manifest, stage)
                    atomic_write(stage / PAYLOAD_STATE, (json.dumps(manifest, indent=2) + "\n").encode(), 0o600)
                    os.rename(stage, destination)
            if version not in state["versions"]:
                state["versions"].append(version)
            state.setdefault("pendingPreviousRegistered", dict(state["registered"]))
            state.setdefault("pendingPreviousUninstallerSha256", state.get("uninstallerSha256"))
            for path, contents in integration.items():
                state["registered"][str(path)] = hashlib.sha256(contents).hexdigest()
            uninstall = archive.read("__main__.py")
            state["uninstallerSha256"] = hashlib.sha256(uninstall).hexdigest()
            save_state(prefix, state)
            for path, contents in integration.items():
                atomic_write(path, contents, 0o755 if path.name in ("funke", "funke-launcher") else 0o644)
            atomic_write(uninstaller, uninstall, 0o644)
            new_link = prefix / (".current-" + uuid.uuid4().hex)
            try:
                new_link.symlink_to("versions/" + version, target_is_directory=True)
                os.replace(new_link, current)
            finally:
                new_link.unlink(missing_ok=True)
            state.pop("pendingPreviousRegistered", None)
            state.pop("pendingPreviousUninstallerSha256", None)
            save_state(prefix, state)
            return current / "Funke-Launcher"
        finally:
            lock.unlink()


def uninstall(prefix: Path) -> list[Path]:
    prefix = safe_directory(prefix)
    state = load_state(prefix)
    lock = prefix / ".install.lock"
    with lock.open("x"):
        pass
    preserved = []
    try:
        inventories = {}
        for version in state["versions"]:
            directory = safe_directory(prefix / "versions" / version)
            inventory = directory / PAYLOAD_STATE
            if inventory.is_symlink() or not inventory.is_file():
                raise ValueError("Missing installed payload inventory")
            inventories[version] = validate_manifest(json.loads(inventory.read_text(encoding="utf-8")))
        allowed_integration = {}
        for manifest in inventories.values():
            for path, contents in integration_files(prefix, manifest).items():
                allowed_integration.setdefault(str(path), set()).add(hashlib.sha256(contents).hexdigest())
        for field in ("registered", "pendingPreviousRegistered"):
            for name, expected in state.get(field, {}).items():
                if expected not in allowed_integration.get(name, set()):
                    raise ValueError("Installation integration metadata differs from the current user directories")
                safe_directory(Path(name).parent)
        current = prefix / "current"
        if current.is_symlink() and os.readlink(current) in {"versions/" + name for name in state["versions"]}:
            current.unlink()
        elif os.path.lexists(current):
            raise ValueError("Refusing an unmanaged current-version link")
        for version in state["versions"]:
            directory = safe_directory(prefix / "versions" / version)
            inventory = directory / PAYLOAD_STATE
            if inventory.is_symlink() or not inventory.is_file():
                raise ValueError("Missing installed payload inventory")
            manifest = inventories[version]
            for name, item in manifest["files"].items():
                path = directory / safe_relative(name)
                safe_directory(path.parent)
                if path.is_file() and not path.is_symlink() and file_digest(path) == item["sha256"]:
                    path.unlink()
                elif os.path.lexists(path):
                    preserved.append(path)
            inventory.unlink()
            for path in sorted(directory.rglob("*"), key=lambda p: len(p.parts), reverse=True):
                if path.is_dir() and not path.is_symlink():
                    try:
                        path.rmdir()
                    except OSError:
                        pass
            try:
                directory.rmdir()
            except OSError:
                preserved.append(directory)
        for name, expected in state.get("registered", {}).items():
            path = Path(name)
            if not path.is_absolute() or path.name not in ("funke", "funke-launcher", "dev.funke.Launcher.desktop"):
                raise ValueError("Invalid desktop integration path")
            safe_directory(path.parent)
            if path.is_file() and not path.is_symlink() and file_digest(path) in {
                    expected, state.get("pendingPreviousRegistered", {}).get(name)}:
                path.unlink()
            elif os.path.lexists(path):
                preserved.append(path)
        uninstaller = prefix / "uninstall.py"
        if uninstaller.is_file() and not uninstaller.is_symlink() and file_digest(uninstaller) in {
                state.get("uninstallerSha256"), state.get("pendingPreviousUninstallerSha256")}:
            uninstaller.unlink()
        (prefix / STATE).unlink()
        try:
            (prefix / "versions").rmdir()
        except OSError:
            pass
    finally:
        lock.unlink()
    try:
        prefix.rmdir()
    except OSError:
        pass
    return preserved


def main() -> int:
    parser = argparse.ArgumentParser(description="Install Funke for your user, without a source checkout or sudo.")
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local/opt/funke")
    parser.add_argument("--uninstall", action="store_true", help="Remove unchanged installer-owned files; preserve projects and modified files")
    parser.add_argument("--check", action="store_true", help="Verify the embedded payload without installing")
    parser.add_argument("--skip-prerequisites", action="store_true", help="Install without checking .NET/host architecture (for package testing only)")
    args = parser.parse_args()
    try:
        if sys.version_info < (3, 10):
            raise ValueError("Python 3.10 or newer is required")
        if args.uninstall:
            preserved = uninstall(args.prefix)
            print("Funke uninstalled. Projects and preferences were retained.")
            for path in preserved:
                print(f"Preserved modified or unowned content: {path}")
        elif args.check:
            archive, manifest = read_package(Path(sys.argv[0]))
            with archive, tempfile.TemporaryDirectory(prefix="funke-verify-") as directory:
                extract_payload(archive, manifest, Path(directory))
            print("Installer payload verified.")
        else:
            launcher = install(Path(sys.argv[0]), args.prefix, skip_prerequisites=args.skip_prerequisites)
            print(f"Funke installed: {launcher}\nOpen Funke in your applications menu or run ~/.local/bin/funke-launcher.")
            print(f"Uninstall: python3 {shlex.quote(str(args.prefix / 'uninstall.py'))} --uninstall --prefix {shlex.quote(str(args.prefix))}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"[Funke setup] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
