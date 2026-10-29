param(
    [switch]$RefreshRuntime,
    [string]$FixtureName = 'EcsNativeComparison-20261029'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
if ($FixtureName -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid fixture name.' }
$fixture = Join-Path $repo "bin-int/_agent/$FixtureName"
$runtime = Join-Path $fixture 'runtime'
$project = Join-Path $fixture 'project'
$nativeOutput = "$project/bin/Release-windows-x86_64/EcsNativeComparison-NativeScripts"
if (Get-Process Funke-Runtime -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$runtime\Funke-Runtime.exe" }) { throw 'Comparison runtime is running.' }
foreach ($path in @($runtime, "$fixture/lib", "$project/Assets/Scenes", $nativeOutput)) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}
if ($RefreshRuntime -or !(Test-Path "$runtime/Funke-Runtime.exe")) {
    $runtimeSource = "$repo/bin/Release-windows-x86_64/Funke-Runtime"
    foreach ($name in @('Funke-Runtime.exe', 'Funke-Engine.dll', 'nethost.dll', 'SDL3.dll', 'Tracy.dll')) {
        Copy-Item -LiteralPath "$runtimeSource/$name" -Destination $runtime
    }
    foreach ($name in @('Funke-ScriptCore.dll', 'Funke-ScriptCore.deps.json', 'Funke-ScriptCore.runtimeconfig.json')) {
        Copy-Item -LiteralPath "$repo/bin/Release-windows-x86_64/Funke-ScriptCore/$name" -Destination $runtime
    }
    Copy-Item -LiteralPath "$repo/bin/Release-windows-x86_64/Funke-Engine/Funke-Engine.lib" -Destination "$fixture/lib"
    if (!(Test-Path "$runtime/FunkeAssets")) {
        New-Item -ItemType Junction -Path "$runtime/FunkeAssets" -Target "$runtimeSource/FunkeAssets" | Out-Null
    }
}
@{
    projectFormatVersion=1; name='EcsNativeComparison'; engineVersion='2026.1.0-beta.1';
    startupScene='Benchmark'; lastOpenedScene='Benchmark'; buildScenes=@('Benchmark');
    modules=@('Funke.2D'); assetSerializationFormat='Json'; buildWidth=320; buildHeight=200;
    buildFullscreen=$false; buildRunInBackground=$true; buildVsync=$false;
    splashScreen=@{enabled=$false}; renderBackend='D3D12'
} | ConvertTo-Json -Depth 6 | Set-Content "$project/project.json" -Encoding utf8
'{"AssetGUID":"8100292026000000002","uuid":"8100292026000000002","kind":"Scene"}' | Set-Content "$project/Assets/Scenes/Benchmark.scene.meta" -Encoding utf8
$csproj = @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework><OutputType>Library</OutputType>
    <AssemblyName>EcsNativeComparison</AssemblyName><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Optimize>true</Optimize><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <OutputPath>bin/Release/</OutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="__SOURCE__/ManagedEcsBenchmark.cs" />
    <Reference Include="Funke-ScriptCore"><HintPath>../runtime/Funke-ScriptCore.dll</HintPath><Private>false</Private></Reference>
  </ItemGroup>
</Project>
'@
$csproj.Replace('__SOURCE__', $PSScriptRoot) | Set-Content "$project/EcsNativeComparison.csproj" -Encoding utf8
& dotnet build "$project/EcsNativeComparison.csproj" -c Release --nologo -v minimal -p:UseSharedCompilation=false
if ($LASTEXITCODE -ne 0) { throw 'Managed comparison build failed.' }
& cmake -S $PSScriptRoot -B "$fixture/native-build" -G 'Visual Studio 17 2022' -T v143 -A x64 "-DFUNKE_DIR=$repo" "-DFUNKE_ENGINE_LIBRARY_DIR=$fixture/lib" "-DFUNKE_NATIVE_OUTPUT_DIR=$nativeOutput"
if ($LASTEXITCODE -ne 0) { throw 'Native comparison configure failed.' }
& cmake --build "$fixture/native-build" --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native comparison build failed.' }
Write-Output "Prepared pinned comparison: $fixture"
