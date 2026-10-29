# Creates a self-contained input bundle and a Windows Sandbox recipe without enabling virtualization.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Install,
    [Parameter(Mandatory = $true)][string]$DotnetRoot,
    [Parameter(Mandatory = $true)][string]$PythonRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$WithTween,
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$installPath = (Resolve-Path -LiteralPath $Install).Path
$dotnetPath = (Resolve-Path -LiteralPath $DotnetRoot).Path
$pythonPath = (Resolve-Path -LiteralPath $PythonRoot).Path
$destination = [IO.Path]::GetFullPath($OutputDirectory)
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
foreach ($parent in @($repository, $installPath, $dotnetPath, $pythonPath)) {
    if ($destination.Equals($parent, [StringComparison]::OrdinalIgnoreCase) -or
        $destination.StartsWith($parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "The Sandbox bundle must be outside the checkout and every input directory: $parent"
    }
}
if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite an existing directory: $destination" }
if (-not (Test-Path -LiteralPath (Join-Path $dotnetPath 'dotnet.exe') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $dotnetPath 'sdk') -PathType Container)) {
    throw 'DotnetRoot must contain an offline .NET 10 SDK installation.'
}
$pythonExe = Join-Path $pythonPath 'python.exe'
if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw 'PythonRoot must be a complete relocatable Python installation containing python.exe.'
}
foreach ($runtimePath in @($dotnetPath, $pythonPath)) {
    if (Test-Path -LiteralPath (Join-Path $runtimePath 'Funke-Engine\src')) {
        throw "Runtime input contains an engine checkout: $runtimePath"
    }
    if ((Get-Item -LiteralPath $runtimePath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Runtime input cannot be a reparse point: $runtimePath"
    }
    $links = Get-ChildItem -LiteralPath $runtimePath -Recurse -Force -Attributes ReparsePoint
    if ($links) { throw "Runtime input contains reparse points: $runtimePath" }
}
& $pythonExe -c 'import sys; sys.path.insert(0, sys.argv[1]); import standalone_sdk_test as gate; from pathlib import Path; gate.validate_installation(Path(sys.argv[2]))' $PSScriptRoot $installPath
if ($LASTEXITCODE -ne 0) { throw 'Binary installation validation failed before bundling.' }

$inputs = Join-Path $destination 'Input'
$results = Join-Path $destination 'Results'
New-Item -ItemType Directory -Path $inputs, $results | Out-Null
Copy-Item -LiteralPath $installPath -Destination (Join-Path $inputs 'installation') -Recurse
Copy-Item -LiteralPath $dotnetPath -Destination (Join-Path $inputs 'dotnet') -Recurse
Copy-Item -LiteralPath $pythonPath -Destination (Join-Path $inputs 'python') -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'standalone_sdk_test.py') -Destination $inputs
$manifest = [ordered]@{
    schemaVersion = 1
    isolation = 'Windows Sandbox recipe; not evidence of execution'
    network = 'disabled'
    inputReadOnly = $true
    hostMappings = @($inputs, $results)
    withTween = [bool]$WithTween
    harnessSha256 = (Get-FileHash -LiteralPath (Join-Path $inputs 'standalone_sdk_test.py') -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $inputs 'recipe.json') -Encoding UTF8

$bootstrap = @'
$ErrorActionPreference = 'Stop'
$report = 'C:\StandaloneResults\evidence.json'
$exit = 1
try {
    New-Item -ItemType Directory -Path 'C:\FunkeStandalone' -Force | Out-Null
    $recipe = Get-Content -LiteralPath 'C:\StandaloneInput\recipe.json' -Raw | ConvertFrom-Json
    $arguments = @('C:\StandaloneInput\standalone_sdk_test.py', '--install', 'C:\StandaloneInput\installation',
        '--dotnet', 'C:\StandaloneInput\dotnet\dotnet.exe', '--work-parent', 'C:\FunkeStandalone', '--report', $report)
    if ($recipe.withTween) { $arguments += '--with-tween' }
    & 'C:\StandaloneInput\python\python.exe' @arguments *> 'C:\StandaloneResults\runner.log'
    $exit = $LASTEXITCODE
} catch {
    $_ | Out-String | Set-Content -LiteralPath 'C:\StandaloneResults\bootstrap-error.log'
} finally {
    if (Test-Path -LiteralPath $report) {
        $evidence = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        $evidence | Add-Member -NotePropertyName isolation -NotePropertyValue 'windows-sandbox' -Force
        $evidence | Add-Member -NotePropertyName sandboxRecipe -NotePropertyValue $recipe -Force
        $evidence | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $report -Encoding UTF8
        if ($evidence.workRoot -and (Test-Path -LiteralPath $evidence.workRoot)) {
            $artifactRoot = 'C:\StandaloneResults\Artifacts'
            New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
            foreach ($name in @('logs', 'editor.png', 'editor-probe.txt', 'guard-self-test.jsonl', 'forbidden-tools.jsonl')) {
                $path = Join-Path $evidence.workRoot $name
                if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $artifactRoot -Recurse }
            }
            if ($evidence.playerRoot -and (Test-Path -LiteralPath $evidence.playerRoot)) {
                Get-ChildItem -LiteralPath $evidence.playerRoot -Filter '*-probe.txt' |
                    Copy-Item -Destination $artifactRoot
            }
        }
    } else {
        @{schemaVersion = 1; status = 'failed'; isolation = 'windows-sandbox'; error = 'Bootstrap failed before harness report; inspect runner/bootstrap logs.'} |
            ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
    }
    Set-Content -LiteralPath 'C:\StandaloneResults\exit-code.txt' -Value $exit
}
'@
$bootstrap | Set-Content -LiteralPath (Join-Path $inputs 'Run.ps1') -Encoding UTF8
$escapedInput = [Security.SecurityElement]::Escape($inputs)
$escapedResults = [Security.SecurityElement]::Escape($results)
$wsb = @"
<Configuration>
  <VGpu>Enable</VGpu>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <PrinterRedirection>Disable</PrinterRedirection>
  <MappedFolders>
    <MappedFolder><HostFolder>$escapedInput</HostFolder><SandboxFolder>C:\StandaloneInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$escapedResults</HostFolder><SandboxFolder>C:\StandaloneResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\StandaloneInput\Run.ps1</Command></LogonCommand>
</Configuration>
"@
$recipePath = Join-Path $destination 'Funke-Standalone.wsb'
$wsb | Set-Content -LiteralPath $recipePath -Encoding UTF8
Write-Output "Prepared $recipePath"
Write-Output "Execution evidence will be written to $results"
if ($Launch) {
    $sandbox = Join-Path $env:SystemRoot 'System32\WindowsSandbox.exe'
    if (-not (Test-Path -LiteralPath $sandbox)) { throw 'Windows Sandbox is unavailable; recipe prepared, no execution performed.' }
    Start-Process -FilePath $sandbox -ArgumentList @('"' + $recipePath + '"') -WindowStyle Hidden
}
