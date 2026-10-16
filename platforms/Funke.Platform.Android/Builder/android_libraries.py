"""What engine packages add to an Android build: Java sources, Maven AARs/JARs and manifest entries.

A package declares them in funke-package.lua:

    android = {
        java         = { "Android/Java/**.java" },                         -- compiled into the APK
        maven        = { "com.google.android.gms:play-services-ads:25.5.0" }, -- pinned in Android/maven.lock.json
        manifest     = "Android/AndroidManifest.xml",                     -- merged into the app manifest
        placeholders = { admobAppId = "ca-app-pub-..." },                 -- ${admobAppId}; build options override
    },
"""

from __future__ import annotations

import hashlib
import re
import shutil
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

from funke_platform_sdk import BuildRequest, BuilderError, emit_log, platform_cache, run
from funke_platform_sdk.packages import project_packages

import android_maven

_ANDROID_NS = "http://schemas.android.com/apk/res/android"


@dataclass
class Library:
    coordinate: str
    jars: list[Path] = field(default_factory=list)
    res: Path | None = None
    manifest: Path | None = None
    package: str = ""
    r_txt: Path | None = None
    native_libs: list[Path] = field(default_factory=list)
    assets: Path | None = None


@dataclass
class Contributions:
    java_sources: list[Path] = field(default_factory=list)
    libraries: list[Library] = field(default_factory=list)
    # (origin, manifest text) in priority order: package fragments first, then the libraries.
    manifests: list[tuple[str, str]] = field(default_factory=list)
    placeholders: dict[str, str] = field(default_factory=dict)

    @property
    def jars(self) -> list[Path]:
        return [jar for library in self.libraries for jar in library.jars]

    @property
    def empty(self) -> bool:
        return not (self.java_sources or self.libraries or self.manifests)

    def stamp(self) -> str:
        digest = hashlib.sha256()
        for path in [*self.java_sources, *self.jars]:
            digest.update(str(path).encode())
            digest.update(path.read_bytes() if path.suffix == ".java" else str(path.stat().st_size).encode())
        return digest.hexdigest()[:16]


def collect(request: BuildRequest, abi: str) -> Contributions:
    out = Contributions()
    closures = []
    for package in project_packages(request.engine_root, request.project_root, request.content_dir):
        android = package.section("android")
        if not android:
            continue
        out.java_sources += package.files(android.get("java"))
        roots = [str(r) for r in (android.get("maven") or [])]
        if roots:
            closure = android_maven.read_lock(package.root, roots)
            if closure is None:
                emit_log(f"{package.name}: {android_maven.LOCK_FILE} is missing or outdated; resolving its Maven "
                         "dependencies from the repositories (pin them with android_maven.py lock).", "warn")
                closure = android_maven.resolve(roots)
            closures.append(closure)
        for path in package.files(android.get("manifest")):
            out.manifests.append((f"{package.name} ({path.name})", path.read_text(encoding="utf-8-sig")))
        placeholders = android.get("placeholders") or {}
        if not isinstance(placeholders, dict):
            raise BuilderError(f"{package.name}: android.placeholders must be a table such as {{ name = \"default\" }}.")
        for name, default in placeholders.items():
            value = request.option(name)
            if not value:
                value = str(default)
                if request.is_release:
                    emit_log(f"{package.name}: build option '{name}' is not set; using the package default "
                             f"'{value}'. Set it in the Android build profile before publishing.", "warn")
            out.placeholders[name] = value

    artifacts = android_maven.merge(closures)
    if artifacts:
        emit_log(f"Android libraries: {len(artifacts)} Maven artifacts")
    for artifact in artifacts:
        library = _extract(artifact, android_maven.download(artifact), abi)
        out.libraries.append(library)
        if library.manifest:
            out.manifests.append((str(artifact.coordinate), library.manifest.read_text(encoding="utf-8-sig")))
    return out


def _extract(artifact: android_maven.Artifact, file: Path, abi: str) -> Library:
    library = Library(str(artifact.coordinate))
    if artifact.type != "aar":
        library.jars.append(file)
        return library
    root = platform_cache("Android", "maven", "extracted", artifact.coordinate.group,
                          f"{artifact.coordinate.artifact}-{artifact.coordinate.version}")
    marker = root / ".funke-extracted"
    if not marker.is_file() or marker.read_text(encoding="utf-8").strip() != artifact.sha256:
        for child in list(root.iterdir()):
            shutil.rmtree(child) if child.is_dir() else child.unlink()
        with zipfile.ZipFile(file) as archive:
            for entry in archive.namelist():
                target = (root / entry).resolve()
                if not str(target).startswith(str(root.resolve())):
                    raise BuilderError(f"{artifact.coordinate}: archive entry escapes its folder: {entry}")
            archive.extractall(root)
        marker.write_text(artifact.sha256, encoding="utf-8")
    if (root / "classes.jar").is_file():
        library.jars.append(root / "classes.jar")
    library.jars += sorted((root / "libs").glob("*.jar")) if (root / "libs").is_dir() else []
    if (root / "res").is_dir() and any(p.is_file() for p in (root / "res").rglob("*")):
        library.res = root / "res"
    if (root / "AndroidManifest.xml").is_file():
        library.manifest = root / "AndroidManifest.xml"
        library.package = ET.parse(library.manifest).getroot().get("package", "")
    if (root / "R.txt").is_file() and (root / "R.txt").stat().st_size > 0:
        library.r_txt = root / "R.txt"
    library.native_libs = sorted((root / "jni" / abi).glob("*.so")) if (root / "jni" / abi).is_dir() else []
    if (root / "assets").is_dir():
        library.assets = root / "assets"
    return library


def compile_resources(aapt2: Path, libraries: list[Library]) -> list[Path]:
    """Compiled resources per library, dependencies first: aapt2 link takes them as overlays in this order."""
    compiled = []
    for library in libraries:
        if not library.res:
            continue
        out = library.res.parent / "res-compiled.zip"
        stamp = library.res.parent / "res-compiled.stamp"
        key = str(aapt2)
        if not out.is_file() or not stamp.is_file() or stamp.read_text(encoding="utf-8") != key:
            run([str(aapt2), "compile", "--dir", str(library.res), "-o", str(out)], label=f"aapt2 compile {library.coordinate}")
            stamp.write_text(key, encoding="utf-8")
        compiled.append(out)
    return compiled


def _read_symbols(path: Path) -> dict[tuple[str, str], tuple[str, str]]:
    """R.txt: `int anim fade_in 0x7f010000` / `int[] styleable View { 0x..., 0x... }` -> {(type, name): (java type, value)}."""
    symbols = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split(" ", 3)
        if len(parts) == 4:
            symbols[(parts[1], parts[2])] = (parts[0], parts[3])
    return symbols


def write_r_classes(symbols_file: Path, libraries: list[Library], out_dir: Path) -> list[Path]:
    """R classes for every library package with the app's final resource IDs (what AGP generates as R.jar).

    Library bytecode reads its own R fields, so each package gets the symbols its R.txt names.
    """
    table = _read_symbols(symbols_file)
    wanted: dict[str, set[tuple[str, str]]] = {}
    for library in libraries:
        if library.package and library.r_txt:
            wanted.setdefault(library.package, set()).update(_read_symbols(library.r_txt).keys())
    shutil.rmtree(out_dir, ignore_errors=True)
    sources = []
    for package, keys in sorted(wanted.items()):
        by_type: dict[str, list[str]] = {}
        for res_type, name in sorted(keys):
            entry = table.get((res_type, name))
            if not entry:
                continue
            java_type, value = entry
            if java_type == "int[]":
                value = "{" + value.strip().strip("{}").strip() + "}"
            by_type.setdefault(res_type, []).append(f"        public static final {java_type} {name} = {value};\n")
        if not by_type:
            continue
        body = "".join(f"    public static final class {res_type} {{\n        private {res_type}() {{}}\n{''.join(fields)}    }}\n"
                       for res_type, fields in sorted(by_type.items()))
        source = out_dir / Path(*package.split(".")) / "R.java"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(f"package {package};\n\npublic final class R {{\n    private R() {{}}\n{body}}}\n", encoding="utf-8")
        sources.append(source)
    return sources


_SKIPPED_RESOURCE = re.compile(r"^(META-INF/(MANIFEST\.MF|[^/]+\.(SF|RSA|DSA|EC)|versions/.*|proguard/.*|com\.android\.tools/.*|"
                               r"LICENSE.*|NOTICE.*|DEPENDENCIES)|module-info\.class|.*\.class)$", re.IGNORECASE)


def java_resources(jars: list[Path]) -> dict[str, bytes]:
    """Non-class files of the libraries' jars (ServiceLoader registrations, Kotlin metadata) for the APK root."""
    files: dict[str, bytes] = {}
    for jar in jars:
        with zipfile.ZipFile(jar) as archive:
            for info in archive.infolist():
                name = info.filename
                if info.is_dir() or _SKIPPED_RESOURCE.match(name):
                    continue
                data = archive.read(info)
                if name.startswith("META-INF/services/") and name in files:
                    lines = files[name].decode("utf-8", "replace").splitlines()
                    extra = [l for l in data.decode("utf-8", "replace").splitlines() if l.strip() and l not in lines]
                    files[name] = ("\n".join(lines + extra) + "\n").encode("utf-8")
                else:
                    files.setdefault(name, data)
    return files
