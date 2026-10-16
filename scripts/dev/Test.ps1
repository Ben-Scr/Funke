<#
.SYNOPSIS
    Build and run Index tests with failure-only output.

.EXAMPLE
    scripts\dev\Test.ps1                       # engine + editor doctest suites
    scripts\dev\Test.ps1 -Filter "*Prefab*"    # doctest test-case filter (wildcards)
    scripts\dev\Test.ps1 -Suite CSharp -Filter "Vector3"
    scripts\dev\Test.ps1 -Suite Engine -NoBuild -List
#>
[CmdletBinding()]
param(
    # Engine, Editor, CSharp, All. Default: Engine,Editor.
    [string[]]$Suite = @("Engine", "Editor"),

    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Config = "Release",

    # doctest: -tc filter (wildcards, comma = OR). CSharp: dotnet test --filter expression or substring.
    [string]$Filter = "",

    [switch]$NoBuild,
    [switch]$List,
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Continue"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
. (Join-Path $PSScriptRoot "LoaderExitCodes.ps1")
$Suite = @($Suite | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($Suite -contains "All") { $Suite = @("Engine", "Editor", "CSharp") }
foreach ($s in $Suite) { if (@("Engine", "Editor", "CSharp") -notcontains $s) { throw "Unknown suite '$s'." } }

if (-not $NoBuild) {
    $buildTargets = @()
    if ($Suite -contains "Engine") { $buildTargets += "EngineTests" }
    if ($Suite -contains "Editor") { $buildTargets += "EditorTests" }
    if ($buildTargets.Count -gt 0) {
        & (Join-Path $PSScriptRoot "Build.ps1") -Project $buildTargets -Config $Config -Warnings none
        if ($LASTEXITCODE -ne 0) { Write-Host "[test] build failed - not running tests" -ForegroundColor Red; exit 1 }
    }
}

$failed = $false
Push-Location $repoRoot
try {
    foreach ($s in $Suite) {
        if ($s -eq "CSharp") {
            $csArgs = @("test", "Tests\Index-ScriptCore-Tests\Index-ScriptCore-Tests.csproj", "-c", $Config, "--nologo", "-v", "q")
            if ($NoBuild) { $csArgs += "--no-build" }
            if ($List) { $csArgs += "--list-tests" }
            if ($Filter) {
                # Plain words become a substring match on the test name.
                $expr = $Filter
                if ($Filter -notmatch '[=~&|!]') { $expr = "FullyQualifiedName~$Filter" }
                $csArgs += @("--filter", $expr)
            }
            $out = & dotnet @csArgs 2>&1 | ForEach-Object { "$_" }
            $code = $LASTEXITCODE
            if ($VerboseOutput -or $List -or $code -ne 0) { $out | Write-Host }
            else { $out | Where-Object { $_ -match 'Passed!|Failed!|error|Total tests|Bestanden|Fehler' } | Write-Host }
            if ($code -ne 0) { $failed = $true; Write-Host "[test] CSharp FAILED" -ForegroundColor Red } else { Write-Host "[test] CSharp ok" -ForegroundColor Green }
            continue
        }

        $name = "Index-$s-Tests"
        $exe = Join-Path $repoRoot "bin\$Config-windows-x86_64\$name\$name.exe"
        if (-not (Test-Path -LiteralPath $exe)) { Write-Host "[test] missing $exe" -ForegroundColor Red; $failed = $true; continue }
        $docArgs = @("--no-intro", "--no-version")
        if ($List) { $docArgs += "--list-test-cases" }
        if ($Filter) { $docArgs += "--test-case=$Filter" }
        $out = & $exe @docArgs 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
        $log = Join-Path $repoRoot "bin-int\_agent\test-$s.log"
        [System.IO.File]::WriteAllLines($log, [string[]]$out)
        if ($VerboseOutput -or $List) { $out | Write-Host }
        else {
            # The engine logs to stdout; keep only doctest's own lines (failures carry file:line).
            $out | Where-Object { $_ -notmatch '^\[\d\d:\d\d:\d\d\] \[' -and $_ -notmatch '^=+$' -and $_.Trim() } | Write-Host
            if ($code -ne 0) { Write-Host "[test] full output incl. engine log: bin-int\_agent\test-$s.log" }
        }
        if ($code -ne 0) {
            $failed = $true
            $loaderFailure = Get-LoaderFailure $code
            if ($loaderFailure) { Write-Host "[test] $s did not start: $loaderFailure" -ForegroundColor Red }
            Write-Host "[test] $s FAILED (exit $code)" -ForegroundColor Red
        }
        else { Write-Host "[test] $s ok" -ForegroundColor Green }
    }
}
finally { Pop-Location }

if ($failed) { exit 1 } else { exit 0 }
