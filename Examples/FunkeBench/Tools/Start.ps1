[CmdletBinding()]
param(
    [switch]$Editor,
    [switch]$NoBuild,
    [string]$EngineRoot = $env:FUNKE_ENGINE_ROOT
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $EngineRoot) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
    if (Test-Path -LiteralPath (Join-Path $candidate 'Funke-Engine')) { $EngineRoot = $candidate }
    else { $EngineRoot = 'C:\Users\benso\source\repos\Pending\Index' }
}
$appName = if ($Editor) { 'Funke-Editor' } else { 'Funke-Runtime' }
$appDirectory = Join-Path $EngineRoot "bin\Release-windows-x86_64\$appName"
$appExecutable = Join-Path $appDirectory "$appName.exe"
if (-not (Test-Path -LiteralPath $appExecutable)) {
    throw "Funke executable missing: $appExecutable. Set FUNKE_ENGINE_ROOT or pass -EngineRoot."
}
if (-not $NoBuild) {
    $corePath = Join-Path $appDirectory 'Funke-ScriptCore.dll'
    if (-not (Test-Path -LiteralPath $corePath)) { throw "ScriptCore missing: $corePath" }
    & dotnet build (Join-Path $projectRoot 'FunkeBench.csproj') -c Release --nologo -v minimal -m:1 -nr:false -p:UseSharedCompilation=false "-p:FunkeScriptCorePath=$corePath"
    if ($LASTEXITCODE -ne 0) { throw 'FunkeBench script build failed.' }
}
Start-Process -FilePath $appExecutable -WorkingDirectory $appDirectory -ArgumentList "--project=`"$projectRoot`""
