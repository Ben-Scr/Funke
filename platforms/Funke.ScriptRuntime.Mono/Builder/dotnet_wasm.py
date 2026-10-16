"""Browser C# through the .NET WebAssembly SDK (wasm-tools workload).

Wasm cannot generate native-to-managed thunks at runtime, so the P/Invoke, function-pointer and
[UnmanagedCallersOnly] tables must be generated from the game's assemblies at build time. Only the
.NET SDK's relink does that, so the Web builder compiles the engine to a static archive with the
SDK's own Emscripten and lets `dotnet publish` link it with the Mono runtime:

    sdk = find_dotnet_wasm_sdk(engine_root)
    toolchain_env = sdk.emscripten_env()            # compile the engine with this Emscripten
    site = publish_browser_app(sdk, ...)            # wwwroot with index.html, main.js, _framework/

.NET 11 is required: the .NET 10 wasm runtime is built with Emscripten 3.1.56, older than the 4.0.3
Dawn's emdawnwebgpu needs. .NET 11 runs the net10.0 script assemblies unchanged.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
from dataclasses import dataclass
from pathlib import Path

from funke_platform_sdk import BuilderError, emit_log, platform_cache, run

# Files the game reads at runtime; licenses ship in the folder but are not loaded into memory.
_NOT_LOADED_AT_RUNTIME = ("Licenses/",)

MIN_SDK_MAJOR = 11
IS_WINDOWS = platform.system() == "Windows"


def _version_key(name: str) -> tuple:
    return tuple(int(x) if x.isdigit() else x for x in re.split(r"[.-]", name))


def _newest(directory: Path, pattern: str = "*") -> Path | None:
    matches = [p for p in directory.glob(pattern) if p.is_dir()]
    return max(matches, key=lambda p: _version_key(p.name)) if matches else None


@dataclass
class DotnetWasmSdk:
    root: Path
    sdk_version: str

    @property
    def dotnet(self) -> Path:
        return self.root / ("dotnet.exe" if IS_WINDOWS else "dotnet")

    def _pack(self, pattern: str) -> Path:
        packs = sorted(self.root.joinpath("packs").glob(pattern), key=lambda p: p.name)
        if not packs:
            raise BuilderError(f"The .NET SDK at {self.root} has no '{pattern}' pack (install the wasm-tools workload).")
        version = _newest(packs[-1])
        if not version:
            raise BuilderError(f"The .NET pack {packs[-1]} is empty.")
        return version

    @property
    def mono_runtime_native(self) -> Path:
        return self._pack("Microsoft.NETCore.App.Runtime.Mono.browser-wasm") / "runtimes" / "browser-wasm" / "native"

    @property
    def emscripten_tools(self) -> Path:
        return self._pack("Microsoft.NET.Runtime.Emscripten.*.Sdk.*") / "tools"

    @property
    def emscripten_version(self) -> str:
        text = (self.emscripten_tools / "emscripten" / "emscripten-version.txt").read_text()
        match = re.search(r"\d+\.\d+\.\d+", text)
        return match.group(0) if match else "unknown"

    def emcmake(self) -> Path:
        return self.emscripten_tools / "emscripten" / ("emcmake.bat" if IS_WINDOWS else "emcmake")

    def emscripten_cache(self) -> Path:
        """A writable copy of the workload's frozen cache; Dawn's port library is built into it."""
        cache = platform_cache("Web", f"emscripten-cache-{self.emscripten_version}")
        # Outside the cache: Emscripten deletes the cache's contents when its sanity line changes.
        marker = cache.parent / f".{cache.name}.seeded"
        if not marker.is_file():
            source = self._pack("Microsoft.NET.Runtime.Emscripten.*.Cache.*") / "tools" / "emscripten" / "cache"
            emit_log(f"Seeding the Emscripten cache from {source} (first C# Web build only)...")
            shutil.copytree(source, cache, dirs_exist_ok=True)
            # The pack records Microsoft's build machine paths; Emscripten would treat that as a config
            # change and wipe the seeded libraries on first use.
            (cache / "sanity.txt").write_text(f"{self.emscripten_version}|{self.emscripten_tools / 'bin'}\n")
            marker.write_text(str(source))
        return cache

    def emscripten_env(self) -> dict[str, str]:
        """Environment the workload's Emscripten expects; its .emscripten reads these variables."""
        tools = self.emscripten_tools
        node = next(self._pack("Microsoft.NET.Runtime.Emscripten.*.Node.*").glob("tools/bin/node*"), None)
        python_root = self._pack("Microsoft.NET.Runtime.Emscripten.*.Python.*") / "tools"
        env = {
            "DOTNET_EMSCRIPTEN_LLVM_ROOT": str(tools / "bin"),
            "DOTNET_EMSCRIPTEN_BINARYEN_ROOT": str(tools),
            "DOTNET_EMSCRIPTEN_NODE_JS": str(node or ""),
            "EM_CACHE": str(self.emscripten_cache()),
            "EM_FROZEN_CACHE": "0",
            "FROZEN_CACHE": "",
            "EMSDK": "",
            "EM_CONFIG": str(tools / "emscripten" / ".emscripten"),
            "PATH": os.pathsep.join([str(tools / "emscripten"), str(tools / "bin"), os.environ.get("PATH", "")]),
        }
        if IS_WINDOWS:
            env.update({"EMSDK_PYTHON": str(python_root / "python.exe"), "PYTHONPATH": str(python_root), "PYTHONHOME": ""})
        return env


def _sdk_at(root: Path) -> DotnetWasmSdk | None:
    exe = root / ("dotnet.exe" if IS_WINDOWS else "dotnet")
    if not exe.is_file() or not (root / "sdk").is_dir():
        return None
    sdks = [p.name for p in (root / "sdk").iterdir() if p.is_dir() and re.match(r"\d+\.", p.name)]
    sdks = [s for s in sdks if int(s.split(".")[0]) >= MIN_SDK_MAJOR]
    if not sdks:
        return None
    sdk = DotnetWasmSdk(root, max(sdks, key=_version_key))
    try:
        sdk.mono_runtime_native, sdk.emscripten_tools  # noqa: B018 - validates the workload
    except BuilderError:
        return None
    return sdk


def find_dotnet_wasm_sdk(engine_root: Path | None, override: str | None = None) -> DotnetWasmSdk | None:
    candidates = [override, os.environ.get("FUNKE_DOTNET_WASM_SDK")]
    if engine_root:
        candidates.append(str(engine_root / "vendor" / "dotnet11"))
    candidates.append(os.environ.get("DOTNET_ROOT"))
    dotnet_on_path = shutil.which("dotnet")
    if dotnet_on_path:
        candidates.append(str(Path(dotnet_on_path).resolve().parent))
    for candidate in candidates:
        if candidate:
            sdk = _sdk_at(Path(candidate))
            if sdk:
                return sdk
    return None


def missing_sdk_fix() -> str:
    return ("Install the .NET 11 SDK and run `dotnet workload install wasm-tools`, or point "
            "FUNKE_DOTNET_WASM_SDK at such an SDK (the engine checks <engine>/vendor/dotnet11 too).")


def _content_manifest(content_dir: Path) -> dict:
    files, directories = [], set()
    for path in sorted(content_dir.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(content_dir).as_posix()
        if relative.startswith(_NOT_LOADED_AT_RUNTIME):
            continue
        files.append(relative)
        parent = Path(relative).parent
        while parent.as_posix() != ".":
            directories.add(parent.as_posix())
            parent = parent.parent
    return {"directories": sorted(directories, key=lambda d: (d.count("/"), d)), "files": files}


def _xml(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def _dotnet_env(sdk: DotnetWasmSdk) -> dict[str, str]:
    return {
        "DOTNET_ROOT": str(sdk.root), "DOTNET_MULTILEVEL_LOOKUP": "0", "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
        "DOTNET_NOLOGO": "1", "DOTNET_SKIP_FIRST_TIME_EXPERIENCE": "1", "MSBUILDDISABLENODEREUSE": "1",
    }


def _write_global_json(directory: Path, sdk: DotnetWasmSdk) -> None:
    # Keeps a global.json higher up (e.g. the engine repo's) from selecting another SDK.
    (directory / "global.json").write_text(json.dumps(
        {"sdk": {"version": sdk.sdk_version, "rollForward": "latestFeature", "allowPrerelease": True}}), encoding="utf-8")


def _calli_signature_tool(sdk: DotnetWasmSdk) -> Path:
    """Builds WasmCalliSignatures once per source revision; returns its dll."""
    source = Path(__file__).resolve().parent / "WasmCalliSignatures"
    digest = hashlib.sha256(b"".join(p.read_bytes() for p in sorted(source.glob("*.*")))).hexdigest()[:12]
    tool_dir = platform_cache("Web", "tools", f"calli-signatures-{digest}")
    dll = tool_dir / "out" / "WasmCalliSignatures.dll"
    if dll.is_file():
        return dll
    for item in source.glob("*.*"):
        shutil.copy2(item, tool_dir / item.name)
    _write_global_json(tool_dir, sdk)
    run([str(sdk.dotnet), "build", str(tool_dir / "WasmCalliSignatures.csproj"), "-c", "Release", "-o", str(tool_dir / "out"),
         "-nologo", "-p:UseSharedCompilation=false"], cwd=tool_dir, env=_dotnet_env(sdk), label="Build WasmCalliSignatures")
    return dll


def publish_browser_app(sdk: DotnetWasmSdk, work_dir: Path, *, app_name: str, managed_assemblies: list[Path],
                        native_archives: list[Path], ld_flags: list[str], template_dir: Path,
                        content_dir: Path, strip: bool) -> Path:
    """Generates the host project, relinks the runtime with the engine and returns the site folder."""
    project_dir = work_dir / "host"
    for stale in ("wwwroot", "refs"):
        shutil.rmtree(project_dir / stale, ignore_errors=True)
    (project_dir / "wwwroot").mkdir(parents=True, exist_ok=True)
    # The staged content is deleted after the build; MSBuild's incremental checks need stable paths.
    refs = project_dir / "refs"
    refs.mkdir()
    local_assemblies = []
    for assembly in managed_assemblies:
        shutil.copy2(assembly, refs / assembly.name)
        local_assemblies.append(refs / assembly.name)

    run([str(sdk.dotnet), str(_calli_signature_tool(sdk)), str(project_dir / "CalliSignatures.g.cs")]
        + [str(a) for a in local_assemblies], cwd=project_dir, env=_dotnet_env(sdk), label="Collect function-pointer signatures")

    references = "\n".join(
        f'    <Reference Include="{_xml(a.stem)}"><HintPath>{_xml(str(a))}</HintPath><Private>true</Private></Reference>'
        for a in local_assemblies)
    # FunkeWeb.a and the packages' Pkg.<Name>.Native.a hold the [DllImport] targets (matched by file name);
    # the other archives just need linking.
    natives = "\n".join(
        f'    <NativeFileReference Include="{_xml(str(a))}"'
        + ('' if a.stem == "FunkeWeb" or a.stem.startswith("Pkg.") else ' ScanForPInvokes="false"') + " />"
        for a in native_archives)
    project = f"""<Project Sdk="Microsoft.NET.Sdk.WebAssembly">
  <PropertyGroup>
    <TargetFramework>net{sdk.sdk_version.split('.')[0]}.0</TargetFramework>
    <OutputType>Exe</OutputType>
    <AssemblyName>FunkeWebHost</AssemblyName>
    <RootNamespace>FunkeWebHost</RootNamespace>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <Nullable>disable</Nullable>
    <InvariantGlobalization>true</InvariantGlobalization>
    <SatelliteResourceLanguages>en</SatelliteResourceLanguages>
    <WasmBuildNative>true</WasmBuildNative>
    <RunAOTCompilation>false</RunAOTCompilation>
    <!-- Scripts are loaded and reflected over at runtime; trimming would strip what they use. -->
    <PublishTrimmed>false</PublishTrimmed>
    <WasmCachePath>{_xml(str(sdk.emscripten_cache()))}</WasmCachePath>
    <EmccInitialHeapSize>134217728</EmccInitialHeapSize>
    <EmccStackSize>5242880</EmccStackSize>
    <EmccExtraLDFlags>{_xml(' '.join(ld_flags))}</EmccExtraLDFlags>
    <WasmNativeStrip>{'true' if strip else 'false'}</WasmNativeStrip>
    <CompressionEnabled>false</CompressionEnabled>
    <BlazorEnableCompression>false</BlazorEnableCompression>
    <WasmFingerprintAssets>false</WasmFingerprintAssets>
    <GenerateDocumentationFile>false</GenerateDocumentationFile>
  </PropertyGroup>
  <ItemGroup>
{references}
{natives}
  </ItemGroup>
</Project>
"""
    (project_dir / "FunkeWebHost.csproj").write_text(project, encoding="utf-8")
    (project_dir / "Program.cs").write_text(
        "// Generated by Funke.ScriptRuntime.Mono. dotnet.js has started Mono; this hands over to the engine.\n"
        "using System.Runtime.InteropServices;\n\n"
        "internal static class Program\n{\n"
        "    [DllImport(\"FunkeWeb\", EntryPoint = \"FunkeWeb_Start\")]\n"
        "    private static extern int Start();\n\n"
        "    public static int Main() => Start();\n}\n", encoding="utf-8")
    _write_global_json(project_dir, sdk)
    for name in ("index.html", "main.js"):
        text = (template_dir / name).read_text(encoding="utf-8").replace("@APP_NAME@", _xml(app_name))
        (project_dir / "wwwroot" / name).write_text(text, encoding="utf-8")

    publish_dir = work_dir / "publish"
    shutil.rmtree(publish_dir, ignore_errors=True)
    # The SDK does not recompile runtime.c when only its generated trampoline tables change, which would
    # leave the previous game's signatures in the binary.
    for stale in (project_dir / "obj").glob("*/*/wasm"):
        shutil.rmtree(stale, ignore_errors=True)
    env = _dotnet_env(sdk)
    emit_log(f".NET {sdk.sdk_version} WebAssembly relink (Emscripten {sdk.emscripten_version})")
    run([str(sdk.dotnet), "publish", str(project_dir / "FunkeWebHost.csproj"), "-c", "Release",
         "-o", str(publish_dir), "-nologo", "-p:UseSharedCompilation=false"],
        cwd=project_dir, env=env, label="dotnet publish (WebAssembly relink)")

    site = publish_dir / "wwwroot"
    if not (site / "_framework" / "dotnet.js").is_file():
        raise BuilderError(f"dotnet publish did not produce {site / '_framework' / 'dotnet.js'}")
    game = site / "game"
    shutil.copytree(content_dir, game, dirs_exist_ok=True)
    (game / "funke-content.json").write_text(json.dumps(_content_manifest(content_dir)), encoding="utf-8")
    return site

