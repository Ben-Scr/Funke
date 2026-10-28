[CmdletBinding()]
param(
    [string]$BundleDirectory,
    [string]$OutputDirectory,
    [string]$CompilerPath,
    [string]$ManifestToolPath,
    [switch]$DownloadCompiler,
    [string]$PrerequisiteDirectory,
    [string]$CatalogPath,
    [string]$SigningCertificatePath,
    [string]$SigningCertificateThumbprint,
    [string]$SigningPasswordEnvironmentVariable = 'FUNKE_SIGNING_CERTIFICATE_PASSWORD',
    [string]$SigningTimestampUrl = 'http://timestamp.digicert.com',
    [switch]$RequireSigning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BundleDirectory) { $BundleDirectory = Join-Path $repoRoot 'bin/Release-windows-x86_64/Funke-Launcher' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'bin/installer' }
if (-not $CatalogPath) { $CatalogPath = Join-Path $repoRoot 'installer/windows/packages.lock.json' }
$BundleDirectory = (Resolve-Path -LiteralPath $BundleDirectory).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$cacheDirectory = Join-Path $repoRoot 'bin-int/installer-cache'
$workDirectory = Join-Path $repoRoot ('bin-int/installer/' + [guid]::NewGuid().ToString('N'))
$payloadDirectory = Join-Path $workDirectory 'payload'
$lockPath = Join-Path $repoRoot 'installer/windows/dependencies.lock.json'
$dependencies = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json

function Assert-DownloadedFile {
    param([string]$Path, $Dependency)
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm $Dependency.algorithm).Hash
    if ($hash -ine $Dependency.hash) {
        throw "Checksum mismatch for $($Dependency.fileName). Remove the cached file and retry: $Path"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notmatch ('(^|,\s*)O="?' + [regex]::Escape($Dependency.publisher) + '"?(,|$)')) {
        throw "Expected a valid $($Dependency.publisher) signature on $Path; got $($signature.Status)."
    }
}

function Get-VerifiedDependency {
    param($Dependency, [string]$ExistingDirectory)
    if ($ExistingDirectory) {
        $path = Join-Path $ExistingDirectory $Dependency.fileName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing offline prerequisite: $path" }
    }
    else {
        $path = Join-Path $cacheDirectory $Dependency.fileName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Write-Host "[Installer] Downloading $($Dependency.fileName) $($Dependency.version)..."
            $temporaryPath = "$path.$([guid]::NewGuid().ToString('N')).download"
            try {
                Invoke-WebRequest -Uri $Dependency.url -OutFile $temporaryPath
                Assert-DownloadedFile $temporaryPath $Dependency
                Move-Item -LiteralPath $temporaryPath -Destination $path
            }
            finally {
                if (Test-Path -LiteralPath $temporaryPath) { Remove-Item -LiteralPath $temporaryPath -Force }
            }
        }
    }
    Assert-DownloadedFile $path $Dependency
    return $path
}

function Find-InstallerCompiler {
    if ($CompilerPath) { return (Resolve-Path -LiteralPath $CompilerPath).Path }
    $localCompiler = Join-Path $repoRoot 'bin-int/installer-tools/InnoSetup/ISCC.exe'
    $candidates = @($localCompiler)
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, $env:LOCALAPPDATA)) {
        if ($base) {
            $candidates += Join-Path $base 'Inno Setup 6/ISCC.exe'
            $candidates += Join-Path $base 'Programs/Inno Setup 6/ISCC.exe'
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    if (-not $DownloadCompiler) {
        throw 'Inno Setup 6.3+ is required. Install it, pass -CompilerPath, or use -DownloadCompiler for the pinned compiler.'
    }
    $setup = Get-VerifiedDependency $dependencies.compiler
    $compilerDirectory = Split-Path -Parent $localCompiler
    $process = Start-Process -FilePath $setup -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS',
        '/SP-', '/TASKS=', ('/DIR="' + $compilerDirectory + '"')
    ) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $localCompiler)) {
        throw "Inno Setup compiler installation failed (exit $($process.ExitCode))."
    }
    return $localCompiler
}

function Enable-PremakeUnicodePaths {
    $manifestTool = $ManifestToolPath
    if (-not $manifestTool) {
        $kitsRoot = Get-ItemPropertyValue 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' `
            -Name KitsRoot10 -ErrorAction SilentlyContinue
        if (-not $kitsRoot) { $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10' }
        $versions = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName 'x64/mt.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $manifestTool = $candidate; break }
        }
    }
    if (-not $manifestTool -or -not (Test-Path -LiteralPath $manifestTool -PathType Leaf)) {
        throw 'Windows SDK mt.exe is required to prepare Premake for Unicode install paths. Pass -ManifestToolPath if necessary.'
    }
    $premake = Join-Path $payloadDirectory 'Core/EngineSDK/premake5.exe'
    $originalHash = (Get-FileHash -LiteralPath $premake -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestPath = Join-Path $workDirectory 'premake-original.manifest'
    & $manifestTool -nologo "-inputresource:$premake;#1" "-out:$manifestPath"
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the bundled Premake manifest.' }
    $utf8Manifest = Join-Path $repoRoot 'premake/Utf8CodePage.manifest'
    & $manifestTool -nologo -manifest $manifestPath $utf8Manifest "-outputresource:$premake;#1"
    if ($LASTEXITCODE -ne 0) { throw 'Could not enable UTF-8 paths for bundled Premake.' }
    $mergedPath = Join-Path $workDirectory 'premake-merged.manifest'
    & $manifestTool -nologo "-inputresource:$premake;#1" "-out:$mergedPath"
    if ($LASTEXITCODE -ne 0) { throw 'Could not verify the updated Premake manifest.' }
    [xml]$merged = Get-Content -LiteralPath $mergedPath -Raw
    $codePage = $merged.SelectSingleNode("//*[local-name()='activeCodePage']")
    if (-not $codePage -or $codePage.InnerText -ne 'UTF-8') { throw 'Bundled Premake has no UTF-8 code page.' }
    $inventoryPath = Join-Path $payloadDirectory 'payload-manifest.json'
    $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    $inventory | Add-Member -NotePropertyName runtimeTools -NotePropertyValue @(
        [pscustomobject]@{
            name = 'premake5.exe'
            sourceSha256 = $originalHash
            sha256 = (Get-FileHash -LiteralPath $premake -Algorithm SHA256).Hash.ToLowerInvariant()
            utf8Paths = $true
        }
    ) -Force
    [IO.File]::WriteAllText($inventoryPath, ($inventory | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
}

if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw 'Python 3.10+ is required to prepare the installer.' }
if ($SigningCertificatePath -and $SigningCertificateThumbprint) { throw 'Specify a signing certificate path OR thumbprint.' }
if ($SigningPasswordEnvironmentVariable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw 'Invalid signing password environment variable name.' }
if ($env:GITHUB_REF_TYPE -eq 'tag') { $RequireSigning = $true }
$signingEnabled = [bool]($SigningCertificatePath -or $SigningCertificateThumbprint)
if ($RequireSigning -and -not $signingEnabled) { throw 'A signing identity is required for a release installer.' }
if ($SigningCertificatePath) { $SigningCertificatePath = (Resolve-Path -LiteralPath $SigningCertificatePath).Path }

$metadata = Get-Content -LiteralPath (Join-Path $BundleDirectory 'BuildMetadata.json') -Raw | ConvertFrom-Json
if ($metadata.version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { throw 'Invalid version in bundle BuildMetadata.json.' }
$appVersion = $metadata.version
$fileVersion = ($appVersion -split '-')[0] + '.0'
if ($RequireSigning -and $metadata.dirty) { throw 'A signed release installer must use a clean release bundle.' }

New-Item -ItemType Directory -Force -Path $cacheDirectory, $workDirectory, $OutputDirectory | Out-Null
$compiler = Find-InstallerCompiler
Write-Host "[Installer] Preparing existing bundle: $BundleDirectory"

if ($RequireSigning) {
    & (Join-Path $PSScriptRoot 'ci/verify_windows_release.ps1') -BundleDirectory $BundleDirectory `
        -MetadataPath (Join-Path $BundleDirectory 'BuildMetadata.json') -RequireReleaseIdentity -RequireSigned
    if ($LASTEXITCODE -ne 0) { throw 'Signed release bundle verification failed.' }
}

& python (Join-Path $repoRoot 'installer/windows/prepare_payload.py') `
    --bundle $BundleDirectory --repo $repoRoot --output $payloadDirectory --catalog $CatalogPath --cache $cacheDirectory
if ($LASTEXITCODE -ne 0) { throw 'Installer payload preparation failed.' }
Enable-PremakeUnicodePaths

$verificationDirectory = Join-Path $workDirectory 'verification'
New-Item -ItemType Directory -Path $verificationDirectory | Out-Null
foreach ($component in @('Core', 'CLI', 'Templates')) {
    Get-ChildItem -LiteralPath (Join-Path $payloadDirectory $component) -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $verificationDirectory -Recurse -Force
    }
}
& (Join-Path $PSScriptRoot 'verify-standalone-zip.bat') Release $verificationDirectory
if ($LASTEXITCODE -ne 0) { throw 'Prepared bundle verification failed. Run scripts/BuildWindows.ps1 -PublishBundle to update missing artifacts.' }

$prerequisites = Join-Path $payloadDirectory 'Prerequisites'
New-Item -ItemType Directory -Path $prerequisites -Force | Out-Null
foreach ($dependency in @($dependencies.dotnet, $dependencies.vcredist)) {
    $download = Get-VerifiedDependency $dependency $PrerequisiteDirectory
    Copy-Item -LiteralPath $download -Destination (Join-Path $prerequisites $dependency.fileName)
}

$signingEnvironment = @{
    FUNKE_INSTALLER_SIGN_CERT_PATH = $SigningCertificatePath
    FUNKE_INSTALLER_SIGN_THUMBPRINT = $SigningCertificateThumbprint
    FUNKE_INSTALLER_SIGN_PASSWORD_ENV = $SigningPasswordEnvironmentVariable
    FUNKE_INSTALLER_SIGN_TIMESTAMP = $SigningTimestampUrl
}
$previousEnvironment = @{}
try {
    $arguments = @(
        '/Qp', "/DPayloadDir=$payloadDirectory", "/DOutputDir=$OutputDirectory", "/DRepoRoot=$repoRoot",
        "/DAppVersion=$appVersion", "/DAppFileVersion=$fileVersion"
    )
    if ($signingEnabled) {
        foreach ($key in $signingEnvironment.Keys) {
            $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
            [Environment]::SetEnvironmentVariable($key, $signingEnvironment[$key], 'Process')
        }
        $powershell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
        $signer = Join-Path $PSScriptRoot 'SignInstaller.ps1'
        $signCommand = '$q' + $powershell + '$q -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $q' + $signer + '$q -Path $f'
        $arguments += @('/DSignToolName=funke', "/Sfunke=$signCommand")
    }
    $arguments += Join-Path $repoRoot 'installer/windows/Funke.iss'
    Write-Host '[Installer] Compiling setup...'
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
}
finally {
    foreach ($key in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previousEnvironment[$key], 'Process')
    }
}

$installer = Join-Path $OutputDirectory "FunkeSetup-$appVersion-Windows-x64.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Compiler produced no installer at $installer" }
if ($signingEnabled -and (Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'Valid') {
    throw 'Installer Authenticode verification failed.'
}
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText("$installer.sha256", "$hash  $([IO.Path]::GetFileName($installer))`n", $utf8)
Copy-Item -LiteralPath (Join-Path $payloadDirectory 'payload-manifest.json') -Destination "$installer.manifest.json" -Force
Copy-Item -LiteralPath $lockPath -Destination "$installer.dependencies.json" -Force
Write-Host "[Installer] Ready: $installer"
Write-Host "[Installer] SHA-256: $hash"
if (-not $signingEnabled) { Write-Host '[Installer] Local unsigned build. Public releases require -RequireSigning.' }
