[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath,

    [Parameter(Mandatory = $true)]
    [string]$ChecksumPath,

    [string]$SignaturePath,

    [string]$ExpectedSignerThumbprint,

    [switch]$RequireSignature
)

$ErrorActionPreference = "Stop"
$archive = (Resolve-Path -LiteralPath $ArchivePath).Path
$checksum = (Resolve-Path -LiteralPath $ChecksumPath).Path

$line = [System.IO.File]::ReadAllText($checksum).Trim()
$match = [regex]::Match($line, '^([0-9a-fA-F]{64})  ([^\\/]+)$')
if (-not $match.Success) {
    throw "Checksum file must contain one SHA-256 line using two-space filename syntax."
}
if ($match.Groups[2].Value -cne [System.IO.Path]::GetFileName($archive)) {
    throw "Checksum filename does not match the release archive."
}

$expectedHash = $match.Groups[1].Value.ToLowerInvariant()
$actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -cne $expectedHash) {
    throw "Release archive SHA-256 mismatch."
}

if (-not $SignaturePath) {
    $SignaturePath = "$checksum.p7s"
}
if (Test-Path -LiteralPath $SignaturePath -PathType Leaf) {
    Add-Type -AssemblyName System.Security.Cryptography.Pkcs
    $content = [System.Security.Cryptography.Pkcs.ContentInfo]::new(
        [System.IO.File]::ReadAllBytes($checksum)
    )
    $signedCms = [System.Security.Cryptography.Pkcs.SignedCms]::new($content, $true)
    $signedCms.Decode([System.IO.File]::ReadAllBytes($SignaturePath))
    $verifySignatureOnly = -not $RequireSignature -or [bool]$ExpectedSignerThumbprint
    $signedCms.CheckSignature($verifySignatureOnly)
    if ($signedCms.SignerInfos.Count -ne 1 -or -not $signedCms.SignerInfos[0].Certificate) {
        throw "Checksum signature must contain exactly one signing certificate."
    }
    if ($ExpectedSignerThumbprint) {
        $expectedThumbprint = $ExpectedSignerThumbprint.Replace(" ", "").ToUpperInvariant()
        $actualThumbprint = $signedCms.SignerInfos[0].Certificate.Thumbprint.Replace(" ", "").ToUpperInvariant()
        if ($actualThumbprint -cne $expectedThumbprint) {
            throw "Checksum signer thumbprint does not match the expected release signer."
        }
    }
}
elseif ($RequireSignature) {
    throw "Detached checksum signature is required but missing: $SignaturePath"
}

Write-Host "[VerifyRelease] OK: $([System.IO.Path]::GetFileName($archive)) ($actualHash)"
