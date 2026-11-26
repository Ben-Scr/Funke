param([string]$EngineRoot = '')
$ErrorActionPreference = 'Stop'
if (-not $EngineRoot) { $EngineRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path }
$runtime = Join-Path $EngineRoot 'bin/Release-windows-x86_64/Funke-Runtime/Funke-Runtime.exe'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Funke Release runtime is missing. Pass -EngineRoot with the engine repository path.' }
& dotnet build (Join-Path $PSScriptRoot 'Factorio.csproj') -c Release -m:1 -nr:false
if ($LASTEXITCODE -ne 0) { throw 'Game script compilation failed.' }
# Portable player saves are separate from Tests/UserData and survive scene restarts.
$env:FUNKE_USER_DATA_ROOT = Join-Path $PSScriptRoot 'UserData'
& $runtime "--project=$PSScriptRoot"
