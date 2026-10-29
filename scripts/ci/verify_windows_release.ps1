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

function Read-PePolicy {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE image: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 96 -gt $bytes.Length) {
        throw "Invalid PE header offset: $Path"
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw "Invalid PE signature: $Path"
    }
    $optionalOffset = $peOffset + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
    if ($magic -notin @(0x10B, 0x20B)) {
        throw "Unsupported PE optional header: $Path"
    }
    return [pscustomobject]@{
        Machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
        Is64Bit = $magic -eq 0x20B
        Subsystem = [BitConverter]::ToUInt16($bytes, $optionalOffset + 68)
        DllCharacteristics = [BitConverter]::ToUInt16($bytes, $optionalOffset + 70)
    }
}

$nativeFiles = @(
    @{ Path = "Funke-Launcher.exe"; Subsystem = 2 },
    @{ Path = "Funke-Editor.exe"; Subsystem = 2 },
    @{ Path = "Funke-Runtime.exe"; Subsystem = 2 },
    @{ Path = "Funke-Engine.dll"; Subsystem = 3 },
    @{ Path = "EngineSDK\Runtime-Development\Funke-Runtime.exe"; Subsystem = 2 },
    @{ Path = "EngineSDK\Runtime-Development\Funke-Engine.dll"; Subsystem = 3 },
    @{ Path = "EngineSDK\Runtime-Dist\Funke-Runtime.exe"; Subsystem = 2 },
    @{ Path = "EngineSDK\Runtime-Dist\Funke-Engine.dll"; Subsystem = 3 }
)
$sdkManifest = Get-Content -LiteralPath (Join-Path $bundle "EngineSDK\FunkeSDK.json") -Raw | ConvertFrom-Json
if ($sdkManifest.capabilities.nativeDevelopment) {
    $nativeFiles += @(
        @{ Path = "EngineSDK\bin\Release-windows-x86_64\Funke-Engine\Funke-Engine.dll"; Subsystem = 3 },
        @{ Path = "EngineSDK\bin\Dist-windows-x86_64\Funke-Engine\Funke-Engine.dll"; Subsystem = 3 }
    )
}
$requiredCharacteristics = [ordered]@{
    HighEntropyVA = 0x0020
    DynamicBase = 0x0040
    NxCompat = 0x0100
    GuardCF = 0x4000
}

foreach ($entry in $nativeFiles) {
    $path = Join-Path $bundle $entry.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required native release binary is missing: $path"
    }
    $versionInfo = (Get-Item -LiteralPath $path).VersionInfo
    if ($versionInfo.ProductVersion -cne $metadata.version) {
        throw "ProductVersion mismatch in $path; expected $($metadata.version), found $($versionInfo.ProductVersion)."
    }

    $policy = Read-PePolicy $path
    if ($policy.Machine -ne 0x8664 -or -not $policy.Is64Bit -or $policy.Subsystem -ne $entry.Subsystem) {
        throw "PE platform/subsystem policy failed: $path"
    }
    foreach ($characteristic in $requiredCharacteristics.GetEnumerator()) {
        if (($policy.DllCharacteristics -band $characteristic.Value) -eq 0) {
            throw "PE hardening flag $($characteristic.Key) is missing: $path"
        }
    }
}

$firstPartyFiles = @(Get-ChildItem -LiteralPath $bundle -Recurse -File |
    Where-Object { $_.Name -match '^Funke-.*\.(exe|dll)$' })
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
