[CmdletBinding()]
param(
    [switch]$Run,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string[]]$Case = @('d3d12', 'vulkan', 'fallback'),
    [string]$RunId = (Get-Date -Format 'yyyyMMdd-HHmmss')
)

$ErrorActionPreference = 'Stop'
if (-not $Run) { throw 'Prepared only. Pass -Run after the coordinated engine build is complete.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$editor = Join-Path $repoRoot "bin/$Config-windows-x86_64/Funke-Editor/Funke-Editor.exe"
foreach ($file in @($editor)) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing binary: $file" }
}
$matrix = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'matrix.json') -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'source-manifest.json') -Raw | ConvertFrom-Json
foreach ($name in $Case) {
    if (-not ($matrix | Where-Object Name -eq $name)) { throw "Unknown smoke case: $name" }
}
$runRoot = Join-Path $PSScriptRoot "runs/$RunId"
if (Test-Path -LiteralPath $runRoot) { throw "Run output already exists: $runRoot" }
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$savedEnvironment = @{}
foreach ($name in @('LOCALAPPDATA', 'FUNKE_USER_DATA_ROOT', 'FUNKE_DISABLE_GPU_ADAPTER_FAST_PATH', 'FUNKE_EDITOR_AUTOPLAY',
    'FUNKE_EDITOR_AUTOSTOP', 'FUNKE_EDITOR_AUTORELOAD', 'FUNKE_EDITOR_BRIDGE')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Assert-SourceUnchanged {
    foreach ($item in $manifest) {
        if ((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.Sha256) {
            throw "Source fixture changed: $($item.Path)"
        }
    }
}

function Invoke-Bridge {
    param([string]$Command, [hashtable]$Arguments = @{}, [string]$OutputName, [int]$Timeout = 60)
    $instance = Get-Content -LiteralPath $script:discovery -Raw | ConvertFrom-Json
    if ($instance.pid -ne $script:editorProcess.Id -or $instance.project -ne $script:fixture) {
        throw 'Bridge discovery does not match the owned fixture process'
    }
    $pipeName = $instance.endpoint.Substring('\\.\pipe\'.Length)
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $pipeName, [IO.Pipes.PipeDirection]::InOut,
        [IO.Pipes.PipeOptions]::Asynchronous)
    $reader = $null
    $writer = $null
    try {
        $pipe.Connect(3000)
        $writer = [IO.StreamWriter]::new($pipe, [Text.UTF8Encoding]::new($false), 4096, $true)
        $reader = [IO.StreamReader]::new($pipe, [Text.Encoding]::UTF8, $false, 4096, $true)
        $request = @{id='gpu-smoke';token=$instance.token;command=$Command;args=$Arguments;timeoutMs=$Timeout*1000}
        $writer.WriteLine(($request | ConvertTo-Json -Depth 6 -Compress))
        $writer.Flush()
        $pending = $reader.ReadLineAsync()
        if (-not $pending.Wait(($Timeout + 5) * 1000)) { throw "Bridge '$Command' timed out" }
        $raw = $pending.Result
    }
    finally {
        if ($writer) { $writer.Dispose() }
        if ($reader) { $reader.Dispose() }
        $pipe.Dispose()
    }
    if ($OutputName) {
        [IO.File]::WriteAllText((Join-Path $script:caseOutput ($OutputName + '.json')), $raw)
    }
    $response = $raw | ConvertFrom-Json
    if (-not $response.ok) {
        throw "Bridge '$Command' rejected: $raw"
    }
    return $response.result
}

$results = [System.Collections.Generic.List[object]]::new()
try {
    Assert-SourceUnchanged
    $binaryHashes = foreach ($file in @($editor, (Join-Path (Split-Path $editor) 'Funke-Engine.dll'))) {
        Get-FileHash -LiteralPath $file -Algorithm SHA256 | Select-Object Path, Hash
    }
    $binaryHashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'binaries.json') -Encoding utf8
    foreach ($entry in $matrix | Where-Object { $_.Name -in $Case }) {
        $script:fixture = (Resolve-Path (Join-Path $PSScriptRoot ('fixtures/' + $entry.Name))).Path
        $script:caseOutput = Join-Path $runRoot $entry.Name
        New-Item -ItemType Directory -Path $script:caseOutput -Force | Out-Null
        $userData = Join-Path $script:caseOutput 'user-data'
        New-Item -ItemType Directory -Path $userData -Force | Out-Null
        $scriptCoreDestination = Join-Path $script:fixture 'Packages/Funke-ScriptCore'
        New-Item -ItemType Directory -Path $scriptCoreDestination -Force | Out-Null
        foreach ($extension in 'dll', 'pdb', 'xml') {
            $source = Join-Path (Split-Path $editor) ('Funke-ScriptCore.' + $extension)
            if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $scriptCoreDestination }
        }
        $env:LOCALAPPDATA = $userData
        $env:FUNKE_USER_DATA_ROOT = $userData
        $env:FUNKE_EDITOR_BRIDGE = '1'
        foreach ($name in 'FUNKE_DISABLE_GPU_ADAPTER_FAST_PATH', 'FUNKE_EDITOR_AUTOPLAY',
            'FUNKE_EDITOR_AUTOSTOP', 'FUNKE_EDITOR_AUTORELOAD') {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        $script:editorProcess = $null
        $failure = $null
        $shots = [System.Collections.Generic.List[string]]::new()
        try {
            $script:editorProcess = Start-Process -FilePath $editor -WorkingDirectory (Split-Path $editor) `
                -ArgumentList @('--background', '--mute', "--project=`"$script:fixture`"") `
                -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $script:caseOutput 'stdout.log') `
                -RedirectStandardError (Join-Path $script:caseOutput 'stderr.log')
            $script:discovery = Join-Path $userData "Funke/EditorBridge/$($script:editorProcess.Id).json"
            $deadline = [DateTime]::UtcNow.AddSeconds(60)
            while (-not (Test-Path -LiteralPath $discovery)) {
                if ($script:editorProcess.HasExited) { throw "Editor exited before bridge discovery: $($script:editorProcess.ExitCode)" }
                if ([DateTime]::UtcNow -ge $deadline) { throw 'Editor bridge discovery timed out' }
                Start-Sleep -Milliseconds 250
            }
            do {
                $status = Invoke-Bridge -Command 'status' -OutputName 'startup-status'
                if ($status.scene -eq 'SampleScene' -and $status.state -eq 'edit' -and -not $status.scripts.compiling) { break }
                if ([DateTime]::UtcNow -ge $deadline) { throw 'Initial scene readiness timed out' }
                Start-Sleep -Milliseconds 250
            } while ($true)
            $compile = Invoke-Bridge -Command 'recompile' -OutputName 'recompile' -Timeout 60
            if (-not $compile.succeeded) { throw "Fixture script build failed: $($compile.errors)" }
            foreach ($scene in $entry.Scenes) {
                $play = Invoke-Bridge -Command 'play' -Arguments @{scene=$scene;discard=$true;size='1280x720';
                    fixedStep='60';frames=8} -OutputName ($scene + '-play') -Timeout 60
                $status = Invoke-Bridge -Command 'status' -OutputName ($scene + '-playing-status')
                if ($status.state -ne 'playing' -or $status.scene -ne $scene -or -not $status.scripts.lastBuildSucceeded) {
                    throw "Unexpected playing state for ${scene}: $($status | ConvertTo-Json -Compress -Depth 5)"
                }
                $shot = Join-Path $script:caseOutput ($scene + '.png')
                $capture = Invoke-Bridge -Command 'screenshot' -Arguments @{path=$shot;width=1280;height=720} -OutputName ($scene + '-capture')
                if (-not (Test-Path -LiteralPath $shot) -or (Get-Item -LiteralPath $shot).Length -lt 1000) {
                    throw "Screenshot missing or implausibly small: $shot"
                }
                $shots.Add($shot)
                $stopped = Invoke-Bridge -Command 'stop' -OutputName ($scene + '-stop')
                $status = Invoke-Bridge -Command 'status' -OutputName ($scene + '-stopped-status')
                if ($status.state -ne 'edit') { throw "Play teardown did not return to edit mode: $scene" }
            }
            $logs = Invoke-Bridge -Command 'logs' -Arguments @{level='warning';tail=200} -OutputName 'warnings'
        }
        catch { $failure = $_.Exception.Message }
        finally {
            if ($script:editorProcess -and -not $script:editorProcess.HasExited) {
                $script:editorProcess.Refresh()
                $closed = $script:editorProcess.CloseMainWindow()
                if (-not $closed -or -not $script:editorProcess.WaitForExit(15000)) {
                    Stop-Process -Id $script:editorProcess.Id -Force
                    $script:editorProcess.WaitForExit()
                    $failure = "$failure Editor did not close gracefully."
                }
            }
            if ($script:editorProcess -and $script:editorProcess.HasExited -and $script:editorProcess.ExitCode -ne 0) {
                $failure = "$failure Editor exit code $($script:editorProcess.ExitCode)."
            }
        }
        $logFile = Join-Path $userData 'Funke/Logs/Funke-Editor.log'
        $logText = if (Test-Path -LiteralPath $logFile) { Get-Content -LiteralPath $logFile -Raw } else { '' }
        $adapter = [regex]::Match($logText, "WebGPU initialized.+adapter='([^']+)'.+backend=(\w+)")
        if (-not $adapter.Success -or $adapter.Groups[2].Value -ne $entry.Expected) {
            $failure = "$failure Expected backend $($entry.Expected), got $($adapter.Value)."
        }
        if ($entry.Name -eq 'fallback' -and $logText -notmatch 'Metal backend unavailable') {
            $failure = "$failure Metal fallback was not exercised."
        }
        $gpuErrors = @([regex]::Matches($logText,
            '(?im)^.*(?:WebGPU \[(?:Validation|OutOfMemory|Internal)\]|Device lost|WGSL compile failed|CreateRenderPipeline.*failed|Unhandled exception|\[critical\]).*$') | ForEach-Object Value)
        if ($gpuErrors.Count -gt 0) { $failure = "$failure GPU errors detected." }
        $result = [pscustomobject]@{ Case=$entry.Name; RequestedBackend=$entry.Backend; Backend=$adapter.Groups[2].Value;
            Adapter=$adapter.Groups[1].Value; Success=[string]::IsNullOrWhiteSpace($failure); Error=$failure;
            Screenshots=@($shots); GpuErrors=$gpuErrors; Log=$logFile }
        $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $script:caseOutput 'result.json') -Encoding utf8
        $results.Add($result)
        Write-Host "[gpu-smoke] $($entry.Name): success=$($result.Success), backend=$($result.Backend), adapter=$($result.Adapter)"
    }
    Assert-SourceUnchanged
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    $results.ToArray() | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
}
if (@($results | Where-Object { -not $_.Success }).Count -gt 0) { exit 1 }
