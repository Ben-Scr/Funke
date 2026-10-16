<#
.SYNOPSIS
    Fast incremental build with compact output, for local dev and AI agents.

.DESCRIPTION
    Builds one or more Funke projects with the VS2022 MSBuild, prints only
    errors (and warnings in files changed in git) as `path(line): code: msg`,
    and writes the full log to bin-int/_agent/. Regenerates premake projects
    automatically when a .cpp exists on disk that a .vcxproj does not list.
    After building Funke-Engine it copies the DLL to every consumer output
    (Editor, Launcher, Runtime, tests) so nothing keeps running a stale engine.

.EXAMPLE
    scripts\dev\Build.ps1 Engine
    scripts\dev\Build.ps1 Editor -Config Debug
    scripts\dev\Build.ps1 -File Funke-Engine\src\Scene\Scene.cpp     # syntax-check one TU
    scripts\dev\Build.ps1 Engine,EngineTests,ScriptCore -Warnings all
    scripts\dev\Build.ps1 ScriptCompiler                               # editor's in-process Roslyn hot-reload compiler
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    # Engine, Editor, Runtime, Launcher, CLI, EditorRuntime, EngineTests, EditorTests, Tests, ScriptCore, ScriptCompiler, PackageTool, All
    [string[]]$Project = @(),

    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Config = "Release",

    # Compile only these translation units (no link). Project is inferred from the path.
    [string[]]$File = @(),

    [ValidateSet("changed", "all", "none")]
    [string]$Warnings = "changed",

    [switch]$Regen,
    [switch]$NoRegenCheck,
    [switch]$NoDeploy,
    [switch]$Rebuild,
    [int]$MaxErrors = 25
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$binDir = Join-Path $repoRoot "bin\$Config-windows-x86_64"
$logDir = Join-Path $repoRoot "bin-int\_agent"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$nativeProjects = [ordered]@{
    Engine        = "Funke-Engine\Funke-Engine.vcxproj"
    EditorRuntime = "Funke-EditorRuntime\Funke-EditorRuntime.vcxproj"
    Editor        = "Funke-Editor\Funke-Editor.vcxproj"
    Runtime       = "Funke-Runtime\Funke-Runtime.vcxproj"
    Launcher      = "Funke-Launcher\Funke-Launcher.vcxproj"
    CLI           = "Funke-CLI\Funke-CLI.vcxproj"
    EngineTests   = "Tests\Funke-Engine-Tests\Funke-Engine-Tests.vcxproj"
    EditorTests   = "Tests\Funke-Editor-Tests\Funke-Editor-Tests.vcxproj"
}
$managedProjects = [ordered]@{
    ScriptCore     = "Funke-ScriptCore\Funke-ScriptCore.csproj"
    ScriptCompiler = "Funke-ScriptCompiler\Funke-ScriptCompiler.csproj"
    PackageTool    = "Funke-PackageTool\Funke-PackageTool.csproj"
}
# Noise the engine always emits (DLL-interface warnings on exported STL members).
$ignoredWarnings = @("C4251", "C4275", "MSB8028", "MSB8077", "LNK4099")

function Get-MSBuild {
    $known = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path -LiteralPath $known) { return $known }
    # VS "18" headers clash with the v143 PCHs, so pin vswhere to VS2022.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found) { return $found }
    }
    throw "VS2022 MSBuild not found."
}

function Get-RelativePath([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($repoRoot.Length).TrimStart('\')
    }
    return $full
}

function Get-ChangedFiles {
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $lines = & git -C $repoRoot status --porcelain --ignore-submodules=all 2>$null
    foreach ($line in $lines) {
        if ($line.Length -gt 3) { [void]$set.Add($line.Substring(3).Trim('"').Replace('/', '\')) }
    }
    return , $set
}

# Sources on disk that no .vcxproj lists. Premake excludes some on purpose
# (removefiles), so callers compare against the set recorded after the last regen.
function Get-UnlistedSources {
    $unlisted = New-Object System.Collections.Generic.List[string]
    foreach ($proj in $nativeProjects.Values) {
        $projPath = Join-Path $repoRoot $proj
        if (-not (Test-Path -LiteralPath $projPath)) { $unlisted.Add("<missing> $proj"); continue }
        $projDir = Split-Path $projPath -Parent
        $srcDir = Join-Path $projDir "src"
        if (-not (Test-Path -LiteralPath $srcDir)) { continue }
        $content = [System.IO.File]::ReadAllText($projPath)
        $listed = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
        foreach ($m in [regex]::Matches($content, '<ClCompile Include="([^"]+)"')) {
            [void]$listed.Add([System.IO.Path]::GetFullPath((Join-Path $projDir $m.Groups[1].Value)))
        }
        foreach ($f in Get-ChildItem -LiteralPath $srcDir -Recurse -File | Where-Object { $_.Extension -eq ".cpp" -or $_.Extension -eq ".c" }) {
            if (-not $listed.Contains($f.FullName)) { $unlisted.Add((Get-RelativePath $f.FullName)) }
        }
    }
    return , $unlisted
}

function Get-StaleReason([string]$baselinePath) {
    if (-not (Test-Path -LiteralPath $baselinePath)) { return "no baseline yet" }
    $baselineTime = (Get-Item -LiteralPath $baselinePath).LastWriteTimeUtc
    $luaFiles = @(Get-ChildItem -LiteralPath $repoRoot -File -Filter *.lua) +
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "premake") -File -Filter *.lua -ErrorAction SilentlyContinue) +
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "premake\dependencies") -File -Filter *.lua -ErrorAction SilentlyContinue) +
        @($nativeProjects.Values | ForEach-Object { Get-Item -LiteralPath (Join-Path $repoRoot ((Split-Path $_ -Parent) + "\premake5.lua")) -ErrorAction SilentlyContinue })
    foreach ($lua in $luaFiles) {
        if ($lua -and $lua.LastWriteTimeUtc -gt $baselineTime) { return "$(Get-RelativePath $lua.FullName) changed" }
    }
    $known = @(Get-Content -LiteralPath $baselinePath)
    $unlisted = Get-UnlistedSources
    foreach ($file in $unlisted) {
        if ($known -notcontains $file) { return "$file not in any project" }
    }
    return $null
}

function Invoke-Premake {
    Write-Host "[build] regenerating project files (premake5 vs2022)"
    Push-Location $repoRoot
    try {
        $out = & "vendor\bin\premake5.exe" vs2022 2>&1
        if ($LASTEXITCODE -ne 0) { $out | Write-Host; throw "premake failed" }
    }
    finally { Pop-Location }
}

# Parses MSBuild/dotnet console output into deduplicated diagnostics.
function Write-Diagnostics([string[]]$lines, $changed) {
    $pattern = '^\s*(?:\d+>)?(?<loc>.+?)\s*:\s*(?<kind>fatal error|error|warning)\s+(?<code>[A-Za-z]+\d+)\s*:\s*(?<msg>.*?)(?:\s+\[[^\]]+\])?\s*$'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'
    $errors = New-Object System.Collections.Generic.List[string]
    $warns = New-Object System.Collections.Generic.List[string]
    foreach ($line in $lines) {
        $m = [regex]::Match($line, $pattern)
        if (-not $m.Success) { continue }
        $code = $m.Groups["code"].Value
        $loc = $m.Groups["loc"].Value
        $file = $loc -replace '\(\d+(,\d+)?\)$', ''
        if ($file -match '^[A-Za-z]:\\') { $relFile = Get-RelativePath $file; $loc = $loc.Replace($file, $relFile) } else { $relFile = $file }
        $text = "${loc}: $code $($m.Groups['msg'].Value)"
        if (-not $seen.Add($text)) { continue }
        if ($m.Groups["kind"].Value -like "*error") {
            $errors.Add($text)
        }
        elseif ($Warnings -ne "none" -and $ignoredWarnings -notcontains $code) {
            if ($Warnings -eq "all" -or $changed.Contains($relFile)) { $warns.Add($text) }
        }
    }
    foreach ($w in $warns) { Write-Host "warning $w" -ForegroundColor Yellow }
    $shown = 0
    foreach ($e in $errors) {
        if ($shown -ge $MaxErrors) { Write-Host "... $($errors.Count - $shown) more errors (see log)" -ForegroundColor Red; break }
        Write-Host "error $e" -ForegroundColor Red
        $shown++
    }
    return $errors.Count
}

function Invoke-Logged([string]$name, [string]$exe, [string[]]$arguments, $changed) {
    $log = Join-Path $logDir "$name.log"
    $ErrorActionPreference = "Continue"  # native stderr must not throw
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $output = & $exe @arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $sw.Stop()
    [System.IO.File]::WriteAllLines($log, [string[]]$output)
    $errorCount = Write-Diagnostics $output $changed
    $secs = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    if ($code -ne 0) {
        if ($errorCount -eq 0) {
            # Failure without a parseable diagnostic (e.g. MSB3073 copy step): show the tail.
            $output | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
        }
        Write-Host "[build] $name FAILED ($errorCount errors, ${secs}s) log: $(Get-RelativePath $log)" -ForegroundColor Red
        return $false
    }
    Write-Host "[build] $name ok (${secs}s)" -ForegroundColor Green
    return $true
}

function Get-ProjectForFile([string]$path) {
    $rel = Get-RelativePath $path
    $best = $null
    foreach ($key in $nativeProjects.Keys) {
        $dir = Split-Path $nativeProjects[$key] -Parent
        if ($rel.StartsWith("$dir\", [System.StringComparison]::OrdinalIgnoreCase)) {
            if (-not $best -or $dir.Length -gt (Split-Path $nativeProjects[$best] -Parent).Length) { $best = $key }
        }
    }
    if (-not $best) { throw "No native project owns $rel" }
    return $best
}

# Same reason as Sync-EngineDll: only a relinking Editor/Runtime copies ScriptCore, so a C#-only change
# would otherwise leave them (and every game build staged from them) on the old assembly.
function Sync-ScriptCore {
    $sourceDir = Join-Path $binDir "Funke-ScriptCore"
    $source = Join-Path $sourceDir "Funke-ScriptCore.dll"
    if (-not (Test-Path -LiteralPath $source)) { return }
    $srcTime = (Get-Item -LiteralPath $source).LastWriteTimeUtc
    foreach ($consumer in "Funke-Editor", "Funke-Launcher", "Funke-Runtime", "Funke-CLI") {
        $target = Join-Path $binDir "$consumer\Funke-ScriptCore.dll"
        if (-not (Test-Path -LiteralPath $target)) { continue }
        if ((Get-Item -LiteralPath $target).LastWriteTimeUtc -eq $srcTime) { continue }
        try {
            foreach ($ext in ".dll", ".pdb", ".xml") {
                $file = Join-Path $sourceDir "Funke-ScriptCore$ext"
                if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $binDir $consumer) -Force }
            }
            Write-Host "[build] deployed Funke-ScriptCore.dll -> $consumer"
            $script:DeployFailures.Remove("$consumer\Funke-ScriptCore.dll")
        }
        catch {
            Write-Host "[build] could not update $consumer\Funke-ScriptCore.dll (in use? close the app): $($_.Exception.Message)" -ForegroundColor Yellow
            $script:DeployFailures["$consumer\Funke-ScriptCore.dll"] = $_.Exception.Message
        }
    }
}

# The editor loads a private copy from <exe>\ScriptCompiler\ so this build output is never locked by it.
function Sync-ScriptCompiler {
    $sourceDir = Join-Path $binDir "Funke-ScriptCompiler"
    $source = Join-Path $sourceDir "Funke-ScriptCompiler.dll"
    if (-not (Test-Path -LiteralPath $source)) { return }
    $srcTime = (Get-Item -LiteralPath $source).LastWriteTimeUtc
    foreach ($consumer in "Funke-Editor", "Funke-Launcher") {
        if (-not (Test-Path -LiteralPath (Join-Path $binDir $consumer))) { continue }
        $targetDir = Join-Path $binDir "$consumer\ScriptCompiler"
        $target = Join-Path $targetDir "Funke-ScriptCompiler.dll"
        if ((Test-Path -LiteralPath $target) -and (Get-Item -LiteralPath $target).LastWriteTimeUtc -eq $srcTime) { continue }
        try {
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
            foreach ($file in "Funke-ScriptCompiler.dll", "Funke-ScriptCompiler.pdb", "Funke-ScriptCompiler.deps.json") {
                $path = Join-Path $sourceDir $file
                if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $targetDir -Force }
            }
            Write-Host "[build] deployed Funke-ScriptCompiler.dll -> $consumer\ScriptCompiler"
            $script:DeployFailures.Remove("$consumer\ScriptCompiler")
        }
        catch {
            Write-Host "[build] could not update $consumer\ScriptCompiler (in use? close the app): $($_.Exception.Message)" -ForegroundColor Yellow
            $script:DeployFailures["$consumer\ScriptCompiler"] = $_.Exception.Message
        }
    }
}

function Sync-EngineDll {
    $source = Join-Path $binDir "Funke-Engine\Funke-Engine.dll"
    if (-not (Test-Path -LiteralPath $source)) { return }
    $srcTime = (Get-Item -LiteralPath $source).LastWriteTimeUtc
    foreach ($consumer in "Funke-Editor", "Funke-Launcher", "Funke-Runtime", "Funke-CLI", "Funke-Engine-Tests", "Funke-Editor-Tests") {
        $target = Join-Path $binDir "$consumer\Funke-Engine.dll"
        if (-not (Test-Path -LiteralPath $target)) { continue }
        if ((Get-Item -LiteralPath $target).LastWriteTimeUtc -eq $srcTime) { continue }
        try {
            Copy-Item -LiteralPath $source -Destination $target -Force
            $pdb = [System.IO.Path]::ChangeExtension($source, ".pdb")
            if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $binDir $consumer) -Force -ErrorAction SilentlyContinue }
            Write-Host "[build] deployed Funke-Engine.dll -> $consumer"
            $script:DeployFailures.Remove("$consumer\Funke-Engine.dll")
        }
        catch {
            Write-Host "[build] could not update $consumer\Funke-Engine.dll (in use? close the app): $($_.Exception.Message)" -ForegroundColor Yellow
            $script:DeployFailures["$consumer\Funke-Engine.dll"] = $_.Exception.Message
        }
    }
}

# --- main -------------------------------------------------------------------

# Consumer copies that stayed stale. A stale Funke-Engine.dll next to a fresh exe dies before main
# (STATUS_ENTRYPOINT_NOT_FOUND) without a word, so this fails the build instead of only warning.
$script:DeployFailures = @{}

# `powershell -File` passes "a,b" as one string; split it here.
$Project = @($Project | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$File = @($File | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
# `powershell -File` passes "a,b" as one string; split it here.
$Project = @($Project | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$File = @($File | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($Project.Count -eq 0 -and $File.Count -eq 0) { $Project = @("Engine") }
$validProjects = @($nativeProjects.Keys) + @($managedProjects.Keys) + @("Tests", "All")
foreach ($p in $Project) { if ($validProjects -notcontains $p) { throw "Unknown project '$p'. Valid: $($validProjects -join ', ')" } }
$expanded = New-Object System.Collections.Generic.List[string]
foreach ($p in $Project) {
    switch ($p) {
        "Tests" { $expanded.Add("EngineTests"); $expanded.Add("EditorTests") }
        "All" { foreach ($k in "Engine", "ScriptCore", "ScriptCompiler", "Editor", "Runtime", "PackageTool", "Launcher", "CLI", "EngineTests", "EditorTests") { $expanded.Add($k) } }
        # A .vcxproj built directly ignores the solution's dependson; without the PackageTool output the
        # Launcher postbuild (CopyEngineSdk) stops before staging EngineSDK\Licenses, which game builds need.
        "Launcher" { $expanded.Add("PackageTool"); $expanded.Add("Launcher") }
        default { $expanded.Add($p) }
    }
}
$targets = $expanded | Select-Object -Unique

$running = Get-Process -Name "Funke-Editor", "Funke-Launcher", "Funke-Runtime" -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($binDir, [System.StringComparison]::OrdinalIgnoreCase) }
foreach ($proc in $running) {
    Write-Host "[build] note: $($proc.Name) ($Config) is running - its DLLs are locked; copy/postbuild steps may fail." -ForegroundColor Yellow
}

$baselinePath = Join-Path $logDir "premake-unlisted.txt"
$doRegen = [bool]$Regen
if (-not $doRegen -and -not $NoRegenCheck) {
    $reason = Get-StaleReason $baselinePath
    if ($reason) { Write-Host "[build] project files stale: $reason"; $doRegen = $true }
}
if ($doRegen) {
    Invoke-Premake
    $unlisted = Get-UnlistedSources
    [System.IO.File]::WriteAllLines($baselinePath, $unlisted.ToArray())
}

$msbuild = Get-MSBuild
$changed = Get-ChangedFiles
$common = @("-nologo", "-v:minimal", "-clp:NoSummary", "-m", "-p:Configuration=$Config", "-p:Platform=x64", "-p:PreferredToolArchitecture=x64")
$ok = $true

if ($File.Count -gt 0) {
    $groups = @{}
    foreach ($f in $File) {
        $key = Get-ProjectForFile $f
        $projDir = Split-Path (Join-Path $repoRoot $nativeProjects[$key]) -Parent
        $relToProj = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $f))
        if ($relToProj.StartsWith($projDir, [System.StringComparison]::OrdinalIgnoreCase)) { $relToProj = $relToProj.Substring($projDir.Length).TrimStart('\') }
        if (-not $groups.ContainsKey($key)) { $groups[$key] = New-Object System.Collections.Generic.List[string] }
        $groups[$key].Add($relToProj)
    }
    foreach ($key in $groups.Keys) {
        $msArgs = $common + @((Join-Path $repoRoot $nativeProjects[$key]), "-t:ClCompile", "-p:BuildProjectReferences=false", "-p:SelectedFiles=$($groups[$key] -join '%3B')")
        Write-Verbose ($msArgs -join " ")
        if (-not (Invoke-Logged "compile-$key" $msbuild $msArgs $changed)) { $ok = $false }
    }
}

foreach ($t in $targets) {
    if ($managedProjects.Contains($t)) {
        $msArgs = @("build", (Join-Path $repoRoot $managedProjects[$t]), "-c", $Config, "-p:Platform=x64", "-nologo", "-v:q", "-clp:NoSummary")
        if ($Rebuild) { $msArgs += "--no-incremental" }
        if (Invoke-Logged $t "dotnet" $msArgs $changed) {
            if (-not $NoDeploy) {
                if ($t -eq "ScriptCore") { Sync-ScriptCore }
                elseif ($t -eq "ScriptCompiler") { Sync-ScriptCompiler }
            }
        }
        else {
            $ok = $false
        }
        continue
    }
    $msArgs = $common + @((Join-Path $repoRoot $nativeProjects[$t]))
    if ($Rebuild) { $msArgs += "-t:Rebuild" }
    if (Invoke-Logged $t $msbuild $msArgs $changed) {
        if (-not $NoDeploy) { Sync-EngineDll; Sync-ScriptCompiler }
    }
    else {
        $ok = $false
    }
}

if ($script:DeployFailures.Count -gt 0) {
    Write-Host "[build] DEPLOY FAILED - these copies are stale, close the app using them and rerun:" -ForegroundColor Red
    foreach ($key in ($script:DeployFailures.Keys | Sort-Object)) { Write-Host "  bin\$Config-windows-x86_64\$key" -ForegroundColor Red }
    $ok = $false
}
if ($ok) { exit 0 } else { exit 1 }
