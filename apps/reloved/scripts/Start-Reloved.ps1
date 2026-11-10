$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectPath
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
if (-not $nodeCommand) {
    $runtime = Get-AppxPackage -Name 'OpenAI.CodexPrimaryRuntime*' | Select-Object -First 1
    if ($runtime) {
        $nodeCommand = Get-ChildItem -LiteralPath $runtime.InstallLocation -Filter node.exe -Recurse | Select-Object -First 1
    }
}
if (-not $nodeCommand) { throw 'Bitte installiere Node.js 24 LTS von https://nodejs.org und starte Reloved erneut.' }
$nodePath = if ($nodeCommand.Source) { $nodeCommand.Source } else { $nodeCommand.FullName }
$env:PATH = (Split-Path $nodePath) + ';' + $env:PATH
if (-not (Test-Path -LiteralPath 'node_modules/vinext/dist/cli.js')) {
    $npmCommand = Get-Command npm.cmd -ErrorAction SilentlyContinue
    if (-not $npmCommand) { throw 'Bitte installiere Node.js inklusive npm von https://nodejs.org.' }
    & $npmCommand.Source ci
    if ($LASTEXITCODE -ne 0) { throw 'Die Installation ist fehlgeschlagen.' }
}
try {
    $response = Invoke-WebRequest -Uri 'http://127.0.0.1:5173' -TimeoutSec 2 -UseBasicParsing
    if ($response.Content -match 'Reloved') {
        Start-Process 'http://127.0.0.1:5173'
        exit 0
    }
    throw 'Port 5173 wird von einer anderen App verwendet.'
} catch [System.Net.WebException] { }
Write-Host 'Reloved startet. Dieses Fenster zum Beenden schliessen.'
if (-not (Test-Path -LiteralPath 'dist/server/index.js')) {
    & $nodePath 'node_modules/vinext/dist/cli.js' build
    if ($LASTEXITCODE -ne 0) { throw 'Die App konnte nicht vorbereitet werden.' }
}
$serverArgs = @('node_modules/vinext/dist/cli.js', 'start', '--hostname', '127.0.0.1', '--port', '5173')
$serverProcess = Start-Process -FilePath $nodePath -ArgumentList $serverArgs -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru
try {
    $healthy = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if ($serverProcess.HasExited) { throw 'Reloved konnte nicht gestartet werden.' }
        try {
            $response = Invoke-WebRequest -Uri 'http://127.0.0.1:5173' -TimeoutSec 1 -UseBasicParsing
            if ($response.Content -match 'Reloved') { $healthy = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 500
    }
    if (-not $healthy) { throw 'Reloved antwortet nicht. Bitte versuche es erneut.' }
    Start-Process 'http://127.0.0.1:5173'
    Read-Host 'Reloved ist bereit. Zum Beenden Enter druecken'
} finally {
    if (-not $serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id }
}
