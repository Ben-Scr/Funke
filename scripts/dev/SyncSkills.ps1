<#
.SYNOPSIS
    Keep .claude/skills and .agents/skills identical: for every file the newer
    copy wins. -Check only reports differences (exit 1 if any).
#>
[CmdletBinding()]
param([switch]$Check)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$roots = @((Join-Path $repoRoot ".claude\skills"), (Join-Path $repoRoot ".agents\skills"))
$relative = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($root in $roots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.FullName -notmatch '\\__pycache__\\' } |
        ForEach-Object { [void]$relative.Add($_.FullName.Substring($root.Length).TrimStart('\')) }
}

$diffs = 0
foreach ($rel in $relative) {
    $a = Join-Path $roots[0] $rel
    $b = Join-Path $roots[1] $rel
    $aExists = Test-Path -LiteralPath $a
    $bExists = Test-Path -LiteralPath $b
    if ($aExists -and $bExists -and (Get-FileHash -LiteralPath $a).Hash -eq (Get-FileHash -LiteralPath $b).Hash) { continue }
    $diffs++
    if (-not $aExists) { $src = $b; $dst = $a }
    elseif (-not $bExists) { $src = $a; $dst = $b }
    elseif ((Get-Item -LiteralPath $a).LastWriteTimeUtc -ge (Get-Item -LiteralPath $b).LastWriteTimeUtc) { $src = $a; $dst = $b }
    else { $src = $b; $dst = $a }
    $from = if ($src -eq $a) { ".claude" } else { ".agents" }
    if ($Check) { Write-Host "differs: $rel (newer in $from)"; continue }
    New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst -Force
    Write-Host "synced: $rel ($from -> other)"
}
if ($diffs -eq 0) { Write-Host "skills in sync" }
if ($Check -and $diffs -gt 0) { exit 1 }
exit 0
