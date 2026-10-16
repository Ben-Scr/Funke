"""Locates the Android SDK, NDK, JDK and build tools."""

from __future__ import annotations

import os
import platform
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

MIN_SDK = 26          # Vulkan 1.0 + AHardwareBuffer, which Dawn needs
TARGET_SDK = 36       # Google Play requires it for new apps and updates since 2026-08-31
MIN_NDK_MAJOR = 26


def _version_key(name: str) -> tuple[int, ...]:
    return tuple(int(p) for p in re.findall(r"\d+", name)) or (0,)


def _exe(name: str) -> str:
    return f"{name}.exe" if platform.system() == "Windows" else name


def _script(name: str) -> str:
    return f"{name}.bat" if platform.system() == "Windows" else name


def _unity_android_players() -> list[Path]:
    """Unity Hub installs bundle a complete SDK / NDK / OpenJDK; reuse them when nothing else exists."""
    roots = []
    if platform.system() == "Windows":
        roots.append(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Unity" / "Hub" / "Editor")
    elif platform.system() == "Darwin":
        roots.append(Path("/Applications/Unity/Hub/Editor"))
    else:
        roots.append(Path.home() / "Unity" / "Hub" / "Editor")
    players = []
    for root in roots:
        if root.is_dir():
            for editor in sorted(root.iterdir(), key=lambda p: _version_key(p.name), reverse=True):
                player = editor / ("Unity.app/Contents/PlaybackEngines/AndroidPlayer" if platform.system() == "Darwin"
                                   else "Editor/Data/PlaybackEngines/AndroidPlayer")
                if player.is_dir():
                    players.append(player)
    return players


def find_sdk(override: str | None = None) -> Path | None:
    candidates = [override, os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    system = platform.system()
    if system == "Windows":
        candidates.append(str(Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"))
    elif system == "Darwin":
        candidates.append(str(Path.home() / "Library" / "Android" / "sdk"))
    else:
        candidates.append(str(Path.home() / "Android" / "Sdk"))
    candidates += [str(p / "SDK") for p in _unity_android_players()]
    for candidate in candidates:
        if candidate and (Path(candidate) / "platforms").is_dir():
            return Path(candidate)
    return None


def find_ndk(sdk: Path | None, override: str | None = None) -> Path | None:
    candidates = [override, os.environ.get("ANDROID_NDK_HOME"), os.environ.get("ANDROID_NDK_ROOT")]
    if sdk and (sdk / "ndk").is_dir():
        candidates += [str(p) for p in sorted((sdk / "ndk").iterdir(), key=lambda p: _version_key(p.name), reverse=True)]
    if sdk and (sdk / "ndk-bundle").is_dir():
        candidates.append(str(sdk / "ndk-bundle"))
    candidates += [str(p / "NDK") for p in _unity_android_players()]
    for candidate in candidates:
        if candidate and (Path(candidate) / "build" / "cmake" / "android.toolchain.cmake").is_file():
            return Path(candidate)
    return None


def ndk_major(ndk: Path) -> int:
    props = ndk / "source.properties"
    if props.is_file():
        match = re.search(r"Pkg\.Revision\s*=\s*(\d+)", props.read_text())
        if match:
            return int(match.group(1))
    return 0


def _java_major(java: Path) -> int:
    try:
        out = subprocess.run([str(java), "-version"], capture_output=True, text=True, timeout=30).stderr
    except OSError:
        return 0
    match = re.search(r'version "(\d+)(?:\.(\d+))?', out)
    if not match:
        return 0
    major = int(match.group(1))
    return int(match.group(2)) if major == 1 and match.group(2) else major


def find_jdk(override: str | None = None) -> Path | None:
    candidates = [override, os.environ.get("JAVA_HOME")]
    system = platform.system()
    if system == "Windows":
        program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        candidates += [str(program_files / "Android" / "Android Studio" / "jbr")]
        for parent in (program_files / "Android" / "openjdk", program_files / "Java", program_files / "Eclipse Adoptium", program_files / "Microsoft"):
            if parent.is_dir():
                candidates += [str(p) for p in sorted(parent.iterdir(), key=lambda p: _version_key(p.name), reverse=True)]
    elif system == "Darwin":
        candidates.append("/Applications/Android Studio.app/Contents/jbr/Contents/Home")
    candidates += [str(p / "OpenJDK") for p in _unity_android_players()]
    for candidate in candidates:
        if not candidate:
            continue
        java = Path(candidate) / "bin" / _exe("java")
        if java.is_file() and _java_major(java) >= 17:
            return Path(candidate)
    return None


@dataclass
class AndroidToolchain:
    sdk: Path
    ndk: Path
    jdk: Path
    build_tools: Path
    platform_jar: Path
    ninja: Path | None

    @property
    def javac(self) -> Path:
        return self.jdk / "bin" / _exe("javac")

    @property
    def keytool(self) -> Path:
        return self.jdk / "bin" / _exe("keytool")

    @property
    def aapt2(self) -> Path:
        return self.build_tools / _exe("aapt2")

    @property
    def d8(self) -> Path:
        return self.build_tools / _script("d8")

    @property
    def zipalign(self) -> Path:
        return self.build_tools / _exe("zipalign")

    @property
    def apksigner(self) -> Path:
        return self.build_tools / _script("apksigner")


def resolve(options: dict | None = None) -> tuple[AndroidToolchain | None, list[dict]]:
    """Returns the toolchain, or None plus a list of {severity, message, fix} issues."""
    options = options or {}
    issues: list[dict] = []
    sdk = find_sdk(options.get("sdkPath"))
    if not sdk:
        issues.append({"severity": "error", "message": "Android SDK not found.",
                       "fix": "Install Android Studio (or the command-line tools) and set ANDROID_HOME."})
    ndk = find_ndk(sdk, options.get("ndkPath"))
    if not ndk:
        issues.append({"severity": "error", "message": "Android NDK not found.",
                       "fix": "Install the NDK from the Android Studio SDK Manager or set ANDROID_NDK_HOME."})
    elif ndk_major(ndk) and ndk_major(ndk) < MIN_NDK_MAJOR:
        issues.append({"severity": "error", "message": f"NDK r{ndk_major(ndk)} is too old (need r{MIN_NDK_MAJOR}+).",
                       "fix": "Install a newer NDK."})
    jdk = find_jdk(options.get("jdkPath"))
    if not jdk:
        issues.append({"severity": "error", "message": "JDK 17+ not found.",
                       "fix": "Install a JDK 17 or newer (Android Studio ships one) and set JAVA_HOME."})

    build_tools = None
    platform_jar = None
    ninja = None
    if sdk:
        tools_root = sdk / "build-tools"
        versions = sorted((p for p in tools_root.iterdir() if (p / _exe("aapt2")).is_file()), key=lambda p: _version_key(p.name),
                          reverse=True) if tools_root.is_dir() else []
        build_tools = versions[0] if versions else None
        if not build_tools:
            issues.append({"severity": "error", "message": "Android build-tools not found.",
                           "fix": "Install 'Android SDK Build-Tools' with the SDK Manager."})
        platforms = sorted((p for p in (sdk / "platforms").iterdir() if (p / "android.jar").is_file()
                            and _version_key(p.name)[0] >= 34), key=lambda p: _version_key(p.name), reverse=True)
        platform_jar = platforms[-1] / "android.jar" if platforms else None
        for preferred in platforms:
            if _version_key(preferred.name)[0] == TARGET_SDK:
                platform_jar = preferred / "android.jar"
        if not platform_jar:
            issues.append({"severity": "error", "message": "No Android platform (API 34+) installed.",
                           "fix": f"Install 'Android SDK Platform {TARGET_SDK}' with the SDK Manager."})
        cmake_root = sdk / "cmake"
        if cmake_root.is_dir():
            for version in sorted(cmake_root.iterdir(), key=lambda p: _version_key(p.name), reverse=True):
                if (version / "bin" / _exe("ninja")).is_file():
                    ninja = version / "bin" / _exe("ninja")
                    break

    if issues:
        return None, issues
    return AndroidToolchain(sdk, ndk, jdk, build_tools, platform_jar, ninja), issues
