[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BundleDirectory,

    [Parameter(Mandatory = $true)]
    [string]$MetadataPath,

    [switch]$RequireReleaseIdentity,

    [switch]$RequireSigned
)

$ErrorActionPreference = "Stop"
$bundle = (Resolve-Path -LiteralPath $BundleDirectory).Path
$metadataFile = (Resolve-Path -LiteralPath $MetadataPath).Path
$metadata = Get-Content -LiteralPath $metadataFile -Raw | ConvertFrom-Json

if ($metadata.schemaVersion -ne 1 -or -not $metadata.version -or -not $metadata.buildId) {
    throw "Build metadata is missing required identity fields."
}
if ($metadata.commit -notmatch '^[0-9a-f]{40,64}$') {
    throw "Build metadata does not contain a full Git commit ID."
}
if ($RequireReleaseIdentity -and ($metadata.dirty -or $metadata.buildId -match '\.(dirty|dev)(?:\.|$)' -or $metadata.identityScope -eq 'engine')) {
    throw "A tagged release cannot use dirty or development-scoped build metadata."
}

$bundledMetadataPath = Join-Path $bundle "BuildMetadata.json"
if (-not (Test-Path -LiteralPath $bundledMetadataPath -PathType Leaf)) {
    throw "The bundle does not contain BuildMetadata.json."
}
$bundledMetadata = Get-Content -LiteralPath $bundledMetadataPath -Raw | ConvertFrom-Json
foreach ($field in @("version", "buildId", "commit")) {
    if ($bundledMetadata.$field -cne $metadata.$field) {
        throw "Bundled build metadata field '$field' does not match the build output."
    }
}

. (Join-Path $PSScriptRoot "windows_pe_policy.ps1")

$nativeFiles = @(
    "Funke-Launcher.exe",
    "Funke-Editor.exe",
    "Funke-Runtime.exe",
    "funke.exe",
    "Funke-Engine.dll",
    "EngineSDK\Runtime-Development\Funke-Runtime.exe",
    "EngineSDK\Runtime-Development\Funke-Engine.dll",
    "EngineSDK\Runtime-Dist\Funke-Runtime.exe",
    "EngineSDK\Runtime-Dist\Funke-Engine.dll"
)
$sdkManifest = Get-Content -LiteralPath (Join-Path $bundle "EngineSDK\FunkeSDK.json") -Raw | ConvertFrom-Json
if ($sdkManifest.capabilities.nativeDevelopment) {
    $nativeFiles += @(
        "EngineSDK\bin\Release-windows-x86_64\Funke-Engine\Funke-Engine.dll",
        "EngineSDK\bin\Dist-windows-x86_64\Funke-Engine\Funke-Engine.dll"
    )
}

foreach ($entry in $nativeFiles) {
    $path = Join-Path $bundle $entry
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required native release binary is missing: $path"
    }
    $versionInfo = (Get-Item -LiteralPath $path).VersionInfo
    if ($versionInfo.ProductVersion -cne $metadata.version) {
        throw "ProductVersion mismatch in $path; expected $($metadata.version), found $($versionInfo.ProductVersion)."
    }

    $subsystem = if ($entry -ceq "funke.exe") { 3 } else { 2 }
    Assert-NativePePolicy -Path $path -ExecutableSubsystem $subsystem
}

$firstPartyFiles = @(Get-ChildItem -LiteralPath $bundle -Recurse -File |
    Where-Object { $_.Name -match '^(Funke-.*\.(exe|dll)|funke\.exe)$' })
foreach ($file in $firstPartyFiles) {
    $versionInfo = $file.VersionInfo
    if ($versionInfo.ProductVersion -cne $metadata.version) {
        throw "ProductVersion mismatch in $($file.FullName); expected $($metadata.version), found $($versionInfo.ProductVersion)."
    }
    if ($versionInfo.SpecialBuild -cne $metadata.buildId) {
        throw "SpecialBuild mismatch in $($file.FullName); expected $($metadata.buildId), found $($versionInfo.SpecialBuild)."
    }
}

if ($RequireSigned) {
    foreach ($file in $firstPartyFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Authenticode verification failed for $($file.FullName): $($signature.StatusMessage)"
        }
    }
}

Write-Host "[verify-windows-release] OK: $($metadata.buildId)"
