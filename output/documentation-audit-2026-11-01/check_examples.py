from pathlib import Path
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
api = OUT / 'managed/Api'
examples = OUT / 'managed/Examples'
api.mkdir(parents=True, exist_ok=True)
examples.mkdir(parents=True, exist_ok=True)
props = '<TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><GenerateAssemblyInfo>false</GenerateAssemblyInfo>'
(api / 'Api.csproj').write_text(f'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>{props}<AssemblyName>Funke-ScriptCore</AssemblyName></PropertyGroup><ItemGroup><Compile Include="{ROOT.as_posix()}/Funke-ScriptCore/Source/**/*.cs" /></ItemGroup></Project>', encoding='utf-8')

selection = {
    'Getting-Started.md': [0, 1],
    'Assets/Assets.md': [0],
    'Assets/Data-Assets.md': [0, 1],
    'Editor/Platform-Export-Testing.md': [0],
    'Engine-Core/3D.md': [1, 3],
    'Engine-Core/Animation.md': [0, 1],
    'Engine-Core/ECS.md': [4, 5],
    'Engine-Core/Graphics-Settings.md': [0],
    'Engine-Core/Physics-3D.md': [0],
    'Packages/Writing-Packages.md': [0],
    'Scripting/Scripting.md': [0, 3, 30],
}
manifest = []
for doc_index, (rel, selected) in enumerate(selection.items()):
    path = ROOT / 'Github-Docs/Documentation' / rel
    body = path.read_text(encoding='utf-8')
    blocks = list(re.finditer(r'```csharp\n(.*?)\n```', body, re.S))
    for block_index in selected:
        match = blocks[block_index]
        line = body[:match.start(1)].count('\n') + 1
        filename = f'Doc{doc_index:02}_{block_index:02}.cs'
        # Put each document in its own namespace, keeping shared example types together.
        content = f'namespace Documentation{doc_index} {{\n#line {line} "{path.as_posix()}"\n{match.group(1)}\n}}\n'
        (examples / filename).write_text(content, encoding='utf-8')
        manifest.append({'file': filename, 'document': str(path.relative_to(ROOT)), 'line': line})
includes = ''.join(f'<Compile Include="{item["file"]}" />' for item in manifest)
(examples / 'Examples.csproj').write_text(f'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>{props}<TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup><ProjectReference Include="../Api/Api.csproj" />{includes}</ItemGroup></Project>', encoding='utf-8')
(OUT / 'examples.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
result = subprocess.run(['dotnet', 'build', str(examples / 'Examples.csproj'), '--nologo', '-v:minimal'], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
(OUT / 'examples-build.log').write_text(result.stdout, encoding='utf-8')
print(result.stdout)
print(f'Checked {len(manifest)} complete C# examples against current ScriptCore source.')
raise SystemExit(result.returncode)
