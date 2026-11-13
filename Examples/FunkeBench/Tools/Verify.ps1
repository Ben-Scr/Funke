[CmdletBinding()]
param(
    [string]$EngineRoot = $env:FUNKE_ENGINE_ROOT,
    [string]$InputScript = 'Tests\Input\Smoke.json',
    [ValidateRange(1, 600)][int]$TimeoutSeconds = 60,
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $EngineRoot) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
    if (Test-Path -LiteralPath (Join-Path $candidate 'Funke-Engine')) { $EngineRoot = $candidate }
    else { $EngineRoot = 'C:\Users\benso\source\repos\Pending\Index' }
}
$runtimeDirectory = Join-Path $EngineRoot 'bin\Release-windows-x86_64\Funke-Runtime'
$runtimeSource = Join-Path $runtimeDirectory 'Funke-Runtime.exe'
if (-not (Test-Path -LiteralPath $runtimeSource)) { throw "Runtime missing: $runtimeSource" }
$scriptPath = if ([IO.Path]::IsPathRooted($InputScript)) { $InputScript } else { Join-Path $projectRoot $InputScript }
if (-not (Test-Path -LiteralPath $scriptPath)) { throw "Test input missing: $scriptPath" }
$inputDefinition = Get-Content -LiteralPath $scriptPath -Raw | ConvertFrom-Json
$expectedChecks = 0
foreach ($step in $inputDefinition.steps) {
    $expectedChecks += [regex]::Matches([string]$step.command, '(?:^|;)\s*bench\.check(?:-result|-export)?(?=\s|;|$)').Count
}
if ($expectedChecks -eq 0) { throw 'Verification input must contain benchmark assertions.' }
$testName = 'FunkeBench-Verify'
$outputDirectory = Join-Path $projectRoot ('Tests\Output\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
$isolatedRuntime = Join-Path $outputDirectory 'Runtime'
New-Item -ItemType Directory -Force -Path $isolatedRuntime | Out-Null
$testExecutable = Join-Path $isolatedRuntime "$testName.exe"
Copy-Item -LiteralPath $runtimeSource -Destination $testExecutable
Get-ChildItem -LiteralPath $runtimeDirectory -File | Where-Object {
    $_.Name -match '\.dll$|^Funke-ScriptCore\.(deps|runtimeconfig)\.json$'
} | Copy-Item -Destination $isolatedRuntime
Copy-Item -LiteralPath (Join-Path $runtimeDirectory 'FunkeAssets') -Destination $isolatedRuntime -Recurse
if (-not $NoBuild) {
    $corePath = Join-Path $isolatedRuntime 'Funke-ScriptCore.dll'
    & dotnet build (Join-Path $projectRoot 'FunkeBench.csproj') -c Release --nologo -v minimal -m:1 -nr:false -p:UseSharedCompilation=false "-p:FunkeScriptCorePath=$corePath"
    if ($LASTEXITCODE -ne 0) { throw 'FunkeBench script build failed.' }
}
$previousUserData = $env:FUNKE_USER_DATA_ROOT
$env:FUNKE_USER_DATA_ROOT = Join-Path $outputDirectory 'UserData'
$testLog = Join-Path $env:FUNKE_USER_DATA_ROOT "Funke\Logs\$testName.log"
$testProcess = $null
try {
    $runArguments = @(
        "--project=`"$projectRoot`"",
        '--background',
        '--prefs-profile=funke-bench-verification',
        "--input-script=`"$scriptPath`"",
        "--screenshot-dir=`"$outputDirectory`"",
        "--quit-after=$TimeoutSeconds"
    )
    $testProcess = Start-Process -FilePath $testExecutable -WorkingDirectory $isolatedRuntime -ArgumentList $runArguments -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 15)
    while (-not $testProcess.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'FunkeBench verification timed out.' }
    }
    $testProcess.Refresh()
    if (-not (Test-Path -LiteralPath $testLog)) { throw 'Runtime did not write its log.' }
    Copy-Item -LiteralPath $testLog -Destination (Join-Path $outputDirectory 'Runtime.log')
    $logText = Get-Content -LiteralPath $testLog -Raw
    $failures = @($logText -split "`r?`n" | Where-Object { $_ -match '\[(error|critical)\]|Exception in|Unhandled exception|BENCH_CHECK_FAIL' })
    if ($testProcess.ExitCode -ne 0 -or $failures.Count -gt 0) {
        $failures | ForEach-Object { Write-Output $_ }
        throw "Runtime verification failed (exit $($testProcess.ExitCode)). See $outputDirectory"
    }
    $actualChecks = [regex]::Matches($logText, 'BENCH_CHECK_PASS:').Count
    if ($actualChecks -ne $expectedChecks) {
        throw "Incomplete verification: expected $expectedChecks successful assertions, got $actualChecks. See $outputDirectory"
    }
    if ($logText -match '--quit-after .*elapsed') { throw "Verification reached its safety timeout. See $outputDirectory" }
    $logText -split "`r?`n" | Where-Object { $_ -match 'BENCH_CHECK|\[FunkeBench\]' } | ForEach-Object { Write-Output $_ }
    Write-Output "Verification output: $outputDirectory"
}
finally {
    if ($null -ne $testProcess -and -not $testProcess.HasExited) { Stop-Process -Id $testProcess.Id -Force }
    $env:FUNKE_USER_DATA_ROOT = $previousUserData
}
