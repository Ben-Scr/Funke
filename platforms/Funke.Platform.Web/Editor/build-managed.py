"""Incremental .NET/Mono + original Editor build, using the installed Web player toolchain."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('editor_native_builder', HERE / 'build.py')
editor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(editor)
web = editor.web

def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--host-only', action='store_true')
    args = parser.parse_args()
    work = ROOT / 'output/web-editor/managed'
    work.mkdir(parents=True, exist_ok=True)
    mono, wasm = web._import_dotnet_wasm(ROOT)
    sdk = wasm.find_dotnet_wasm_sdk(ROOT)
    if not sdk: raise RuntimeError(wasm.missing_sdk_fix())
    ninja = web.find_ninja()
    toolchain = web._dotnet_toolchain(sdk, ninja)
    sdl = web.build_sdl3(ROOT, toolchain, shared=False)
    dawn = web._build_emdawnwebgpu(ROOT, toolchain)
    cmake = web.find_cmake(ROOT)
    native = work / 'native'
    content = work / 'content'
    content.mkdir(exist_ok=True)
    for name in ['Textures', 'Shaders', 'Fonts']:
        source = ROOT / 'Funke-Runtime/FunkeAssets' / name
        if source.exists(): shutil.copytree(source, content / 'FunkeAssets' / name, dirs_exist_ok=True)
    if not args.host_only:
        options = [f'-DFUNKE_ROOT={ROOT.as_posix()}', f'-DFUNKE_SDL3_DIR={sdl.as_posix()}',
            f'-DFUNKE_EMDAWNWEBGPU_PORT={(dawn / "emdawnwebgpu.port.py").as_posix()}',
            '-DFUNKE_WEBGPU_INCLUDE_DIRS=' + ';'.join((dawn / p).as_posix() for p in ['webgpu/include', 'webgpu_cpp/include']),
            f'-DFUNKE_EDITOR_CONTENT={content.as_posix()}', '-DFUNKE_WEB_EDITOR_MANAGED=ON',
            f'-DFUNKE_MONO_PACKAGE_DIR={mono.as_posix()}',
            f'-DFUNKE_MONO_INCLUDE_DIR={(sdk.mono_runtime_native / "include/mono-2.0").as_posix()}',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG', '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG']
        web.run(toolchain.configure_command(cmake, HERE, native, options), env=toolchain.env, label='Configure managed Web editor')
        web.run([str(cmake), '--build', str(native), '--target', 'FunkeWeb', '--parallel', str(args.jobs)], env=toolchain.env, label='Build managed Web editor')

    host = work / 'host'
    host.mkdir(exist_ok=True)
    wasm._write_global_json(host, sdk)
    write_changed(host / 'Directory.Build.props', '<Project />\n')
    write_changed(host / 'Directory.Build.targets', '<Project />\n')
    core_dir = host / 'core'
    core_dir.mkdir(exist_ok=True)
    write_changed(core_dir / 'Funke-ScriptCore.csproj', f'''<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><TargetFramework>net11.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
<Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
<GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles><GenerateDependencyFile>true</GenerateDependencyFile>
<AssemblyName>Funke-ScriptCore</AssemblyName></PropertyGroup>
<ItemGroup><Compile Include="{(ROOT / 'Funke-ScriptCore/Source/**/*.cs').as_posix()}" /></ItemGroup></Project>''')
    env = wasm._dotnet_env(sdk)
    web.run([str(sdk.dotnet), 'build', str(core_dir / 'Funke-ScriptCore.csproj'), '-c', 'Release', '-o', str(host / 'core-bin'), '-nologo', '-p:UseSharedCompilation=false'], cwd=host, env=env, label='Build browser ScriptCore')
    core = host / 'core-bin/Funke-ScriptCore.dll'
    web.run([str(sdk.dotnet), str(wasm._calli_signature_tool(sdk)), str(host / 'CalliSignatures.g.cs'), str(core)], cwd=host, env=env, label='Collect browser interop signatures')
    shutil.copy2(HERE / 'Managed/Program.cs', host / 'Program.cs')
    archives = [native / 'FunkeWeb.a'] + sorted(p for p in native.glob('*.a') if p.name != 'FunkeWeb.a') + [sdl / 'lib/libSDL3.a']
    natives = '\n'.join(f'<NativeFileReference Include="{p.as_posix()}"' + ('' if p.stem == 'FunkeWeb' else ' ScanForPInvokes="false"') + ' />' for p in archives)
    roslyn = sdk.root / 'sdk' / sdk.sdk_version / 'Roslyn/bincore'
    references = '\n'.join(f'<Reference Include="{p.stem}"><HintPath>{p.as_posix()}</HintPath></Reference>' for p in [core, roslyn / 'Microsoft.CodeAnalysis.dll', roslyn / 'Microsoft.CodeAnalysis.CSharp.dll'])
    flags = f"--use-port={(dawn / 'emdawnwebgpu.port.py').as_posix()} -lidbfs.js"
    write_changed(host / 'FunkeWebHost.csproj', f'''<Project Sdk="Microsoft.NET.Sdk.WebAssembly">
<PropertyGroup><TargetFramework>net11.0</TargetFramework><OutputType>Exe</OutputType><AssemblyName>FunkeWebHost</AssemblyName>
<AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
<InvariantGlobalization>true</InvariantGlobalization><SatelliteResourceLanguages>en</SatelliteResourceLanguages>
<WasmBuildNative>true</WasmBuildNative><RunAOTCompilation>false</RunAOTCompilation><PublishTrimmed>false</PublishTrimmed>
<WasmCachePath>{sdk.emscripten_cache().as_posix()}</WasmCachePath>
<EmccInitialHeapSize>268435456</EmccInitialHeapSize><EmccStackSize>8388608</EmccStackSize>
<EmccExtraLDFlags>{flags}</EmccExtraLDFlags><WasmNativeStrip>true</WasmNativeStrip>
<CompressionEnabled>false</CompressionEnabled><BlazorEnableCompression>false</BlazorEnableCompression><WasmFingerprintAssets>false</WasmFingerprintAssets>
</PropertyGroup><ItemGroup><Compile Include="Program.cs" /><Compile Include="CalliSignatures.g.cs" />
<EmccExportedRuntimeMethod Include="FS;ccall;UTF8ToString" />
{references}
{natives}
</ItemGroup></Project>''')
    publish = work / 'publish'
    web.run([str(sdk.dotnet), 'publish', str(host / 'FunkeWebHost.csproj'), '-c', 'Release', '-o', str(publish), '-nologo', '-p:UseSharedCompilation=false'], cwd=host, env=env, label='Link original editor with browser .NET')
    site = publish / 'wwwroot'
    for p in (host / 'core-bin').glob('Funke-ScriptCore.*'):
        if p.suffix != '.pdb': shutil.copy2(p, content / p.name)
    with zipfile.ZipFile(site / 'editor-content.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(content.rglob('*')):
            if p.is_file(): archive.write(p, p.relative_to(content).as_posix())
    refs = work / 'references'
    refs.mkdir(exist_ok=True)
    ref_root = wasm._newest(sdk.root / 'packs/Microsoft.NETCore.App.Ref') / 'ref/net11.0'
    for p in ref_root.glob('*.dll'): shutil.copy2(p, refs / p.name)
    shutil.copy2(core, refs / core.name)
    with zipfile.ZipFile(site / 'references.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(refs.glob('*.dll')): archive.write(p, p.name)
    for p in (HERE / 'Browser').glob('*'):
        if p.is_file(): shutil.copy2(p, site / p.name)
    shutil.copy2(ROOT / 'Funke-Runtime/FunkeAssets/Textures/icon.png', site / 'icon.png')
    licenses = site / 'Licenses'
    licenses.mkdir(exist_ok=True)
    for name in ['LICENSE.txt', 'ThirdPartyNotices.txt']: shutil.copy2(sdk.root / name, licenses / ('dotnet-' + name))
    shutil.copy2(ROOT / 'THIRD_PARTY_LICENSES.md', licenses / 'Funke-ThirdParty.md')
    manifest = [p.relative_to(site).as_posix() for p in sorted(site.rglob('*'))
        if p.is_file() and p.name != 'runtime-files.json' and p.relative_to(site).parts[0] not in ('editor', 'references')]
    write_changed(site / 'runtime-files.json', json.dumps(manifest))
    print(f'Managed Web editor: {site}', flush=True)

if __name__ == '__main__': main()
