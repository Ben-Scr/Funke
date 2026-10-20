<#
.SYNOPSIS
    `funke editor ...` from this repo's build: drive an editor that is already open.

.DESCRIPTION
    Thin wrapper around bin\<Config>-windows-x86_64\Funke-CLI\funke.exe so agents use the CLI that matches the
    editor they just built and launched (scripts\dev\Run.ps1 -KeepOpen). All arguments go to `funke editor`.
    See Github-Docs/Documentation/Editor/Editor-Bridge.md.

.EXAMPLE
    scripts\dev\Editor.ps1 status -Project bin-int\_agent\SandboxProject
    scripts\dev\Editor.ps1 play --size 1080x2400
    scripts\dev\Editor.ps1 screenshot bin-int\_agent\run\shot.png
    scripts\dev\Editor.ps1 tap 0.5 0.4
    scripts\dev\Editor.ps1 logs --level warning
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Config = "Release",
    # Becomes --project; the editor with this project open is used.
    [string]$Project = "",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cli = Join-Path $repoRoot "bin\$Config-windows-x86_64\Funke-CLI\funke.exe"
if (-not (Test-Path -LiteralPath $cli)) { throw "Not built: $cli (run scripts\dev\Build.ps1 CLI -Config $Config)" }

$forward = @("editor") + @($Arguments | Where-Object { $_ -ne $null })
if ($Project) { $forward += @("--project", (Resolve-Path -LiteralPath $Project).Path) }
& $cli @forward
exit $LASTEXITCODE
