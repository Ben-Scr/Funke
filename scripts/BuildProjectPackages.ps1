[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$EngineRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ProjectRoot,

    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration,

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$MSBuildPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found at '$Path'."
    }
}

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$generator = Join-Path $engineRootPath "premake\project-packages.lua"
$projectFile = Join-Path $projectRootPath "project.json"
$premakeCandidates = @(
    (Join-Path $engineRootPath "vendor\bin\premake5.exe"),
    (Join-Path $engineRootPath "premake5.exe")
)
$premake = $premakeCandidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1

Require-File -Path $generator -Description "Project package generator"
Require-File -Path $projectFile -Description "Index project manifest"
Require-File -Path $MSBuildPath -Description "MSBuild"
if (-not $premake) {
    throw "premake5.exe was not found below '$engineRootPath'."
}

Write-Host "Generating package workspace for '$projectRootPath'..."
& $premake "--file=$generator" "vs2022" "--index-project=$projectRootPath"
if ($LASTEXITCODE -ne 0) {
    throw "Project package generation failed with exit code $LASTEXITCODE."
}

$workspaceRoot = Join-Path $projectRootPath ".index\build\packages"
$generatedRoot = Join-Path $workspaceRoot "generated"
if (-not (Test-Path -LiteralPath $generatedRoot -PathType Container)) {
    throw "Generated project package directory was not found at '$generatedRoot'."
}

$nativeProjects = @(
    Get-ChildItem -LiteralPath $generatedRoot -Recurse -File -Filter "*.vcxproj" |
        Sort-Object -Property FullName
)
$managedProjects = @(
    Get-ChildItem -LiteralPath $generatedRoot -Recurse -File -Filter "*.csproj" |
        Sort-Object -Property FullName
)

foreach ($nativeProject in $nativeProjects) {
    Write-Host "Building $($nativeProject.Name) ($Configuration|$Platform)..."
    & $MSBuildPath $nativeProject.FullName `
        "-p:Configuration=$Configuration" `
        "-p:Platform=$Platform" `
        "-p:PreferredToolArchitecture=x64" `
        "-m:1" `
        "-nr:false" `
        "-p:CL_MPCount=1" `
        "-p:UseMultiToolTask=false" `
        "-verbosity:minimal" `
        "-nologo"
    if ($LASTEXITCODE -ne 0) {
        throw "Native project package build failed for '$($nativeProject.Name)' with exit code $LASTEXITCODE."
    }
}

foreach ($managedProject in $managedProjects) {
    Write-Host "Building $($managedProject.Name) ($Configuration|$Platform)..."
    & dotnet build $managedProject.FullName `
        "-c" $Configuration `
        "-p:Platform=$Platform" `
        "--nologo" `
        "-v" "minimal" `
        "-nodeReuse:false" `
        "-p:UseSharedCompilation=false"
    if ($LASTEXITCODE -ne 0) {
        throw "Managed project package build failed for '$($managedProject.Name)' with exit code $LASTEXITCODE."
    }
}

Write-Host "Enabled project packages are up to date."
