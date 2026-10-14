"""Mono runtime packs for platform builders.

Used as a library by Index.Platform.* builders:

    from mono_runtime import fetch_runtime_pack
    pack = fetch_runtime_pack("android-arm64")
    pack.native_dir, pack.managed_dir, pack.include_dir

Packs come from nuget.org and are verified against the SHA-512 the NuGet catalog publishes.
"""

from __future__ import annotations

import base64
import hashlib
import json
import platform
import shutil
import sys
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

from index_platform_sdk import BuilderError, emit_log, platform_cache

# The scripts target net10.0. Browser Mono comes from .NET 11 because .NET 10's wasm runtime is
# built with Emscripten 3.1.56, too old for Dawn's emdawnwebgpu (>= 4.0.3); .NET 11 runs net10 IL.
RUNTIME_PACK_VERSIONS = {
    "android-arm64": "10.0.11",
    "android-x64": "10.0.11",
    "ios-arm64": "10.0.11",
    "iossimulator-arm64": "10.0.11",
    "browser-wasm": "11.0.0-rc.1.26425.128",
}

_NUGET_FLAT = "https://api.nuget.org/v3-flatcontainer"
_NUGET_REGISTRATION = "https://api.nuget.org/v3/registration5-semver1"


@dataclass
class RuntimePack:
    rid: str
    version: str
    root: Path

    @property
    def native_dir(self) -> Path:
        return self.root / "runtimes" / self.rid / "native"

    @property
    def managed_dir(self) -> Path:
        return next((self.root / "runtimes" / self.rid / "lib").glob("net*"))

    @property
    def include_dir(self) -> Path:
        return self.native_dir / "include" / "mono-2.0"

    def framework_assemblies(self) -> list[Path]:
        """The BCL plus System.Private.CoreLib (which ships in native/)."""
        assemblies = sorted(self.managed_dir.glob("*.dll"))
        corelib = self.native_dir / "System.Private.CoreLib.dll"
        if corelib.is_file():
            assemblies.append(corelib)
        return assemblies


def _catalog_sha512(package_id: str, version: str) -> str:
    with urllib.request.urlopen(f"{_NUGET_REGISTRATION}/{package_id}/{version.lower()}.json", timeout=60) as response:
        registration = json.load(response)
    with urllib.request.urlopen(registration["catalogEntry"], timeout=60) as response:
        catalog = json.load(response)
    if catalog.get("packageHashAlgorithm", "").upper() != "SHA512":
        raise BuilderError(f"Unexpected NuGet hash algorithm for {package_id}.")
    return catalog["packageHash"]


def _fetch_verified_package(package_id: str, version: str, root: Path) -> None:
    """Downloads and extracts a nuget.org package into root unless a previous run verified it."""
    marker = root / ".index-verified"
    if marker.is_file():
        return
    emit_log(f"Downloading {package_id} {version} from nuget.org...")
    nupkg = root.parent / f"{package_id}.{version}.nupkg"
    url = f"{_NUGET_FLAT}/{package_id}/{version.lower()}/{package_id}.{version.lower()}.nupkg"
    try:
        with urllib.request.urlopen(url, timeout=300) as response, open(nupkg, "wb") as out:
            shutil.copyfileobj(response, out)
        expected = _catalog_sha512(package_id, version)
    except OSError as error:
        raise BuilderError(f"Could not download {package_id} {version}: {error}") from error

    actual = base64.b64encode(hashlib.sha512(nupkg.read_bytes()).digest()).decode()
    if actual != expected:
        nupkg.unlink(missing_ok=True)
        raise BuilderError(f"{package_id} {version}: SHA-512 mismatch against the NuGet catalog; download discarded.")

    with zipfile.ZipFile(nupkg) as archive:
        for member in archive.infolist():
            target = (root / member.filename).resolve()
            if not str(target).startswith(str(root.resolve())):
                raise BuilderError(f"{package_id}: archive entry escapes the extraction folder: {member.filename}")
        archive.extractall(root)
    nupkg.unlink(missing_ok=True)
    marker.write_text(expected)


def _pinned_version(rid: str, version: str | None) -> str:
    version = version or RUNTIME_PACK_VERSIONS.get(rid)
    if not version:
        raise BuilderError(f"No Mono runtime pack version is pinned for '{rid}'.")
    return version


def fetch_runtime_pack(rid: str, version: str | None = None) -> RuntimePack:
    version = _pinned_version(rid, version)
    root = platform_cache("Mono", f"{rid}-{version}")
    _fetch_verified_package(f"microsoft.netcore.app.runtime.mono.{rid}", version, root)
    return RuntimePack(rid, version, root)


def _host_rid() -> str:
    machine = platform.machine().lower()
    arm = machine in ("arm64", "aarch64")
    if sys.platform == "win32":
        return "win-arm64" if arm else "win-x64"
    if sys.platform == "darwin":
        return "osx-arm64" if arm else "osx-x64"
    return "linux-arm64" if arm else "linux-x64"


def fetch_aot_compiler(rid: str, version: str | None = None) -> Path:
    """Directory holding mono-aot-cross (plus LLVM's opt/llc) for the given target; must match the runtime pack."""
    version = _pinned_version(rid, version)
    host = _host_rid()
    root = platform_cache("Mono", f"aot-cross-{host}-{rid}-{version}")
    _fetch_verified_package(f"microsoft.netcore.app.runtime.aot.{host}.cross.{rid}", version, root)
    return root / "tools"


def package_root() -> Path:
    return Path(__file__).resolve().parents[1]


def native_sources() -> list[Path]:
    """C++ backend compiled into the game binary by the platform's CMake build."""
    return sorted((package_root() / "Native").glob("*.cpp"))
