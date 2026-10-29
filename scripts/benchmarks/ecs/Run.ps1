param(
    [string]$Tag = "main",
    [string]$Sizes = "10000",
    [ValidateSet("all", "structural", "query", "custom", "history", "validation", "mixed", "playback")][string]$Group = "all",
    [int]$Warmups = 3,
    [int]$Samples = 9,
    [string]$Filter = "",
    [switch]$Wait,
    [switch]$PrepareOnly,
    [switch]$UsePreparedRuntime,
    [switch]$UsePreparedHarness
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$fixture = Join-Path $repo "bin-int/_agent/EcsBenchmark-20261028"
$runtime = Join-Path $fixture "runtime"
$project = Join-Path $fixture "project"
$results = Join-Path $repo "output/ecs-benchmark-2026-10-28"
if ($Sizes -notmatch '^\d+$') { throw "Use one entity count per process; Run-Suite.ps1 runs the three sizes sequentially." }
if ($Tag -notmatch '^[A-Za-z0-9_-]+$') { throw "Tag must contain only letters, digits, underscores or hyphens." }
if ($UsePreparedHarness -and !$UsePreparedRuntime) { throw "UsePreparedHarness requires UsePreparedRuntime to keep the measured binaries fixed." }
if ((Test-Path -LiteralPath "$results/$Tag.done") -or (Test-Path -LiteralPath "$results/$Tag.failed")) { throw "Choose a new output tag; $Tag already exists." }
if (Get-Process Funke-Runtime -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$runtime\Funke-Runtime.exe" }) { throw "The isolated benchmark runtime is still running." }
foreach ($folder in @($runtime, $project, $results, "$project/Assets/Scenes", "$project/Assets/Prefabs")) {
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
}
$runtimeSource = Join-Path $repo "bin/Release-windows-x86_64/Funke-Runtime"
foreach ($file in @("Funke-Runtime.exe", "Funke-Engine.dll", "nethost.dll", "SDL3.dll", "Tracy.dll")) {
    if ($UsePreparedRuntime) {
        if (!(Test-Path -LiteralPath (Join-Path $runtime $file))) { throw "Prepared runtime is missing $file" }
    } else {
        Copy-Item -LiteralPath (Join-Path $runtimeSource $file) -Destination $runtime
    }
}
foreach ($file in @("Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json")) {
    if ($UsePreparedRuntime) {
        if (!(Test-Path -LiteralPath (Join-Path $runtime $file))) { throw "Prepared runtime is missing $file" }
    } else {
        Copy-Item -LiteralPath (Join-Path $repo "bin/Release-windows-x86_64/Funke-ScriptCore/$file") -Destination $runtime
    }
}
if (!(Test-Path -LiteralPath "$runtime/FunkeAssets")) {
    New-Item -ItemType Junction -Path "$runtime/FunkeAssets" -Target "$runtimeSource/FunkeAssets" | Out-Null
}
@{
    projectFormatVersion=1; name="EcsBenchmark"; engineVersion="2026.1.0-beta.1";
    startupScene="Benchmark"; lastOpenedScene="Benchmark"; buildScenes=@("Benchmark");
    modules=@("Funke.2D"); assetSerializationFormat="Json"; buildWidth=320; buildHeight=200;
    buildFullscreen=$false; buildRunInBackground=$true; buildVsync=$false;
    splashScreen=@{enabled=$false}; renderBackend="D3D12"
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$project/project.json" -Encoding utf8
'{"version":1,"name":"Benchmark","systems":["EcsBenchmarkSystem"],"entities":[]}' | Set-Content -LiteralPath "$project/Assets/Scenes/Benchmark.scene" -Encoding utf8
'{"AssetGUID":"8100282026000000002","uuid":"8100282026000000002","kind":"Scene"}' | Set-Content -LiteralPath "$project/Assets/Scenes/Benchmark.scene.meta" -Encoding utf8
'{"version":1,"type":"Prefab","Entities":[{"uuid":"8100282026000000003","name":"","Transform2D":{"posX":0,"posY":0,"rotation":0,"scaleX":1,"scaleY":1},"SpriteRenderer":{"r":1,"g":1,"b":1,"a":1,"sortOrder":0,"sortLayer":0}}]}' | Set-Content -LiteralPath "$project/Assets/Prefabs/Benchmark.prefab" -Encoding utf8
'{"AssetGUID":"8100282026000000001","uuid":"8100282026000000001","kind":"Prefab"}' | Set-Content -LiteralPath "$project/Assets/Prefabs/Benchmark.prefab.meta" -Encoding utf8
$csproj = @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework><OutputType>Library</OutputType>
    <AssemblyName>EcsBenchmark</AssemblyName><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Optimize>true</Optimize><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <OutputPath>bin/Release/</OutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="__SOURCE__/*.cs" />
    <Reference Include="Funke-ScriptCore"><HintPath>../runtime/Funke-ScriptCore.dll</HintPath><Private>false</Private></Reference>
  </ItemGroup>
</Project>
'@
$csproj.Replace("__SOURCE__", $PSScriptRoot) | Set-Content -LiteralPath "$project/EcsBenchmark.csproj" -Encoding utf8
if ($UsePreparedHarness) {
    if (!(Test-Path -LiteralPath "$project/bin/Release/EcsBenchmark.dll")) { throw "Prepared benchmark assembly is missing" }
} else {
    & dotnet build "$project/EcsBenchmark.csproj" -c Release --nologo -v minimal -p:UseSharedCompilation=false
    if ($LASTEXITCODE -ne 0) { throw "Benchmark build failed" }
}
$machine = @{
    Timestamp=(Get-Date).ToString("o"); Cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed);
    Memory=(Get-CimInstance Win32_PhysicalMemory | Select-Object Capacity,Speed,ConfiguredClockSpeed,SMBIOSMemoryType,Manufacturer,PartNumber);
    OS=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,FreePhysicalMemory);
    GPU=(Get-CimInstance Win32_VideoController | Select-Object Name);
    Binaries=@(Get-FileHash -LiteralPath "$runtime/Funke-Runtime.exe","$runtime/Funke-Engine.dll","$runtime/Funke-ScriptCore.dll","$project/bin/Release/EcsBenchmark.dll" -Algorithm SHA256);
    Sources=@(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs' | Get-FileHash -Algorithm SHA256);
    PowerScheme=(& powercfg /getactivescheme | Out-String).Trim();
    EcbSerialKernels=$env:FUNKE_ECB_SERIAL_KERNELS;
    EcbParallelDiagnostics=$env:FUNKE_ECB_PARALLEL_DIAGNOSTICS;
    EcbTimings=$env:FUNKE_ECB_TIMINGS;
    GitHead=(& git -C $repo rev-parse HEAD | Out-String).Trim();
    Dirty=$true
}
$machine | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$results/$Tag-machine.json" -Encoding utf8
if ($PrepareOnly) { Write-Output "Prepared $project"; exit 0 }
$env:ECS_BENCH_OUTPUT = "$results/$Tag"
$env:ECS_BENCH_SIZES = $Sizes
$env:ECS_BENCH_GROUP = $Group
$env:ECS_BENCH_WARMUPS = "$Warmups"
$env:ECS_BENCH_SAMPLES = "$Samples"
$env:ECS_BENCH_FILTER = $Filter
$env:DOTNET_TieredCompilation = "0"
$env:DOTNET_ReadyToRun = "0"
$env:DOTNET_EnableDiagnostics = "0"
$process = Start-Process -FilePath "$runtime/Funke-Runtime.exe" -ArgumentList @("--project=`"$project`"", "--background", "--prefs-profile=ecs-benchmark") -WorkingDirectory $runtime -WindowStyle Hidden -RedirectStandardOutput "$results/$Tag-stdout.log" -RedirectStandardError "$results/$Tag-stderr.log" -PassThru
@{ Pid=$process.Id; Output=$env:ECS_BENCH_OUTPUT; Runtime=$runtime; Project=$project } | ConvertTo-Json
if ($Wait) {
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath "$results/$Tag.done")) { throw "Benchmark failed; inspect $results/$Tag-stdout.log and .failed." }
}
