"""Small real filesystem SDK payloads for packaging and manifest regression tests."""
from pathlib import Path
import json

from scripts import sdk_manifest


def write(path: Path, content: str = "fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def make_sdk(sdk: Path, *, native: bool = False, platform: str = "windows") -> None:
    for name in sdk_manifest.SCRIPT_CORE_FILES:
        write(sdk / "Funke-ScriptCore" / name)
    for name in sdk_manifest.required_licenses(platform):
        write(sdk / "Licenses" / name)
    write(sdk / ("premake5.exe" if platform == "windows" else "premake5"))
    for name in ("project-packages.lua", "package-loader.lua", "fix_csharp_platforms.lua", "architecture.lua", "compat.lua"):
        write(sdk / "premake" / name)
    write(sdk / "templates/Funke.Empty2D/template.json", "{}")
    for directory in ("Runtime-Development", "Runtime-Dist"):
        for name in (*sdk_manifest.PLATFORMS[platform], *sdk_manifest.SCRIPT_CORE_FILES):
            write(sdk / directory / name)
        write(sdk / directory / "FunkeAssets/Fonts/font.ttf")
        for name in sdk_manifest.required_licenses(platform):
            write(sdk / directory / "Licenses" / name)
    if native:
        for name in sdk_manifest.NATIVE_HEADERS:
            write(sdk / name)
        for configuration in ("Release", "Dist"):
            directory = sdk / f"bin/{configuration}-{platform}-x86_64/Funke-Engine"
            write(directory / sdk_manifest.PLATFORMS[platform][1])
            if platform == "windows":
                write(directory / "Funke-Engine.lib")
    sdk_manifest.write_manifest(sdk, "2026.1.0-beta.1", platform, "x86_64", native,
                                {"Development": "Runtime-Development", "Release": "Runtime-Dist"})


def make_bundle(bundle: Path, *, native: bool = False, platform: str = "windows") -> None:
    make_sdk(bundle / "EngineSDK", native=native, platform=platform)
    suffix = ".exe" if platform == "windows" else ""
    for name in (f"Funke-Launcher{suffix}", f"Funke-Editor{suffix}", f"funke{suffix}",
                 *sdk_manifest.PLATFORMS[platform], *sdk_manifest.SCRIPT_CORE_FILES,
                 f"Funke-PackageTool{suffix}", "Funke-PackageTool.dll", "Funke-PackageTool.deps.json",
                 "Funke-PackageTool.runtimeconfig.json"):
        write(bundle / name)
    write(bundle / "FunkeAssets/Fonts/font.ttf")
    write(bundle / "BuildMetadata.json", json.dumps({"version": "2026.1.0-beta.1"}))
