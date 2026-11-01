#!/usr/bin/env python3
"""Prepare and remove an isolated CI keychain without logging signing credentials."""
from __future__ import annotations

import argparse
import base64
import json
import os
from pathlib import Path
import platform
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile

REQUIRED = ("MACOS_CERTIFICATE_P12_BASE64", "MACOS_CERTIFICATE_PASSWORD", "MACOS_SIGNING_IDENTITY",
            "APPLE_API_KEY_P8_BASE64", "APPLE_API_KEY_ID", "APPLE_API_ISSUER_ID")


def run(command: list[str]) -> str:
    try:
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
    except (OSError, subprocess.SubprocessError):
        raise RuntimeError(f"{Path(command[0]).name} operation could not complete") from None
    if result.returncode:
        # security/notarytool arguments and diagnostics can contain credentials.
        raise RuntimeError(f"{Path(command[0]).name} operation failed with exit code {result.returncode}")
    return result.stdout


def workspace() -> Path:
    return Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir())).resolve()


def cleanup(state_file: Path) -> None:
    state_file = state_file.resolve()
    directory = state_file.parent
    if (state_file.name != "state.json" or not directory.name.startswith("funke-signing-") or
            directory.parent != workspace()):
        raise ValueError("Signing state is outside the owned temporary directory")
    state = json.loads(state_file.read_text(encoding="utf-8"))
    if state.get("schemaVersion") != 1:
        raise ValueError("Invalid signing state")
    keychain = directory / "funke.keychain-db"
    try:
        run(["security", "list-keychains", "-d", "user", "-s", *state["originalKeychains"]])
    finally:
        try:
            if keychain.exists():
                run(["security", "delete-keychain", str(keychain)])
        finally:
            shutil.rmtree(directory)


def prepare(require: bool) -> None:
    present = [bool(os.environ.get(name)) for name in REQUIRED]
    if not any(present) and not require:
        print("[Mac signing] No signing configuration: local test build only.")
        return
    if not all(present):
        raise ValueError("Signing requires all MACOS_CERTIFICATE_*, MACOS_SIGNING_IDENTITY and APPLE_API_* secrets")
    identity = os.environ["MACOS_SIGNING_IDENTITY"]
    if not identity.startswith("Developer ID Application:") or any(c in identity for c in "\r\n"):
        raise ValueError("MACOS_SIGNING_IDENTITY must name a Developer ID Application identity")
    directory = Path(tempfile.mkdtemp(prefix="funke-signing-", dir=workspace()))
    directory.chmod(0o700)
    state_file = directory / "state.json"
    keychain = directory / "funke.keychain-db"
    original = shlex.split(run(["security", "list-keychains", "-d", "user"]))
    state_file.write_text(json.dumps({"schemaVersion": 1, "originalKeychains": original}), encoding="utf-8")
    state_file.chmod(0o600)
    certificate, api_key = directory / "certificate.p12", directory / "AuthKey.p8"
    try:
        for path, variable in ((certificate, "MACOS_CERTIFICATE_P12_BASE64"), (api_key, "APPLE_API_KEY_P8_BASE64")):
            path.write_bytes(base64.b64decode(os.environ[variable], validate=True))
            path.chmod(0o600)
        password = secrets.token_urlsafe(32)
        run(["security", "create-keychain", "-p", password, str(keychain)])
        run(["security", "set-keychain-settings", "-lut", "21600", str(keychain)])
        run(["security", "unlock-keychain", "-p", password, str(keychain)])
        run(["security", "import", str(certificate), "-k", str(keychain), "-P", os.environ["MACOS_CERTIFICATE_PASSWORD"],
             "-T", "/usr/bin/codesign", "-T", "/usr/bin/security"])
        run(["security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s", "-k", password, str(keychain)])
        run(["security", "list-keychains", "-d", "user", "-s", str(keychain), *original])
        profile = "funke-notary-" + secrets.token_hex(8)
        run(["xcrun", "notarytool", "store-credentials", profile, "--keychain", str(keychain), "--key", str(api_key),
             "--key-id", os.environ["APPLE_API_KEY_ID"], "--issuer", os.environ["APPLE_API_ISSUER_ID"]])
        values = {"FUNKE_MAC_SIGNING_STATE": str(state_file), "FUNKE_MAC_SIGNING_IDENTITY": identity,
                  "FUNKE_MAC_SIGNING_KEYCHAIN": str(keychain), "FUNKE_MAC_NOTARY_KEYCHAIN_PROFILE": profile}
        if not os.environ.get("GITHUB_ENV"):
            raise ValueError("This helper requires GITHUB_ENV to pass its temporary keychain to later CI steps")
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as output:
            for name, value in values.items():
                if any(c in value for c in "\r\n"):
                    raise ValueError("Invalid multiline signing configuration")
                output.write(f"{name}={value}\n")
    except BaseException:
        cleanup(state_file)
        raise
    finally:
        certificate.unlink(missing_ok=True)
        api_key.unlink(missing_ok=True)
    print("[Mac signing] Temporary keychain and notarization profile prepared.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "cleanup"))
    parser.add_argument("--require", action="store_true")
    args = parser.parse_args()
    try:
        if platform.system() != "Darwin":
            raise ValueError("Signing helper requires macOS")
        if args.action == "prepare":
            prepare(args.require)
        elif os.environ.get("FUNKE_MAC_SIGNING_STATE"):
            cleanup(Path(os.environ["FUNKE_MAC_SIGNING_STATE"]))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"[Mac signing] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
