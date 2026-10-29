param(
    [Parameter(Mandatory)][ValidateSet('Native','Managed')][string]$Language,
    [Parameter(Mandatory)][string]$Tag,
    [int]$Count = 100000,
    [int]$Warmups = 3,
    [int]$Samples = 9,
    [string]$FixtureName = 'EcsNativeComparison-20261029',
    [string]$ResultsName = 'ecs-native-comparison-2026-10-29',
    [string]$Filter = '',
    [switch]$ValidationOnly,
    [switch]$Wait
)
$ErrorActionPreference = 'Stop'
if ($ValidationOnly) { $Warmups = 0; $Samples = 1 }
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
if ($FixtureName -notmatch '^[A-Za-z0-9_-]+$' -or $ResultsName -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid fixture/results name.' }
$fixture = Join-Path $repo "bin-int/_agent/$FixtureName"
$runtime = Join-Path $fixture 'runtime'
$project = Join-Path $fixture 'project'
$results = Join-Path $repo "output/$ResultsName"
if ($Tag -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid output tag.' }
if ($Count -le 0 -or $Warmups -lt 0 -or $Samples -le 0) { throw 'Invalid sample settings.' }
New-Item -ItemType Directory -Force -Path $results | Out-Null
if (Test-Path "$results/$Tag.csv") { throw 'Output tag already exists.' }
if (Get-Process Funke-Runtime -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$runtime\Funke-Runtime.exe" }) { throw 'Comparison runtime is running.' }
$driver = @{uuid='8100292026000000003'; name='BenchmarkDriver'; Transform2D=@{posX=0;posY=0;rotation=0;scaleX=1;scaleY=1}}
$systems = @()
if ($Language -eq 'Native') { $driver.Scripts = @(@{className='NativeEcsBenchmark'; type='Native'}) }
else { $systems = @('ManagedEcsBenchmark') }
@{version=1;name='Benchmark';systems=$systems;entities=@($driver)} | ConvertTo-Json -Depth 8 | Set-Content "$project/Assets/Scenes/Benchmark.scene" -Encoding utf8
$binaries = @("$runtime/Funke-Runtime.exe", "$runtime/Funke-Engine.dll", "$runtime/Funke-ScriptCore.dll", "$project/bin/Release/EcsNativeComparison.dll", "$project/bin/Release-windows-x86_64/EcsNativeComparison-NativeScripts/EcsNativeComparison-NativeScripts.dll")
@{
    Timestamp=(Get-Date).ToString('o'); Language=$Language; Count=$Count; Warmups=$Warmups; Samples=$Samples; Filter=$Filter;
    Purpose=$(if ($ValidationOnly) {'Correctness validation; timer values are not performance results'} else {'Performance measurement'});
    CPU=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
    Memory=(Get-CimInstance Win32_PhysicalMemory | Select-Object Capacity,ConfiguredClockSpeed);
    OS=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,FreePhysicalMemory);
    Binaries=@(Get-FileHash -LiteralPath $binaries -Algorithm SHA256);
    Sources=@(Get-ChildItem -LiteralPath $PSScriptRoot -File | Get-FileHash -Algorithm SHA256);
    PowerScheme=(& powercfg /getactivescheme | Out-String).Trim();
    ActiveCompilers=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(cl|link)\.exe$'} | Select-Object Name,ProcessId);
    GitHead=(& git -C $repo rev-parse HEAD | Out-String).Trim(); Dirty=$true
} | ConvertTo-Json -Depth 6 | Set-Content "$results/$Tag-machine.json" -Encoding utf8
$env:ECS_NATIVE_OUTPUT = "$results/$Tag"
$env:ECS_NATIVE_SIZE = "$Count"
$env:ECS_NATIVE_WARMUPS = "$Warmups"
$env:ECS_NATIVE_SAMPLES = "$Samples"
$env:ECS_NATIVE_FILTER = $Filter
$env:DOTNET_TieredCompilation = '0'
$env:DOTNET_ReadyToRun = '0'
$env:DOTNET_EnableDiagnostics = '0'
foreach ($name in @('FUNKE_ECB_TIMINGS','FUNKE_ECB_PARALLEL_DIAGNOSTICS','FUNKE_ECB_SERIAL_KERNELS')) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
$process = Start-Process -FilePath "$runtime/Funke-Runtime.exe" -ArgumentList @("--project=`"$project`"", '--background', '--prefs-profile=ecs-native-comparison') -WorkingDirectory $runtime -WindowStyle Hidden -RedirectStandardOutput "$results/$Tag-stdout.log" -RedirectStandardError "$results/$Tag-stderr.log" -PassThru
@{Pid=$process.Id; Output=$env:ECS_NATIVE_OUTPUT; Language=$Language} | ConvertTo-Json
if ($Wait) {
    $process.WaitForExit()
    @{ ExitCode=$process.ExitCode; Completed=(Get-Date).ToString('o') } | ConvertTo-Json | Set-Content "$results/$Tag-exit.json" -Encoding utf8
    if ($process.ExitCode -ne 0 -or !(Test-Path "$results/$Tag.done")) { throw "Benchmark failed; inspect $results/$Tag-stdout.log and .failed." }
}
