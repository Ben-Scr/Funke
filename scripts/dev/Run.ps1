<#
.SYNOPSIS
    Launch an Index app, screenshot its window, close it, and report log
    warnings/errors and crash dumps. Lets an agent verify runtime behaviour.

.DESCRIPTION
    Without -Project a throwaway sandbox project (bin-int/_agent/SandboxProject)
    is staged, so the user's own projects are never touched. -Recent opens the
    project the Launcher used last instead.

    Outputs land in bin-int/_agent/run/: <app>-<n>.png screenshots and
    <app>.log (only the log lines written during this run).

.EXAMPLE
    scripts\dev\Run.ps1                                  # Editor on the sandbox project
    scripts\dev\Run.ps1 -App Runtime -Project C:\Games\Foo -Seconds 5 -Shots 3
    scripts\dev\Run.ps1 -Recent -KeepOpen
    scripts\dev\Run.ps1 -Project bin-int\_agent\My3D -EditorView   # screenshot the Editor View tab instead of the Game View
    scripts\dev\Run.ps1 -App Runtime -Variant 2d -Project <dir>     # module-reduced runtime from premake --without-3d
    scripts\dev\Run.ps1 -App Launcher -LauncherPage templates       # screenshot the Launcher's Templates page
#>
[CmdletBinding()]
param(
    [ValidateSet("Editor", "Runtime", "Launcher")]
    [string]$App = "Editor",

    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Config = "Release",

    # Engine-module runtime variant (bin\<Config>-windows-x86_64-<Variant>), e.g. 2d, 3d, nomodules.
    [string]$Variant = "",

    [string]$Project = "",
    [switch]$Recent,

    # Seconds to wait before the first screenshot.
    [double]$Seconds = 8,
    [int]$Shots = 1,
    [double]$Interval = 2,

    [string[]]$ExtraArgs = @(),
    [switch]$KeepOpen,
    # Editor only: bring the Editor View tab to the front (INDEX_EDITOR_FOCUS_VIEW).
    [switch]$EditorView,
    # Editor only: enter play mode once the startup scene has loaded (INDEX_EDITOR_AUTOPLAY).
    [switch]$Play,
    # Launcher only: start on this page (INDEX_LAUNCHER_PAGE).
    [ValidateSet("", "templates", "assets")]
    [string]$LauncherPage = "",
    [int]$MaxLogLines = 40
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exeName = "Index-$App"
$outputFolder = if ($Variant) { "$Config-windows-x86_64-$Variant" } else { "$Config-windows-x86_64" }
$exe = Join-Path $repoRoot "bin\$outputFolder\$exeName\$exeName.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Not built: $exe (run scripts\dev\Build.ps1 $App -Config $Config)" }
$outDir = Join-Path $repoRoot "bin-int\_agent\run"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Get-ChildItem -LiteralPath $outDir -Filter "$App-*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

Add-Type -AssemblyName System.Drawing
if (-not ("IndexAgent.WindowShot" -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace IndexAgent {
public static class WindowShot {
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    // PW_RENDERFULLCONTENT captures DirectX/Vulkan swapchains even when the window is covered.
    public static bool Capture(IntPtr hwnd, string path) {
        RECT r;
        if (!GetWindowRect(hwnd, out r)) return false;
        int w = r.R - r.L, h = r.B - r.T;
        if (w <= 0 || h <= 0) return false;
        using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            bool ok = PrintWindow(hwnd, hdc, 2);
            g.ReleaseHdc(hdc);
            if (!ok) return false;
            bmp.Save(path, ImageFormat.Png);
        }
        return true;
    }
}
}
"@
}
[void][IndexAgent.WindowShot]::SetProcessDPIAware()

function New-SandboxProject {
    $root = Join-Path $repoRoot "bin-int\_agent\SandboxProject"
    $scenes = Join-Path $root "Assets\Scenes"
    New-Item -ItemType Directory -Force -Path $scenes | Out-Null
    $projectJson = Join-Path $root "project.json"
    if (-not (Test-Path -LiteralPath $projectJson)) {
        $version = "0.0.0-dev"
        $meta = Join-Path (Split-Path $exe -Parent) "BuildMetadata.json"
        if (Test-Path -LiteralPath $meta) { $version = (Get-Content -LiteralPath $meta -Raw | ConvertFrom-Json).version }
        $json = @{
            projectFormatVersion     = 1
            name                     = "AgentSandbox"
            engineVersion            = $version
            startupScene             = "SampleScene"
            lastOpenedScene          = "SampleScene"
            buildScenes              = @("SampleScene")
            assetSerializationFormat = "Json"
        } | ConvertTo-Json
        [System.IO.File]::WriteAllText($projectJson, $json)
        [System.IO.File]::WriteAllText((Join-Path $scenes "SampleScene.scene"), '{ "version": 1, "name": "SampleScene", "entities": [] }')
    }
    return $root
}

if ($App -ne "Launcher") {
    if ($Recent) {
        $launcherJson = Join-Path $env:LOCALAPPDATA "Index\Launcher\launcher.json"
        $entries = Get-Content -LiteralPath $launcherJson -Raw | ConvertFrom-Json
        $Project = ($entries | Sort-Object { $_.lastOpened } -Descending | Select-Object -First 1).path
    }
    if (-not $Project) { $Project = New-SandboxProject }
    $Project = (Resolve-Path -LiteralPath $Project).Path
}

$logPath = Join-Path $env:LOCALAPPDATA "Index\Logs\$exeName.log"
$logOffset = 0
if (Test-Path -LiteralPath $logPath) { $logOffset = (Get-Item -LiteralPath $logPath).Length }
$dumpDir = Join-Path $env:LOCALAPPDATA "Index\CrashDumps"
$started = Get-Date

$argList = @()
if ($Project) { $argList += "--project=`"$Project`"" }
$argList += @($ExtraArgs | Where-Object { $_ })
Write-Host "[run] $exeName $($argList -join ' ')"
# Start-Process rejects an empty -ArgumentList (the Launcher runs without a project).
$startArgs = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe -Parent); PassThru = $true }
if ($argList.Count -gt 0) { $startArgs.ArgumentList = $argList }
$previousFocusView = $env:INDEX_EDITOR_FOCUS_VIEW
$previousLauncherPage = $env:INDEX_LAUNCHER_PAGE
$previousAutoplay = $env:INDEX_EDITOR_AUTOPLAY
if ($Play) { $env:INDEX_EDITOR_AUTOPLAY = "1" }
if ($EditorView) { $env:INDEX_EDITOR_FOCUS_VIEW = "editor" }
if ($LauncherPage) { $env:INDEX_LAUNCHER_PAGE = $LauncherPage }
try { $proc = Start-Process @startArgs }
finally {
    $env:INDEX_EDITOR_FOCUS_VIEW = $previousFocusView
    $env:INDEX_LAUNCHER_PAGE = $previousLauncherPage
    $env:INDEX_EDITOR_AUTOPLAY = $previousAutoplay
}

$shotsTaken = New-Object System.Collections.Generic.List[string]
$deadline = $started.AddSeconds($Seconds)
for ($i = 1; $i -le $Shots; $i++) {
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) { Start-Sleep -Milliseconds 200 }
    if ($proc.HasExited) { break }
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    $shot = Join-Path $outDir "$App-$i.png"
    if ($hwnd -ne [IntPtr]::Zero -and [IndexAgent.WindowShot]::Capture($hwnd, $shot)) {
        $shotsTaken.Add($shot)
    }
    else {
        Write-Host "[run] screenshot $i failed (no main window yet?)" -ForegroundColor Yellow
    }
    $deadline = (Get-Date).AddSeconds($Interval)
}

$exitInfo = ""
if ($KeepOpen) {
    $exitInfo = "left running (pid $($proc.Id))"
}
elseif ($proc.HasExited) {
    $exitInfo = "exited on its own with code $($proc.ExitCode)"
}
else {
    [void]$proc.CloseMainWindow()
    if ($proc.WaitForExit(20000)) {
        $exitInfo = "closed gracefully, exit code $($proc.ExitCode)"
    }
    else {
        $proc.Kill()
        $exitInfo = "did not close within 20s (modal dialog?) - killed"
    }
}

# Log lines written during this run; handles rotation by falling back to the whole file.
$runLog = @()
if (Test-Path -LiteralPath $logPath) {
    $stream = [System.IO.File]::Open($logPath, "Open", "Read", "ReadWrite")
    try {
        if ($stream.Length -ge $logOffset) { [void]$stream.Seek($logOffset, "Begin") }
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $runLog = $reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ }
    }
    finally { $stream.Dispose() }
}
$runLogPath = Join-Path $outDir "$App.log"
[System.IO.File]::WriteAllLines($runLogPath, [string[]]$runLog)

$problems = @($runLog | Where-Object { $_ -match '\] \[(warning|error|critical)\] ' })
$dumps = @()
if (Test-Path -LiteralPath $dumpDir) {
    $dumps = @(Get-ChildItem -LiteralPath $dumpDir -File | Where-Object { $_.LastWriteTime -ge $started })
}

Write-Host "[run] $exitInfo"
Write-Host "[run] log: $($runLog.Count) lines -> $(Join-Path 'bin-int\_agent\run' "$App.log")"
if ($problems.Count -gt 0) {
    Write-Host "[run] $($problems.Count) warning/error lines:" -ForegroundColor Yellow
    $problems | Select-Object -First $MaxLogLines | ForEach-Object { Write-Host "  $_" }
    if ($problems.Count -gt $MaxLogLines) { Write-Host "  ... $($problems.Count - $MaxLogLines) more" }
}
foreach ($d in $dumps) { Write-Host "[run] CRASH DUMP: $($d.FullName)" -ForegroundColor Red }
foreach ($s in $shotsTaken) { Write-Host "[run] screenshot: $s" }

$crashed = $dumps.Count -gt 0 -or ($proc.HasExited -and -not $KeepOpen -and $exitInfo -like "exited on its own*" -and $proc.ExitCode -ne 0)
if ($crashed) { exit 1 } else { exit 0 }
