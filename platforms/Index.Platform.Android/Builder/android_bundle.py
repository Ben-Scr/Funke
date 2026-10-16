"""Android App Bundle (.aab) for Google Play, from the same parts as the APK.

Play only accepts bundles for new apps. aapt2 links the resources in protobuf format, the parts go into a base
module zip, bundletool turns that into the bundle and jarsigner signs it with the upload key.
"""

from __future__ import annotations

import hashlib
import json
import os
import urllib.request
import zipfile
from pathlib import Path

from index_platform_sdk import BuilderError, emit_log, platform_cache, run

BUNDLETOOL_VERSION = "1.18.3"
_BUNDLETOOL_SHA256 = "a099cfa1543f55593bc2ed16a70a7c67fe54b1747bb7301f37fdfd6d91028e29"
_BUNDLETOOL_URL = f"https://github.com/google/bundletool/releases/download/{BUNDLETOOL_VERSION}/bundletool-all-{BUNDLETOOL_VERSION}.jar"

# Native libraries stay compressed and are extracted on install, as in the APK (android:extractNativeLibs="true"):
# Mono loads the AOT images from the extracted library folder.
_BUNDLE_CONFIG = {
    "optimizations": {
        "uncompressNativeLibraries": {"enabled": False},
        "splitsConfig": {"splitDimension": [
            {"value": "ABI", "negate": False},
            {"value": "SCREEN_DENSITY", "negate": False},
            {"value": "LANGUAGE", "negate": False},
            {"value": "TEXTURE_COMPRESSION_FORMAT", "negate": True},
        ]},
    },
}


def find_bundletool(sdk: Path, option: str) -> Path:
    if option:
        path = Path(option)
        if not path.is_file():
            raise BuilderError(f"bundletoolPath does not exist: {path}")
        return path
    cached = platform_cache("Android", "tools") / f"bundletool-all-{BUNDLETOOL_VERSION}.jar"
    if cached.is_file():
        return cached
    emit_log(f"Downloading bundletool {BUNDLETOOL_VERSION} (32 MB, once)...")
    try:
        request = urllib.request.Request(_BUNDLETOOL_URL, headers={"User-Agent": "Index-Platform-Android"})
        with urllib.request.urlopen(request, timeout=600) as response:
            data = response.read()
    except OSError as error:
        bundled = sorted(sdk.parent.glob("Tools/bundletool-all-*.jar"), reverse=True)  # Unity's Android player has one
        if bundled:
            emit_log(f"bundletool download failed ({error}); using {bundled[0]}", "warn")
            return bundled[0]
        raise BuilderError(f"Downloading bundletool failed: {error}. Set the build option bundletoolPath to a "
                           "bundletool-all.jar, or set appBundle to false to build only the APK.")
    if hashlib.sha256(data).hexdigest() != _BUNDLETOOL_SHA256:
        raise BuilderError("The downloaded bundletool does not match its pinned SHA-256; not using it.")
    partial = cached.with_suffix(".part")
    partial.write_bytes(data)
    partial.replace(cached)
    return cached


def build_bundle(*, java: Path, bundletool: Path, proto_apk: Path, dex_files: list[Path], native_libs: dict[str, Path],
                 abi: str, assets: dict[str, Path], root_files: dict[str, bytes], work: Path, output: Path) -> Path:
    """Unsigned bundle at output. assets maps "assets/..." archive paths to files."""
    module = work / "base-module.zip"
    with zipfile.ZipFile(module, "w", compression=zipfile.ZIP_DEFLATED) as zip_out:
        with zipfile.ZipFile(proto_apk) as proto:
            for info in proto.infolist():
                if info.filename == "AndroidManifest.xml":
                    zip_out.writestr("manifest/AndroidManifest.xml", proto.read(info))
                elif info.filename == "resources.pb" or info.filename.startswith(("res/", "assets/")):
                    zip_out.writestr(info.filename, proto.read(info))
        for index, dex in enumerate(dex_files):
            zip_out.write(dex, "dex/" + ("classes.dex" if index == 0 else f"classes{index + 1}.dex"))
        for name, path in native_libs.items():
            zip_out.write(path, f"lib/{abi}/{name}")
        for archive_name, path in assets.items():
            zip_out.write(path, archive_name)
        for name, data in root_files.items():
            zip_out.writestr("root/" + name, data)
    config = work / "BundleConfig.json"
    config.write_text(json.dumps(_BUNDLE_CONFIG, indent=2), encoding="utf-8")
    output.unlink(missing_ok=True)
    run([str(java), "-jar", str(bundletool), "build-bundle", f"--modules={module}", f"--output={output}",
         f"--config={config}"], label="bundletool build-bundle")
    run([str(java), "-jar", str(bundletool), "validate", f"--bundle={output}"], label="bundletool validate")
    return output


def sign_bundle(jdk: Path, bundle: Path, keystore: Path, store_pass: str, alias: str, key_pass: str) -> None:
    jarsigner = jdk / "bin" / ("jarsigner.exe" if os.name == "nt" else "jarsigner")
    # Passwords go through the environment: command lines end up in the build log and process lists.
    run([str(jarsigner), "-keystore", str(keystore), "-storepass:env", "INDEX_AAB_STORE_PASS",
         "-keypass:env", "INDEX_AAB_KEY_PASS", str(bundle), alias],
        env={"INDEX_AAB_STORE_PASS": store_pass, "INDEX_AAB_KEY_PASS": key_pass, "JAVA_HOME": str(jdk)},
        label="jarsigner")
