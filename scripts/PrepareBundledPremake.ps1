[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PremakePath,
    [string]$ManifestToolPath
)

$ErrorActionPreference = 'Stop'
function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

$premake = (Resolve-Path -LiteralPath $PremakePath).Path
$vendorPremake = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../vendor/bin/premake5.exe'))
if ($premake -ieq $vendorPremake) { throw 'Prepare a staged Premake copy; the vendor executable must remain unchanged.' }

$manifestTool = $ManifestToolPath
if (-not $manifestTool) {
    $kitsRoot = Get-ItemPropertyValue 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' `
        -Name KitsRoot10 -ErrorAction SilentlyContinue
    if (-not $kitsRoot) { $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10' }
    $versions = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($version in $versions) {
        $candidate = Join-Path $version.FullName 'x64/mt.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $manifestTool = $candidate; break }
    }
}
if (-not $manifestTool -or -not (Test-Path -LiteralPath $manifestTool -PathType Leaf)) {
    throw 'Windows SDK mt.exe is required to prepare Premake for Unicode paths. Pass -ManifestToolPath if necessary.'
}

$originalHash = Get-Sha256 $premake
$manifestPath = [IO.Path]::GetTempFileName()
$mergedPath = [IO.Path]::GetTempFileName()
try {
    & $manifestTool -nologo "-inputresource:$premake;#1" "-out:$manifestPath" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the bundled Premake manifest.' }
    [xml]$original = Get-Content -LiteralPath $manifestPath -Raw
    $codePage = $original.SelectSingleNode("//*[local-name()='activeCodePage']")
    if (-not $codePage -or $codePage.InnerText -ne 'UTF-8') {
        $utf8Manifest = Join-Path $PSScriptRoot '../premake/Utf8CodePage.manifest'
        & $manifestTool -nologo -manifest $manifestPath $utf8Manifest "-outputresource:$premake;#1" | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'Could not enable UTF-8 paths for bundled Premake.' }
    }
    & $manifestTool -nologo "-inputresource:$premake;#1" "-out:$mergedPath" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Could not verify the updated Premake manifest.' }
    [xml]$merged = Get-Content -LiteralPath $mergedPath -Raw
    $codePage = $merged.SelectSingleNode("//*[local-name()='activeCodePage']")
    if (-not $codePage -or $codePage.InnerText -ne 'UTF-8') { throw 'Bundled Premake has no UTF-8 code page.' }
    [pscustomobject]@{
        name = 'premake5.exe'
        sourceSha256 = $originalHash
        sha256 = Get-Sha256 $premake
        utf8Paths = $true
    }
}
finally {
    Remove-Item -LiteralPath $manifestPath, $mergedPath -Force -ErrorAction SilentlyContinue
}
