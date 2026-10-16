# Dot-sourced by Run.ps1 / Test.ps1: Windows loader failures end a process before main() without any output.
function Get-LoaderFailure([long]$ExitCode) {
    # Hex literals like 0xC0000135 are negative Int32 in PowerShell; compare in that bit pattern.
    $code = [BitConverter]::ToInt32([BitConverter]::GetBytes($ExitCode), 0)
    switch ($code) {
        0xC0000135 { return "a DLL it needs was not found (0xC0000135)" }
        0xC0000138 { return "a DLL next to it lacks an ordinal it imports (0xC0000138) - stale Funke-Engine.dll? rerun Build.ps1 for this app" }
        0xC0000139 { return "a DLL next to it lacks a function it imports (0xC0000139) - stale Funke-Engine.dll? rerun Build.ps1 for this app" }
        0xC000007B { return "a DLL next to it is for another architecture or corrupt (0xC000007B)" }
        0xC0000142 { return "a DLL failed to initialize (0xC0000142)" }
    }
    return ""
}
