<#
.SYNOPSIS
    Launch a Funke app, screenshot its window, close it, and report log
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
    scripts\dev\Run.ps1 -Project bin-int\_agent\My3D -EditorView -DrawMode triangle   # Editor View in the Triangle draw mode
    scripts\dev\Run.ps1 -App Runtime -Variant 2d -Project <dir>     # module-reduced runtime from premake --without-3d
    scripts\dev\Run.ps1 -App Launcher -LauncherPage templates       # screenshot the Launcher's Templates page
    scripts\dev\Run.ps1 -OpenWindow build,buildprofiles             # editor with those windows open
    scripts\dev\Run.ps1 -Theme glass                                # editor in the Liquid Glass theme, prefs untouched
    scripts\dev\Run.ps1 -App Runtime -Project <dir> -ScreenshotAt 2s,5s -FixedStep 60   # engine screenshots at game time
    scripts\dev\Run.ps1 -App Runtime -Project <dir> -InputScript Tests\level5.json      # scripted input, quits itself
    scripts\dev\Run.ps1 -App Runtime -Project <dir> -Exec "physics.debug on" -ScreenshotAt 3s

    Windows start with --background: they never take focus, open behind the user's windows and keep
    running unfocused (fullscreen/maximized become a plain window filling the screen). -Foreground opts out.

    Runtime runs use the isolated player-data profile "agent" (PlayerPrefs/SaveData under
    Profiles\agent), so a test never changes the player's saves; -RealPrefs turns that off.
    With -ScreenshotAt / -InputScript / -QuitAt the game takes its own screenshots
    (bin-int/_agent/run/shots-<time>-<pid>/at-<time>.png) and quits itself; -Seconds is then only the timeout.
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
    # Let the window take focus and come to the front (default: --background, it opens behind the user's windows).
    [switch]$Foreground,
    # Editor only: bring the Editor View tab to the front (FUNKE_EDITOR_FOCUS_VIEW).
    [switch]$EditorView,
    # Editor only: bring the Game View tab to the front (FUNKE_EDITOR_FOCUS_VIEW=game).
    [switch]$GameView,
    # Editor only: enter play mode once the startup scene has loaded (FUNKE_EDITOR_AUTOPLAY).
    [switch]$Play,
    # Editor only: leave play mode again after this many seconds (FUNKE_EDITOR_AUTOSTOP); implies -Play.
    [double]$StopAfter = 0,
    # Editor only: reload the editor in-process once after this many seconds (FUNKE_EDITOR_AUTORELOAD).
    [double]$ReloadAfter = 0,
    # Editor only, with -ReloadAfter: remove this engine module (e.g. Funke.2D) right before the reload.
    [string]$RemoveModule = "",
    # Editor only: open these windows once loaded (FUNKE_EDITOR_OPEN): build, buildprofiles, projectsettings,
    # preferences, packages, profiler, gamestats (the Game View's Stats overlay). projectsettings:graphics (display,
    # branding, build, ...) also picks the category.
    [string[]]$OpenWindow = @(),
    # Editor only: select the first entity with this name once loaded and frame it in the 3D Editor View (FUNKE_EDITOR_SELECT).
    [string]$Select = "",
    # Editor only: start the Editor View in this draw mode (FUNKE_EDITOR_DRAW_MODE).
    [ValidateSet("", "triangle", "mixed")]
    [string]$DrawMode = "",
    # Editor only: show this theme for the run without saving it to the user's preferences (FUNKE_EDITOR_THEME).
    [ValidateSet("", "auto", "dark", "light", "glass", "custom")]
    [string]$Theme = "",
    # Launcher only: start on this page (FUNKE_LAUNCHER_PAGE).
    [ValidateSet("", "templates", "assets")]
    [string]$LauncherPage = "",
    # Runtime only: engine screenshots at game time ("2s", "2.5") or frame ("120f").
    [string[]]$ScreenshotAt = @(),
    # Runtime only: JSON input script (keys, mouse, touch, commands, screenshots, quit); see Editor/Testing.md.
    [string]$InputScript = "",
    # Runtime only: every frame advances exactly 1/FixedStep seconds.
    [double]$FixedStep = 0,
    # Runtime only: quit at this game time or frame.
    [string]$QuitAt = "",
    # Runtime only: dev console commands on the first game frame ("physics.debug on; timescale 2").
    [string]$Exec = "",
    # Runtime only: player-data profile; -RealPrefs uses the player's real PlayerPrefs/SaveData.
    [string]$PrefsProfile = "agent",
    [switch]$RealPrefs,
    [int]$MaxLogLines = 40
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
. (Join-Path $PSScriptRoot "LoaderExitCodes.ps1")
$exeName = "Funke-$App"
$outputFolder = if ($Variant) { "$Config-windows-x86_64-$Variant" } else { "$Config-windows-x86_64" }
$exe = Join-Path $repoRoot "bin\$outputFolder\$exeName\$exeName.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Not built: $exe (run scripts\dev\Build.ps1 $App -Config $Config)" }
$outDir = Join-Path $repoRoot "bin-int\_agent\run"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Get-ChildItem -LiteralPath $outDir -Filter "$App-*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
# Game screenshots get a folder per run: parallel sessions share bin-int\_agent\run and clean it up.
$shotDir = Join-Path $outDir ("shots-{0:yyyyMMdd-HHmmss}-{1}" -f (Get-Date), $PID)

$ScreenshotAt = @($ScreenshotAt | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$engineDriven = $App -eq "Runtime" -and ($ScreenshotAt.Count -gt 0 -or $InputScript -or $QuitAt)
if ($App -ne "Runtime" -and ($ScreenshotAt.Count -gt 0 -or $InputScript -or $QuitAt -or $FixedStep -gt 0 -or $Exec)) {
    throw "-ScreenshotAt, -InputScript, -QuitAt, -FixedStep and -Exec need -App Runtime"
}
# Without an explicit end the game quits shortly after its last screenshot.
if ($engineDriven -and -not $QuitAt -and -not $InputScript) {
    $frames = @($ScreenshotAt | Where-Object { $_ -match '^\d+f$' } | ForEach-Object { [int]($_.TrimEnd('f')) })
    $times = @($ScreenshotAt | Where-Object { $_ -notmatch 'f$' } | ForEach-Object {
        if ($_ -match '^([\d.]+)ms$') { [double]$Matches[1] / 1000 } else { [double]($_.TrimEnd('s')) } })
    $invariant = [Globalization.CultureInfo]::InvariantCulture
    if ($frames.Count -eq 0) { $QuitAt = [string]::Format($invariant, "{0}s", ($times | Measure-Object -Maximum).Maximum + 0.5) }
    elseif ($times.Count -eq 0) { $QuitAt = [string]::Format($invariant, "{0}f", ($frames | Measure-Object -Maximum).Maximum + 30) }
}

Add-Type -AssemblyName System.Drawing
if (-not ("FunkeAgent.WindowShot" -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace FunkeAgent {
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
[void][FunkeAgent.WindowShot]::SetProcessDPIAware()

function Read-LogHead([string]$path, [int]$length) {
    $stream = [System.IO.File]::Open($path, "Open", "Read", "ReadWrite")
    try {
        $buffer = New-Object byte[] $length
        $read = $stream.Read($buffer, 0, $buffer.Length)
        if ($read -le 0) { return $null }
        return [System.Convert]::ToBase64String($buffer, 0, $read)
    }
    finally { $stream.Dispose() }
}

function Read-LogLines([string]$path, [long]$offset) {
    $stream = [System.IO.File]::Open($path, "Open", "Read", "ReadWrite")
    try {
        if ($offset -gt 0 -and $stream.Length -ge $offset) { [void]$stream.Seek($offset, "Begin") }
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ })
    }
    finally { $stream.Dispose() }
}

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
        $launcherJson = Join-Path $env:LOCALAPPDATA "Funke\Launcher\launcher.json"
        $entries = Get-Content -LiteralPath $launcherJson -Raw | ConvertFrom-Json
        $Project = ($entries | Sort-Object { $_.lastOpened } -Descending | Select-Object -First 1).path
    }
    if (-not $Project) { $Project = New-SandboxProject }
    $Project = (Resolve-Path -LiteralPath $Project).Path
}

$logPath = Join-Path $env:LOCALAPPDATA "Funke\Logs\$exeName.log"
$logOffset = 0
$logHead = $null
if (Test-Path -LiteralPath $logPath) {
    $logOffset = (Get-Item -LiteralPath $logPath).Length
    $logHeadLength = [int][Math]::Min(256, $logOffset)
    if ($logHeadLength -gt 0) { $logHead = Read-LogHead $logPath $logHeadLength }
}
$dumpDir = Join-Path $env:LOCALAPPDATA "Funke\CrashDumps"
$started = Get-Date

$argList = @()
if ($Project) { $argList += "--project=`"$Project`"" }
if (-not $Foreground) { $argList += "--background" }
if ($App -eq "Runtime") {
    if (-not $RealPrefs -and $PrefsProfile) { $argList += "--prefs-profile=$PrefsProfile" }
    if ($ScreenshotAt.Count -gt 0) { $argList += "--screenshot-at=$($ScreenshotAt -join ',')" }
    if ($ScreenshotAt.Count -gt 0 -or $InputScript) { $argList += "--screenshot-dir=`"$shotDir`"" }
    if ($InputScript) { $argList += "--input-script=`"$((Resolve-Path -LiteralPath $InputScript).Path)`"" }
    if ($FixedStep -gt 0) { $argList += "--fixed-step=$([string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0}', $FixedStep))" }
    if ($QuitAt) { $argList += "--quit-at=$QuitAt" }
    if ($Exec) { $argList += "--exec=`"$Exec`"" }
}
$argList += @($ExtraArgs | Where-Object { $_ })
Write-Host "[run] $exeName $($argList -join ' ')"
# Start-Process rejects an empty -ArgumentList (the Launcher runs without a project).
$startArgs = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe -Parent); PassThru = $true }
if ($argList.Count -gt 0) { $startArgs.ArgumentList = $argList }
$previousFocusView = $env:FUNKE_EDITOR_FOCUS_VIEW
$previousLauncherPage = $env:FUNKE_LAUNCHER_PAGE
$previousOpenWindow = $env:FUNKE_EDITOR_OPEN
$previousAutoplay = $env:FUNKE_EDITOR_AUTOPLAY
$previousAutostop = $env:FUNKE_EDITOR_AUTOSTOP
$previousAutoreload = $env:FUNKE_EDITOR_AUTORELOAD
$previousSelect = $env:FUNKE_EDITOR_SELECT
$previousDrawMode = $env:FUNKE_EDITOR_DRAW_MODE
$previousTheme = $env:FUNKE_EDITOR_THEME
if ($Select) { $env:FUNKE_EDITOR_SELECT = $Select }
if ($DrawMode) { $env:FUNKE_EDITOR_DRAW_MODE = $DrawMode }
if ($Theme) { $env:FUNKE_EDITOR_THEME = $Theme }
if ($Play -or $StopAfter -gt 0) { $env:FUNKE_EDITOR_AUTOPLAY = "1" }
if ($StopAfter -gt 0) { $env:FUNKE_EDITOR_AUTOSTOP = [string]$StopAfter }
if ($ReloadAfter -gt 0) { $env:FUNKE_EDITOR_AUTORELOAD = if ($RemoveModule) { "${ReloadAfter}:$RemoveModule" } else { [string]$ReloadAfter } }
if ($EditorView) { $env:FUNKE_EDITOR_FOCUS_VIEW = "editor" }
elseif ($GameView) { $env:FUNKE_EDITOR_FOCUS_VIEW = "game" }
if ($LauncherPage) { $env:FUNKE_LAUNCHER_PAGE = $LauncherPage }
if ($OpenWindow.Count -gt 0) { $env:FUNKE_EDITOR_OPEN = (@($OpenWindow | ForEach-Object { $_ -split ',' }) -join ',') }
try { $proc = Start-Process @startArgs }
finally {
    $env:FUNKE_EDITOR_FOCUS_VIEW = $previousFocusView
    $env:FUNKE_LAUNCHER_PAGE = $previousLauncherPage
    $env:FUNKE_EDITOR_OPEN = $previousOpenWindow
    $env:FUNKE_EDITOR_AUTOPLAY = $previousAutoplay
    $env:FUNKE_EDITOR_AUTOSTOP = $previousAutostop
    $env:FUNKE_EDITOR_AUTORELOAD = $previousAutoreload
    $env:FUNKE_EDITOR_SELECT = $previousSelect
    $env:FUNKE_EDITOR_DRAW_MODE = $previousDrawMode
    $env:FUNKE_EDITOR_THEME = $previousTheme
}

$shotsTaken = New-Object System.Collections.Generic.List[string]
$deadline = $started.AddSeconds($Seconds)
# Engine-driven runs end themselves; wall-clock shots only when asked for explicitly.
$wallClockShots = if ($engineDriven -and -not $PSBoundParameters.ContainsKey('Shots')) { 0 } else { $Shots }
if ($engineDriven) {
    $timeout = [Math]::Max($Seconds, 60)
    if (-not $proc.WaitForExit([int]($timeout * 1000)) -and $wallClockShots -eq 0) {
        Write-Host "[run] the game did not quit within ${timeout}s (missing quit step?)" -ForegroundColor Yellow
    }
}
for ($i = 1; $i -le $wallClockShots; $i++) {
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) { Start-Sleep -Milliseconds 200 }
    if ($proc.HasExited) { break }
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    $shot = Join-Path $outDir "$App-$i.png"
    if ($hwnd -ne [IntPtr]::Zero -and [FunkeAgent.WindowShot]::Capture($hwnd, $shot)) {
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
    $loaderFailure = Get-LoaderFailure $proc.ExitCode
    if ($loaderFailure) { $exitInfo += " before main(): $loaderFailure" }
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

# Log lines written during this run, across spdlog rotations (<exe>.log -> <exe>.1.log -> ... <exe>.3.log),
# so a log flood can't push the first real error out of the slice.
$runLog = @()
$logFiles = @($logPath) + @(1..3 | ForEach-Object { Join-Path (Split-Path $logPath) "$exeName.$_.log" })
# The file that was <exe>.log at launch is found by its first bytes; everything newer is read whole.
$startIndex = -1
if ($logHead) {
    for ($i = 0; $i -lt $logFiles.Count; $i++) {
        if ((Test-Path -LiteralPath $logFiles[$i]) -and (Read-LogHead $logFiles[$i] $logHeadLength) -eq $logHead) { $startIndex = $i; break }
    }
}
if ($startIndex -lt 0) {
    # No log at launch, or the launch-time file rotated away entirely: take every file written since then.
    $startIndex = -1
    for ($i = $logFiles.Count - 1; $i -ge 0; $i--) {
        if ((Test-Path -LiteralPath $logFiles[$i]) -and (Get-Item -LiteralPath $logFiles[$i]).LastWriteTime -ge $started) { $startIndex = $i; break }
    }
    $logOffset = 0
    if ($logHead) { Write-Host "[run] log rotated past the launch-time file; the earliest lines of this run may be missing" -ForegroundColor Yellow }
}
for ($i = $startIndex; $i -ge 0; $i--) {
    if (-not (Test-Path -LiteralPath $logFiles[$i])) { continue }
    $offset = if ($i -eq $startIndex -and $logHead) { $logOffset } else { 0 }
    $runLog += Read-LogLines $logFiles[$i] $offset
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
if ($App -eq "Runtime") {
    Get-ChildItem -LiteralPath $shotDir -Filter "*.png" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | ForEach-Object { Write-Host "[run] game screenshot: $($_.FullName)" }
}

$crashed = $dumps.Count -gt 0 -or ($proc.HasExited -and -not $KeepOpen -and $exitInfo -like "exited on its own*" -and $proc.ExitCode -ne 0)
if ($crashed) { exit 1 } else { exit 0 }
