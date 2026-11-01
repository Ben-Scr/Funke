$ErrorActionPreference = 'Stop'
$bundleRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bundleRunScript = Get-Content (Join-Path $bundleRepo 'scripts/dev/Run.ps1')
$bundleCaptureStart = [Array]::IndexOf($bundleRunScript, 'Add-Type -AssemblyName System.Drawing')
$bundleCaptureEnd = [Array]::IndexOf($bundleRunScript, '[void][FunkeAgent.WindowShot]::SetProcessDPIAware()')
Invoke-Expression ($bundleRunScript[$bundleCaptureStart..$bundleCaptureEnd] -join "`n")
Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class BundleWindows {
    public delegate bool Visit(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(Visit visit, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    public static IntPtr[] ForProcess(uint process) {
        var windows = new List<IntPtr>();
        EnumWindows((window, data) => { uint owner; GetWindowThreadProcessId(window, out owner); if (owner == process) windows.Add(window); return true; }, IntPtr.Zero);
        return windows.ToArray();
    }
}
'@
$bundleOldOpen = $env:FUNKE_EDITOR_OPEN
$bundleOldTheme = $env:FUNKE_EDITOR_THEME
try {
    foreach ($bundleTheme in @('dark','light')) {
        $env:FUNKE_EDITOR_OPEN = 'buildprofiles'
        $env:FUNKE_EDITOR_THEME = $bundleTheme
        $bundleApp = Start-Process -FilePath (Join-Path $PSScriptRoot 'Editor/Funke-Editor.exe') -ArgumentList @('--background', ('--project="' + (Join-Path $PSScriptRoot 'Source') + '"')) -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 4
        foreach ($bundleWindow in [BundleWindows]::ForProcess($bundleApp.Id)) {
            if (-not [FunkeAgent.WindowShot]::IsWindowVisible($bundleWindow)) { continue }
            $bundleTitle = New-Object System.Text.StringBuilder 512
            [void][BundleWindows]::GetWindowText($bundleWindow, $bundleTitle, $bundleTitle.Capacity)
            Write-Output "$bundleTheme window: $bundleTitle"
            if ($bundleTitle.ToString() -like '*Build Profiles*') {
                [void][FunkeAgent.WindowShot]::Capture($bundleWindow, (Join-Path $PSScriptRoot "profiles-$bundleTheme.png"))
            }
        }
        [void]$bundleApp.CloseMainWindow()
        if (-not $bundleApp.WaitForExit(15000)) { $bundleApp.Kill() }
    }
} finally {
    $env:FUNKE_EDITOR_OPEN = $bundleOldOpen
    $env:FUNKE_EDITOR_THEME = $bundleOldTheme
}
