#!/usr/bin/env python3
"""Build a relocatable Funke.app and drag-to-Applications DMG from a staged macOS bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_manifest
import release_policy

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / "installer" / "macos"
APP_ID = "com.ben-scr.funke"
MACHO_MAGIC = {bytes.fromhex(value) for value in
               ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
VERSION_PATTERN = re.compile(r"([0-9]+)\.([0-9]+)\.([0-9]+)(?:-[0-9A-Za-z.-]+)?\Z")
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")
HOST_EXECUTABLES = {"Funke-Launcher", "Funke-Editor", "Funke-Runtime", "Funke-PackageTool", "funke"}
APP_EXECUTABLES = HOST_EXECUTABLES - {"Funke-PackageTool"}


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, check=True, capture_output=True, text=True, timeout=1200)
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_identity(bundle: Path, require_signing: bool) -> tuple[dict, dict]:
    manifest = sdk_manifest.verify_bundle(bundle)
    if manifest["platform"] != "macosx":
        raise ValueError("A macOS SDK bundle is required")
    metadata = json.loads((bundle / "BuildMetadata.json").read_text(encoding="utf-8-sig"))
    if not isinstance(metadata.get("version"), str) or not VERSION_PATTERN.fullmatch(metadata["version"]):
        raise ValueError("BuildMetadata version must be a filename-safe semantic version")
    if require_signing:
        commit, build_id = metadata.get("commit"), metadata.get("buildId")
        if (metadata.get("schemaVersion") != 1 or metadata.get("dirty") is not False or
                metadata.get("identityScope") != "repository" or not isinstance(commit, str) or
                not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", commit) or
                not isinstance(build_id, str) or build_id != f"{metadata['version']}+{commit[:12]}"):
            raise ValueError("A public installer requires clean repository-scoped release metadata and a full commit identity")
    return manifest, metadata


def extract_payload(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
        seen: set[str] = set()
        for entry in members:
            relative = PurePosixPath(entry.name)
            if (not entry.isfile() or relative.is_absolute() or "\\" in entry.name or ":" in entry.name or
                    any(part in ("", ".", "..") for part in entry.name.split("/")) or
                    entry.name.casefold() in seen):
                raise ValueError(f"Unsafe or duplicate payload member: {entry.name}")
            seen.add(entry.name.casefold())
        for entry in members:
            target = destination.joinpath(*PurePosixPath(entry.name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(entry) as input_stream, target.open("xb") as output:
                shutil.copyfileobj(input_stream, output)
            target.chmod((entry.mode & 0o755) | stat.S_IRUSR | stat.S_IWUSR)


def stage_app(bundle: Path, app: Path, metadata: dict, include_native: bool = False) -> None:
    numeric_version = VERSION_PATTERN.fullmatch(metadata["version"])
    if numeric_version is None:
        raise ValueError("Invalid app version")
    if app.exists() or app.is_symlink():
        raise ValueError("Refusing to overwrite an existing app")
    executable_dir = app / "Contents" / "MacOS"
    resources = app / "Contents" / "Resources"
    executable_dir.mkdir(parents=True)
    resources.mkdir()
    frameworks = app / "Contents" / "Frameworks"
    frameworks.mkdir()
    with tempfile.TemporaryDirectory(prefix="funke-mac-payload-") as temporary:
        archive = Path(temporary) / "payload.tar.gz"
        sdk_manifest.archive_bundle(bundle, archive, include_native=include_native)
        extract_payload(archive, resources)
    for path in resources.rglob("*"):
        if path.is_file() and (path.name in HOST_EXECUTABLES or path.name == "premake5"):
            path.chmod(path.stat().st_mode | 0o111)
    # SDK runtimes are export templates; managed hosts keep their adjacent JSON and assemblies in Resources.
    for entry in list(resources.iterdir()):
        directory = executable_dir if entry.name in APP_EXECUTABLES else frameworks if entry.suffix == ".dylib" else None
        if directory is not None and entry.is_file():
            destination = directory / entry.name
            entry.rename(destination)
            entry.symlink_to(Path("..") / directory.name / entry.name)
    with (app / "Contents" / "Info.plist").open("wb") as output:
        plistlib.dump({
            "CFBundleIdentifier": APP_ID,
            "CFBundleName": "Funke",
            "CFBundleDisplayName": "Funke",
            "CFBundleExecutable": "Funke-Launcher",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": ".".join(numeric_version.groups()),
            "CFBundleVersion": ".".join(numeric_version.groups()),
            "FunkeReleaseVersion": metadata["version"],
            "FunkeBuildID": metadata.get("buildId", metadata["version"]),
            "CFBundleIconFile": "Funke.icns",
            "LSMinimumSystemVersion": "15.0",
            "NSHighResolutionCapable": True,
        }, output, sort_keys=True)
    (app / "Contents" / "PkgInfo").write_bytes(b"APPL????")
    shutil.copyfile(SUPPORT / "Read Me.txt", resources / "Read Me.txt")
    sdk_manifest.verify_bundle(resources, require_binary=not include_native, macos_app=True)


def macho_files(root: Path) -> list[Path]:
    files = []
    for path in root.rglob("*"):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    files.append(path)
    return sorted(files)


def dependencies(output: str) -> list[str]:
    return [line.strip().split(" (", 1)[0] for line in output.splitlines()[1:] if " (" in line]


def is_system_path(path: str) -> bool:
    return path.startswith(SYSTEM_PREFIXES) and not any(part in (".", "..") for part in path.split("/"))


def runtime_paths(output: str) -> list[str]:
    result = []
    lines = output.splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            for candidate in lines[index + 1:index + 4]:
                match = re.fullmatch(r"\s*path (.+) \(offset \d+\)", candidate)
                if match:
                    result.append(match.group(1))
    return result


def validate_minimum_os(output: str, binary: Path) -> None:
    for block in re.split(r"Load command [0-9]+", output):
        if "cmd LC_BUILD_VERSION" in block:
            target = re.search(r"^\s*platform\s+(\S+)", block, re.MULTILINE)
            if target and target[1] not in ("1", "MACOS", "macos"):
                raise ValueError(f"Non-macOS Mach-O platform in {binary}")
            version = re.search(r"^\s*minos\s+([0-9.]+)", block, re.MULTILINE)
        elif "cmd LC_VERSION_MIN_MACOSX" in block:
            version = re.search(r"^\s*version\s+([0-9.]+)", block, re.MULTILINE)
        else:
            continue
        if version and tuple((list(map(int, version[1].split("."))) + [0, 0])[:3]) > (15, 0, 0):
            raise ValueError(f"Native minimum OS exceeds the macOS 15.0 installer baseline: {binary}")


def resolve_dependency(dependency: str, binary: Path, payload: Path, binaries: list[Path]) -> Path:
    if dependency.startswith("@loader_path/"):
        candidate = binary.parent / dependency[len("@loader_path/"):]
    elif dependency.startswith("@executable_path/"):
        candidate = (payload / "MacOS" if (payload / "Info.plist").is_file() else payload) / dependency[len("@executable_path/"):]
    else:
        candidate = binary.parent / Path(dependency).name
    if candidate.resolve() in binaries and candidate.resolve().is_relative_to(payload.resolve()):
        return candidate.resolve()
    directory = binary.parent
    while directory.is_relative_to(payload):
        nearby = directory / Path(dependency).name
        if nearby in binaries:
            return nearby
        directory = directory.parent
    framework = payload / "Frameworks" / Path(dependency).name
    if framework in binaries:
        return framework
    matches = [path for path in binaries if path.name == Path(dependency).name]
    if len(matches) != 1:
        raise ValueError(f"Unresolved or ambiguous library in {binary.name}: {dependency}")
    return matches[0]


def relocate_macho(payload: Path, architecture: str) -> list[Path]:
    payload = payload.resolve()
    resources = payload / "Resources" if (payload / "Info.plist").is_file() else payload
    executables = payload / "MacOS" if resources != payload else payload
    binaries = macho_files(payload)
    for library in payload.rglob("*.dylib"):
        if library.resolve() not in binaries:
            raise ValueError(f"Native library is not Mach-O: {library.relative_to(payload)}")
    for executable in HOST_EXECUTABLES:
        if (resources / executable).resolve() not in binaries:
            raise ValueError(f"Required executable is not Mach-O: {executable}")
    if resources / "EngineSDK" / "premake5" not in binaries:
        raise ValueError("Bundled Premake is not Mach-O")
    for binary in binaries:
        if architecture not in run(["lipo", "-archs", str(binary)]).split():
            raise ValueError(f"{binary.relative_to(payload)} does not support {architecture}")
        own_ids = run(["otool", "-arch", architecture, "-D", str(binary)]).splitlines()[1:]
        own_id = own_ids[0].strip() if own_ids else None
        changes: list[str] = []
        if own_id:
            changes.extend(["-id", "@rpath/" + binary.name])
        for dependency in dependencies(run(["otool", "-arch", architecture, "-L", str(binary)])):
            if dependency == own_id or is_system_path(dependency):
                continue
            target = resolve_dependency(dependency, binary, payload, binaries)
            runtime_root = next((parent for parent in binary.parents
                                 if parent.parent == resources / "EngineSDK" and parent.name.startswith("Runtime-")), None)
            if runtime_root and not target.is_relative_to(runtime_root):
                raise ValueError(f"Export runtime library escapes its own runtime: {binary}: {dependency}")
            replacement = "@loader_path/" + os.path.relpath(target, binary.parent).replace(os.sep, "/")
            if dependency != replacement:
                changes.extend(["-change", dependency, replacement])
        load_commands = run(["otool", "-arch", architecture, "-l", str(binary)])
        validate_minimum_os(load_commands, binary)
        for search_path in runtime_paths(load_commands):
            keep = is_system_path(search_path)
            for token, base in (("@loader_path", binary.parent), ("@executable_path", executables)):
                if search_path == token or search_path.startswith(token + "/"):
                    keep = (base / search_path[len(token):].lstrip("/")).resolve().is_relative_to(payload.resolve())
            if not keep:
                changes.extend(["-delete_rpath", search_path])
        if changes:
            run(["install_name_tool", *changes, str(binary)])
    for binary in binaries:
        own_ids = run(["otool", "-arch", architecture, "-D", str(binary)]).splitlines()[1:]
        own_id = own_ids[0].strip() if own_ids else None
        for dependency in dependencies(run(["otool", "-arch", architecture, "-L", str(binary)])):
            if dependency == own_id or is_system_path(dependency):
                continue
            if not dependency.startswith("@loader_path/"):
                raise ValueError(f"Unrelocated dependency in {binary}: {dependency}")
            target = (binary.parent / dependency[len("@loader_path/"):]).resolve()
            if not target.is_relative_to(payload.resolve()) or not target.is_file():
                raise ValueError(f"Library escapes or is missing from app: {dependency}")
    return binaries


def create_icon(app: Path, temporary: Path) -> None:
    source = app / "Contents/Resources/FunkeAssets/Textures/icon_512.png"
    if not source.is_file():
        raise ValueError("The macOS app needs FunkeAssets/Textures/icon_512.png")
    iconset = temporary / "Funke.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        run(["sips", "-z", str(size), str(size), str(source), "--out", str(iconset / f"icon_{size}x{size}.png")])
        if size < 512:
            run(["sips", "-z", str(size * 2), str(size * 2), str(source), "--out",
                 str(iconset / f"icon_{size}x{size}@2x.png")])
    run(["iconutil", "-c", "icns", str(iconset), "-o", str(app / "Contents/Resources/Funke.icns")])


def sign_app(app: Path, binaries: list[Path], identity: str | None, keychain: str | None = None) -> None:
    signer = identity or "-"
    options = ["--options", "runtime", "--timestamp"] if identity else []
    if keychain:
        options += ["--keychain", keychain]
    entitlements = ["--entitlements", str(SUPPORT / "entitlements.plist")]
    for binary in sorted(binaries, key=lambda path: len(path.parts), reverse=True):
        if binary == app / "Contents/MacOS/Funke-Launcher":
            continue
        host_options = entitlements if binary.name in HOST_EXECUTABLES else []
        run(["codesign", "--force", "--sign", signer, *options, *host_options, str(binary)])
    run(["codesign", "--force", "--sign", signer, *options, *entitlements, str(app)])
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)])


def notarize(path: Path, profile: str, keychain: str | None = None) -> None:
    response = json.loads(run(["xcrun", "notarytool", "submit", str(path), "--keychain-profile", profile,
                               "--wait", "--output-format", "json", *(["--keychain", keychain] if keychain else [])]))
    if response.get("status") != "Accepted":
        raise ValueError(f"Apple notarization was not accepted: {response.get('status', 'missing status')}")


def build(args: argparse.Namespace) -> Path:
    if platform.system() != "Darwin":
        raise ValueError("The DMG build must run on macOS with Xcode command-line tools")
    if args.require_signing and (not args.signing_identity or not args.notary_keychain_profile):
        raise ValueError("Public installers require a Developer ID identity and a notary keychain profile")
    if args.notary_keychain_profile and not args.signing_identity:
        raise ValueError("Notarization requires a Developer ID signing identity")
    if args.signing_identity and not args.signing_identity.startswith("Developer ID Application:"):
        raise ValueError("Use a Developer ID Application signing identity; ad-hoc signing is test-only")
    bundle, output = args.bundle.resolve(), args.output_dir.resolve()
    if output.is_relative_to(bundle):
        raise ValueError("Output directory must be outside the input bundle")
    tag = release_policy.current_tag()
    manifest, metadata = read_identity(bundle, args.require_signing or bool(tag))
    if tag and tag != "v" + metadata["version"]:
        raise ValueError("Release tag does not match the installer bundle version")
    architecture = manifest["architecture"]
    label = "arm64" if architecture == "arm64" else "x64"
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f"FunkeSetup-{metadata['version']}-macOS-{label}.dmg"
    for path in (destination, Path(str(destination) + ".sha256"), Path(str(destination) + ".manifest.json")):
        if path.exists() or path.is_symlink():
            raise ValueError(f"Refusing to replace existing installer output: {path}")
    with tempfile.TemporaryDirectory(prefix="funke-mac-installer-", dir=output) as temporary:
        temporary_path = Path(temporary)
        image_root = temporary_path / "image"
        image_root.mkdir()
        app = image_root / "Funke.app"
        stage_app(bundle, app, metadata, args.include_native_sdk)
        binaries = relocate_macho(app / "Contents", architecture)
        create_icon(app, temporary_path)
        sign_app(app, binaries, args.signing_identity, args.signing_keychain)
        if args.notary_keychain_profile:
            archive = temporary_path / "Funke-notarization.zip"
            run(["ditto", "-c", "-k", "--keepParent", str(app), str(archive)])
            notarize(archive, args.notary_keychain_profile, args.signing_keychain)
            run(["xcrun", "stapler", "staple", str(app)])
            run(["xcrun", "stapler", "validate", str(app)])
            run(["spctl", "--assess", "--type", "execute", "--verbose=2", str(app)])
        shutil.copyfile(SUPPORT / "Read Me.txt", image_root / "Read Me.txt")
        (image_root / "Applications").symlink_to("/Applications", target_is_directory=True)
        with (image_root / "Install .NET 10 SDK.webloc").open("wb") as link:
            plistlib.dump({"URL": "https://dotnet.microsoft.com/download/dotnet/10.0"}, link)
        dmg = temporary_path / destination.name
        run(["hdiutil", "create", "-volname", "Funke", "-srcfolder", str(image_root),
             "-format", "UDZO", "-ov", str(dmg)])
        if args.signing_identity:
            run(["codesign", "--sign", args.signing_identity, "--timestamp",
                 *(["--keychain", args.signing_keychain] if args.signing_keychain else []), str(dmg)])
            run(["codesign", "--verify", "--verbose=2", str(dmg)])
        if args.notary_keychain_profile:
            notarize(dmg, args.notary_keychain_profile, args.signing_keychain)
            run(["xcrun", "stapler", "staple", str(dmg)])
            run(["xcrun", "stapler", "validate", str(dmg)])
        run(["hdiutil", "verify", str(dmg)])
        inventory = {"schemaVersion": 1, "engine": metadata, "platform": "macosx", "architecture": architecture,
                     "nativeSdkIncluded": args.include_native_sdk, "signed": bool(args.signing_identity),
                     "notarized": bool(args.notary_keychain_profile), "sha256": sha256(dmg),
                     "prerequisites": {"dotnetSdk": "10.0", "bundled": False},
                     "files": {path.relative_to(app).as_posix(): sha256(path)
                               for path in sorted(app.rglob("*")) if path.is_file() and not path.is_symlink()},
                     "symlinks": {path.relative_to(app).as_posix(): path.readlink().as_posix()
                                  for path in sorted(app.rglob("*")) if path.is_symlink()}}
        shutil.move(str(dmg), str(destination))
        Path(str(destination) + ".manifest.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        Path(str(destination) + ".sha256").write_text(f"{inventory['sha256']}  {destination.name}\n", encoding="utf-8")
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "bin/installer/macos")
    parser.add_argument("--include-native-sdk", action="store_true")
    parser.add_argument("--signing-identity")
    parser.add_argument("--signing-keychain")
    parser.add_argument("--notary-keychain-profile")
    parser.add_argument("--require-signing", action="store_true")
    args = parser.parse_args()
    if release_policy.policy(release_policy.current_tag())["requiresSigning"]:
        args.require_signing = True
    try:
        result = build(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"[Mac Installer] {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        return 1
    print(f"[Mac Installer] Ready: {result}")
    if not args.notary_keychain_profile:
        print("[Mac Installer] Local test build: not Developer ID signed and notarized for public distribution.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
