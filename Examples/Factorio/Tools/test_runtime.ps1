param([ValidateSet('Freeplay','CreativeUI','SurvivalUI','Continue','BuildRoute')][string]$Scenario = 'Freeplay')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$engineRoot = (Resolve-Path (Join-Path $projectRoot '../..')).Path
$runtimeDir = Join-Path $engineRoot 'bin/Release-windows-x86_64/Funke-Runtime'
$testExe = Join-Path $runtimeDir 'Factorio-Freeplay-QA.exe'
Copy-Item -LiteralPath (Join-Path $runtimeDir 'Funke-Runtime.exe') -Destination $testExe -Force
$evidenceDir = Join-Path $projectRoot ('Tests/Evidence/' + $Scenario)
New-Item -ItemType Directory -Force $evidenceDir | Out-Null
$inputPath = Join-Path $projectRoot ('Tests/Input/' + $Scenario + '.json')
$env:CI = '1'
$env:FUNKE_USER_DATA_ROOT = Join-Path $projectRoot 'Tests/UserData'
$logPath = Join-Path $env:FUNKE_USER_DATA_ROOT 'Funke/Logs/Factorio-Freeplay-QA.log'
$logOffset = if (Test-Path -LiteralPath $logPath) { @(Get-Content -LiteralPath $logPath).Count } else { 0 }
$scene = if ($Scenario -in @('Freeplay','BuildRoute')) { 'Factory' } else { 'MainMenu' }
$argsList = @("--project=`"$projectRoot`"", "--scene=$scene", '--background', '--prefs-profile=factorio-freeplay-qa',
    "--input-script=`"$inputPath`"", "--screenshot-dir=`"$evidenceDir`"", '--quit-after=45', '--fixed-step=60')
$process = Start-Process -FilePath $testExe -ArgumentList $argsList -WorkingDirectory $runtimeDir -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(55000)) { Stop-Process -Id $process.Id; throw 'Factory runtime timed out.' }
$process.Refresh()
$runLog = @(Get-Content -LiteralPath $logPath | Select-Object -Skip $logOffset)
$evidenceLog = Join-Path $evidenceDir 'runtime.log'
$runLog | Set-Content -LiteralPath $evidenceLog -Encoding utf8
$runLog | Select-String -Pattern '\[Factory|PASS |error\]|critical\]|Exception|mismatch|Quit at frame|screenshot' | Select-Object -Last 85
Write-Output "Runtime exit: $($process.ExitCode); evidence: $evidenceDir"
if ($process.ExitCode -ne 0) { throw "Runtime exit $($process.ExitCode)" }
if (Select-String -LiteralPath $evidenceLog -Pattern '\[error\]|\[critical\]|Exception|FAIL:|Unknown command' -Quiet) { throw 'Runtime reported errors; inspect runtime.log.' }
$markers = switch ($Scenario) {
    'Freeplay' { 'FACTORY_FREEPLAY_PASS'; 'PASS paused'; 'PASS complete state roundtrip' }
    'CreativeUI' { 'PASS creative'; 'PASS crafted'; 'PASS input placement'; 'PASS paused' }
    'SurvivalUI' { 'PASS normal'; 'PASS mined'; 'PASS inventory'; 'PASS closed' }
    'Continue' { 'PASS loaded' }
    'BuildRoute' { 'PASS input placement' }
}
foreach ($marker in $markers) {
    if (-not (Select-String -LiteralPath $evidenceLog -Pattern $marker -Quiet)) { throw "Missing success marker: $marker" }
}
Remove-Item -LiteralPath $testExe
