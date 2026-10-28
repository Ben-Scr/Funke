param([string]$Tag = ("run-" + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = "Stop"
foreach ($count in @(1000, 10000, 100000)) {
    & (Join-Path $PSScriptRoot 'Run.ps1') -Tag "$Tag-$count" -Sizes "$count" -Wait
}
