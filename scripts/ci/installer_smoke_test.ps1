[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$InstallerPath)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{36454A75-515B-49FC-82E8-2DC876A3FEA5}_is1'
if (Test-Path -LiteralPath $uninstallKey) {
    throw 'An installed Funke already exists for this user. Run this test on a separate test account.'
}
$testName = 'Funke Setup Pr{0}fung {1}' -f [char]0x00FC, [guid]::NewGuid().ToString('N')
$testRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) $testName))
$installDirectory = Join-Path $testRoot 'App'
$projectDirectory = Join-Path $testRoot 'Projects/KeepMe'
New-Item -ItemType Directory -Path $projectDirectory -Force | Out-Null
$projectFile = Join-Path $projectDirectory 'project.json'
[IO.File]::WriteAllText($projectFile, '{"name":"KeepMe"}')

function Invoke-Setup([string]$Components, [string]$LogName) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS',
        '/LANG=german', '/TASKS=', ('/DIR="' + $installDirectory + '"'),
        ('/COMPONENTS="' + $Components + '"'), ('/LOG="' + (Join-Path $testRoot $LogName) + '"'))
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Setup failed (exit $($process.ExitCode)); see $testRoot" }
}

function Assert-Installed([string]$RelativePath, [bool]$Expected) {
    $actual = Test-Path -LiteralPath (Join-Path $installDirectory $RelativePath)
    if ($actual -ne $Expected) { throw "Unexpected installed state for $RelativePath (expected $Expected)" }
}

function Invoke-InstalledCli([string[]]$Arguments, [string]$LogName) {
    $log = Join-Path $testRoot $LogName
    Push-Location $installDirectory
    try {
        & (Join-Path $installDirectory 'funke.exe') @Arguments *> $log
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $log | Select-Object -Last 15 | Write-Host
            throw "Installed CLI failed; see $log"
        }
    }
    finally { Pop-Location }
}

try {
    Invoke-Setup 'core,templates,packages\tween' 'selective-install.log'
    Assert-Installed 'Funke-Launcher.exe' $true
    Assert-Installed 'Funke-Editor.exe' $true
    Assert-Installed 'EngineSDK/templates/Funke.Empty2D/template.json' $true
    Assert-Installed 'EngineSDK/packages/Funke.Tween/funke-package.lua' $true
    Assert-Installed 'EngineSDK/packages/Funke.Networking' $false
    Assert-Installed 'EngineSDK/packages/Funke.AudioMixer' $false
    Assert-Installed 'funke.exe' $false
    Assert-Installed 'Funke-Engine.pdb' $false
    Assert-Installed 'EngineSDK/External/glad' $false
    Assert-Installed 'EngineSDK/packages/Funke.Tween/.funke-installer-package.json' $true
    $obsoleteSource = Join-Path $installDirectory 'EngineSDK/packages/Funke.Tween/Source/RemovedInNewVersion.cs'
    [IO.File]::WriteAllText($obsoleteSource, 'class ObsoleteInstallerFixture {}')
    Write-Host '[Installer smoke] Selective installation passed.'

    Invoke-Setup 'core,templates,cli,packages\tween,packages\networking' 'upgrade.log'
    Assert-Installed 'funke.exe' $true
    Assert-Installed 'EngineSDK/packages/Funke.Tween/funke-package.lua' $true
    Assert-Installed 'EngineSDK/packages/Funke.Networking/funke-package.lua' $true
    Assert-Installed 'EngineSDK/packages/Funke.AudioMixer' $false
    Assert-Installed 'EngineSDK/packages/Funke.Tween/Source/RemovedInNewVersion.cs' $false
    if ((Get-Content -LiteralPath $projectFile -Raw) -ne '{"name":"KeepMe"}') {
        throw 'Setup changed the separate project.'
    }
    Write-Host '[Installer smoke] Adding components to an existing installation passed.'

    $exportProject = Join-Path $testRoot 'Projects/InstallerExportSmoke'
    Invoke-InstalledCli @('create', 'InstallerExportSmoke', '--path', (Join-Path $testRoot 'Projects'),
        '--no-git', '--no-register') 'create-project.log'
    Invoke-InstalledCli @('package', 'add', 'Funke.Tween', '--project', $exportProject) 'enable-package.log'
    $probe = 'public static class InstallerProbe { public static Funke.Tween.Ease Value = Funke.Tween.Ease.Linear; }'
    [IO.File]::WriteAllText((Join-Path $exportProject 'Assets/Scripts/InstallerProbe.cs'), $probe)
    $packageOutput = Join-Path $exportProject '.funke/build/packages/bin/Dist-windows-x86_64/Pkg.Funke.Tween/Pkg.Funke.Tween.dll'
    if (Test-Path -LiteralPath $packageOutput) { throw 'Dist package output unexpectedly exists before first export.' }
    $gameDirectory = Join-Path $testRoot 'Exported Game'
    Invoke-InstalledCli @('build', '--project', $exportProject, '--release', '--output', $gameDirectory) 'export-game.log'
    if (-not (Test-Path -LiteralPath $packageOutput) -or
        -not (Test-Path -LiteralPath (Join-Path $gameDirectory 'InstallerExportSmoke.exe'))) {
        throw 'Export did not produce the Dist package and game artifacts.'
    }
    Write-Host '[Installer smoke] Installed package enabled, compiled for Dist and exported with the game.'
}
finally {
    $uninstaller = Join-Path $installDirectory 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
        $location = (Get-ItemProperty -LiteralPath $uninstallKey).InstallLocation.TrimEnd('\')
        $resolvedDirectory = [IO.Path]::GetFullPath($installDirectory)
        if ($location -ine $resolvedDirectory -or
            [IO.DirectoryInfo]::new($resolvedDirectory).Parent.FullName -ne $testRoot) {
            throw 'Refusing to uninstall an installation outside the isolated test directory.'
        }
        $process = Start-Process -FilePath $uninstaller -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
            ('/LOG="' + (Join-Path $testRoot 'uninstall.log') + '"')
        ) -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "Test uninstallation failed (exit $($process.ExitCode))." }
    }
}
Assert-Installed 'Funke-Launcher.exe' $false
Assert-Installed 'EngineSDK/packages/Funke.Tween/funke-package.lua' $false
if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstall registration was not removed.' }
if ((Get-Content -LiteralPath $projectFile -Raw) -ne '{"name":"KeepMe"}') {
    throw 'Uninstall did not preserve the separate project.'
}
Write-Host '[Installer smoke] Uninstallation preserved the project and removed installed files.'
Write-Host "[Installer smoke] Logs and preserved test project: $testRoot"
