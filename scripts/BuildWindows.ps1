[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",

    [ValidateSet("Engine", "Editor", "Runtime", "Launcher", "CLI")]
    [string[]]$Targets = @("Editor", "Runtime"),

    [switch]$PublishBundle,

    [string]$ReleaseTag,

    [string]$SigningCertificatePath,

    [string]$SigningCertificateThumbprint,

    [string]$SigningPasswordEnvironmentVariable = "FUNKE_SIGNING_CERTIFICATE_PASSWORD",

    [string]$SigningTimestampUrl,

    [switch]$RequireSigning
)

$ErrorActionPreference = "Stop"
$pathValue = $env:PATH
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Invoke-BuildMetadataGeneration {
    param([switch]$ForRelease)

    $arguments = @("scripts\generate_build_metadata.py")
    if ($ReleaseTag) {
        $arguments += @("--tag", $ReleaseTag, "--require-clean")
    }
    elseif ($ForRelease -and $env:GITHUB_REF_TYPE -eq "tag") {
        $arguments += @("--tag", $env:GITHUB_REF_NAME, "--require-clean")
    }

    & python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build metadata generation failed."
    }

    $metadataPath = Join-Path $repoRoot "build\BuildMetadata.json"
    if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
        throw "Build metadata was not produced: $metadataPath"
    }
    return Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
}

function Get-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $resolved = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
            Select-Object -First 1
        if ($resolved -and (Test-Path -LiteralPath $resolved)) {
            return $resolved
        }
    }

    foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
        $candidate = "C:\Program Files\Microsoft Visual Studio\2022\$edition\MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "Visual Studio MSBuild was not found. Install Visual Studio with the MSVC v143 workload."
}

function Invoke-ManagedBuild {
    param(
        [string]$BuildConfiguration,
        [string[]]$Projects,
        [switch]$EmitSymbols
    )

    foreach ($project in $Projects) {
        $projectPath = Join-Path $repoRoot $project
        Write-Host "[BuildWindows] dotnet $BuildConfiguration $project"
        $arguments = @(
            "build", $projectPath,
            "-c", $BuildConfiguration,
            "-p:Platform=x64",
            "--nologo", "-v", "minimal", "-nodeReuse:false",
            "-p:UseSharedCompilation=false"
        )
        if ($EmitSymbols) {
            $arguments += @("-p:DebugSymbols=true", "-p:DebugType=portable")
        }
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Managed build failed: $project ($BuildConfiguration)"
        }
    }
}

function Invoke-NativeBuild {
    param(
        [string]$BuildConfiguration,
        [string[]]$BuildTargets,
        [switch]$EmitSymbols
    )

    $msbuild = Get-MSBuild
    $projectMap = @{
        Engine = "Funke-Engine\Funke-Engine.vcxproj"
        Editor = "Funke-Editor\Funke-Editor.vcxproj"
        Runtime = "Funke-Runtime\Funke-Runtime.vcxproj"
        Launcher = "Funke-Launcher\Funke-Launcher.vcxproj"
        CLI = "Funke-CLI\Funke-CLI.vcxproj"
    }

    foreach ($target in $BuildTargets) {
        $projectPath = Join-Path $repoRoot $projectMap[$target]
        Write-Host "[BuildWindows] MSBuild $BuildConfiguration $target"
        $arguments = @(
            $projectPath,
            "/m:1", "/nr:false", "/p:CL_MPCount=1",
            "/p:UseMultiToolTask=false", "/p:Configuration=$BuildConfiguration",
            "/p:Platform=x64", "/p:PreferredToolArchitecture=x64",
            "/v:minimal", "/nologo"
        )
        if ($EmitSymbols) {
            $arguments += @(
                "/p:DebugInformationFormat=ProgramDatabase",
                "/p:GenerateDebugInformation=true"
            )
        }
        & $msbuild @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Native build failed: $target ($BuildConfiguration)"
        }
    }
}
function Invoke-Premake {
    param([switch]$NoProfiler)

    $premake = Join-Path $repoRoot "vendor\bin\premake5.exe"
    $arguments = @("vs2022")
    if ($NoProfiler) {
        $arguments = @("--no-profiler", "vs2022")
    }

    & $premake @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Premake generation failed."
    }
}

function Get-WindowsSdkTool {
    param([string]$ToolName)

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsRoot -PathType Container) {
        $candidate = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter $ToolName -File |
            Where-Object { $_.Directory.Name -eq "x64" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    $fromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }
    throw "$ToolName was not found in the Windows SDK or on PATH."
}

function New-ManagedVersionResource {
    $resourceCompiler = Get-WindowsSdkTool "rc.exe"
    $source = Join-Path $repoRoot "build\ManagedVersion.rc"
    $output = Join-Path $repoRoot "build\ManagedVersion.res"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Managed version resource source was not generated: $source"
    }

    $resourceCompilerDirectory = [System.IO.DirectoryInfo]::new(
        [System.IO.Path]::GetDirectoryName($resourceCompiler)
    )
    $sdkVersionDirectory = $resourceCompilerDirectory.Parent
    $sdkRoot = if ($sdkVersionDirectory -and $sdkVersionDirectory.Parent) {
        $sdkVersionDirectory.Parent.Parent
    }
    $arguments = @("/nologo", "/fo$output")
    if ($sdkRoot -and $sdkVersionDirectory) {
        $includeRoot = Join-Path $sdkRoot.FullName ("Include\" + $sdkVersionDirectory.Name)
        foreach ($includeLeaf in @("shared", "um")) {
            $includeDirectory = Join-Path $includeRoot $includeLeaf
            if (Test-Path -LiteralPath $includeDirectory -PathType Container) {
                $arguments += "/I$includeDirectory"
            }
        }
    }
    $arguments += $source
    & $resourceCompiler @arguments
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw "Managed version resource compilation failed."
    }
}

function Assert-DotNetHostVersion {
    $nethost = Join-Path $repoRoot "External\dotnet\lib\nethost.dll"
    if (-not (Test-Path -LiteralPath $nethost -PathType Leaf)) {
        throw ".NET host library was not found: $nethost"
    }

    $productVersion = (Get-Item -LiteralPath $nethost).VersionInfo.ProductVersion
    if ($productVersion -notmatch '^10\.') {
        throw "Funke targets .NET 10, but bundled nethost is '$productVersion'. Run scripts\setup-dotnet.ps1."
    }
}

function Reset-PublishOutput {
    $publishParent = [System.IO.Path]::GetFullPath(
        (Join-Path $repoRoot "bin\Release-windows-x86_64")
    )
    $publishDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $publishParent "Funke-Launcher")
    )
    $resolvedParent = [System.IO.DirectoryInfo]::new($publishDirectory).Parent.FullName
    $unexpectedParent = $resolvedParent -ne $publishParent
    $unexpectedLeaf = [System.IO.Path]::GetFileName($publishDirectory) -ne "Funke-Launcher"
    if ($unexpectedParent -or $unexpectedLeaf) {
        throw "Refusing to clear unexpected publish output: $publishDirectory"
    }
    if (Test-Path -LiteralPath $publishDirectory) {
        Remove-Item -LiteralPath $publishDirectory -Recurse -Force
    }
}

function Remove-EmbeddedGitMetadata {
    param([string]$PublishDirectory)

    $expectedPublishDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $repoRoot "bin\Release-windows-x86_64\Funke-Launcher")
    )
    $publishDirectoryPath = [System.IO.Path]::GetFullPath($PublishDirectory)
    if ($publishDirectoryPath -ne $expectedPublishDirectory) {
        throw "Refusing to clean Git metadata from unexpected publish output: $publishDirectoryPath"
    }

    $sdkDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $publishDirectoryPath "EngineSDK")
    )
    $sdkParent = [System.IO.DirectoryInfo]::new($sdkDirectory).Parent.FullName
    $unexpectedParent = $sdkParent -ne $publishDirectoryPath
    $unexpectedLeaf = [System.IO.Path]::GetFileName($sdkDirectory) -ne "EngineSDK"
    if ($unexpectedParent -or $unexpectedLeaf) {
        throw "Refusing to clean unexpected SDK directory: $sdkDirectory"
    }
    if (-not (Test-Path -LiteralPath $sdkDirectory -PathType Container)) {
        throw "Published EngineSDK was not found: $sdkDirectory"
    }

    $sdkPrefix = $sdkDirectory.TrimEnd('\') + '\'
    $metadata = @(Get-ChildItem -LiteralPath $sdkDirectory -Recurse -Force |
        Where-Object { $_.Name -ieq ".git" } |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($entry in $metadata) {
        $entryPath = [System.IO.Path]::GetFullPath($entry.FullName)
        $insideSdk = $entryPath.StartsWith(
            $sdkPrefix,
            [System.StringComparison]::OrdinalIgnoreCase
        )
        $isGitMetadata = [System.IO.Path]::GetFileName($entryPath) -ieq ".git"
        if (-not $insideSdk -or -not $isGitMetadata) {
            throw "Refusing to remove unexpected publish entry: $entryPath"
        }
        if ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to traverse Git metadata reparse point: $entryPath"
        }

        if ($entry.PSIsContainer) {
            Remove-Item -LiteralPath $entryPath -Recurse -Force
        }
        else {
            Remove-Item -LiteralPath $entryPath -Force
        }
    }

    $remaining = @(Get-ChildItem -LiteralPath $sdkDirectory -Recurse -Force |
        Where-Object { $_.Name -ieq ".git" })
    if ($remaining.Count -ne 0) {
        throw "Published EngineSDK still contains Git metadata."
    }
}

function Get-SigningPassword {
    if (-not $SigningPasswordEnvironmentVariable) {
        return ""
    }
    if ($SigningPasswordEnvironmentVariable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
        throw "Signing password environment variable name is invalid."
    }
    return [Environment]::GetEnvironmentVariable($SigningPasswordEnvironmentVariable, "Process")
}

function Get-SigningCertificate {
    if ($SigningCertificatePath -and $SigningCertificateThumbprint) {
        throw "Specify either SigningCertificatePath or SigningCertificateThumbprint, not both."
    }

    if ($SigningCertificatePath) {
        $resolvedPath = (Resolve-Path -LiteralPath $SigningCertificatePath).Path
        $password = Get-SigningPassword
        $flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
        return [pscustomobject]@{
            Certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $resolvedPath,
                $password,
                $flags
            )
            UseMachineStore = $false
        }
    }

    if ($SigningCertificateThumbprint) {
        $normalized = $SigningCertificateThumbprint.Replace(" ", "").ToUpperInvariant()
        foreach ($store in @(
            @{ Path = "Cert:\CurrentUser\My"; UseMachineStore = $false },
            @{ Path = "Cert:\LocalMachine\My"; UseMachineStore = $true }
        )) {
            $certificate = Get-ChildItem -LiteralPath $store.Path |
                Where-Object { $_.Thumbprint -eq $normalized -and $_.HasPrivateKey } |
                Select-Object -First 1
            if ($certificate) {
                return [pscustomobject]@{
                    Certificate = $certificate
                    UseMachineStore = $store.UseMachineStore
                }
            }
        }
        throw "A code-signing certificate with thumbprint $normalized and a private key was not found."
    }

    return $null
}

function Invoke-AuthenticodeSigning {
    param(
        [string]$PublishDirectory,
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [switch]$UseMachineStore
    )

    if (-not $Certificate) {
        if ($RequireSigning) {
            throw "Signing is required, but no certificate path or thumbprint was supplied."
        }
        Write-Host "[BuildWindows] Authenticode signing skipped; no certificate was configured."
        return
    }

    $signTool = Get-WindowsSdkTool "signtool.exe"
    $files = @(Get-ChildItem -LiteralPath $PublishDirectory -Recurse -File |
        Where-Object {
            $_.Name -match '^Funke-.*\.(exe|dll)$' -and
            $_.Extension -in @('.exe', '.dll')
        })
    if ($files.Count -eq 0) {
        throw "No first-party executables or libraries were found to sign."
    }

    foreach ($file in $files) {
        $arguments = @("sign", "/fd", "SHA256")
        if ($SigningCertificatePath) {
            $arguments += @("/f", (Resolve-Path -LiteralPath $SigningCertificatePath).Path)
            $password = Get-SigningPassword
            if ($null -ne $password -and $password.Length -gt 0) {
                $arguments += @("/p", $password)
            }
        }
        else {
            if ($UseMachineStore) {
                $arguments += "/sm"
            }
            $arguments += @("/sha1", $Certificate.Thumbprint)
        }
        if ($SigningTimestampUrl) {
            $arguments += @("/tr", $SigningTimestampUrl, "/td", "SHA256")
        }
        $arguments += $file.FullName

        & $signTool @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Authenticode signing failed: $($file.FullName)"
        }
    }
}

function Get-RequiredPrivateSymbolPaths {
    return @(
        "bin\Release-windows-x86_64\Funke-Engine\Funke-Engine.pdb",
        "bin\Release-windows-x86_64\Funke-Editor\Funke-Editor.pdb",
        "bin\Release-windows-x86_64\Funke-Launcher\Funke-Launcher.pdb",
        "bin\Release-windows-x86_64\Funke-Runtime\Funke-Runtime.pdb",
        "bin\Release-windows-x86_64\Funke-ScriptCore\Funke-ScriptCore.pdb",
        "bin\Release-windows-x86_64\Funke-PackageTool\Funke-PackageTool.pdb",
        "bin\Dist-windows-x86_64\Funke-Engine\Funke-Engine.pdb",
        "bin\Dist-windows-x86_64\Funke-Runtime\Funke-Runtime.pdb",
        "bin\Dist-windows-x86_64\Funke-ScriptCore\Funke-ScriptCore.pdb"
    )
}

function Remove-StalePrivateSymbols {
    foreach ($relativePath in Get-RequiredPrivateSymbolPaths) {
        $path = Join-Path $repoRoot $relativePath
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

function New-PrivateSymbolsArchive {
    param(
        [string]$PublishDirectory,
        [pscustomobject]$Metadata
    )

    $requiredSymbols = @(Get-RequiredPrivateSymbolPaths)
    foreach ($relativePath in $requiredSymbols) {
        $path = Join-Path $repoRoot $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required private symbol file was not produced: $path"
        }
    }

    $publishParent = [System.IO.DirectoryInfo]::new($PublishDirectory).Parent.FullName
    $symbolsDirectory = Join-Path $publishParent "PrivateSymbols"
    if (-not (Test-Path -LiteralPath $symbolsDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $symbolsDirectory | Out-Null
    }
    $symbolsArchive = Join-Path $symbolsDirectory $Metadata.symbolsArchiveName
    if ([System.IO.DirectoryInfo]::new($symbolsArchive).Parent.FullName -ne $symbolsDirectory) {
        throw "Refusing to write private symbols outside the symbols directory: $symbolsArchive"
    }
    if (Test-Path -LiteralPath $symbolsArchive) {
        Remove-Item -LiteralPath $symbolsArchive -Force
    }

    $tempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
    $stageDirectory = Join-Path $tempParent ("Funke Symbols " + [guid]::NewGuid())
    try {
        New-Item -ItemType Directory -Path $stageDirectory | Out-Null
        foreach ($relativePath in $requiredSymbols) {
            $source = Join-Path $repoRoot $relativePath
            $destination = Join-Path $stageDirectory $relativePath
            New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination
        }

        $binaries = @(Get-ChildItem -LiteralPath $PublishDirectory -Recurse -File |
            Where-Object { $_.Name -match '^Funke-.*\.(exe|dll)$' } |
            ForEach-Object {
                [ordered]@{
                    path = [System.IO.Path]::GetRelativePath($PublishDirectory, $_.FullName).Replace('\', '/')
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            })
        $symbolManifest = [ordered]@{
            schemaVersion = 1
            productVersion = $Metadata.version
            buildId = $Metadata.buildId
            commit = $Metadata.commit
            binaries = $binaries
            symbols = @($requiredSymbols | ForEach-Object { $_.Replace('\', '/') })
        }
        $manifestPath = Join-Path $stageDirectory "symbols-manifest.json"
        [System.IO.File]::WriteAllText(
            $manifestPath,
            ($symbolManifest | ConvertTo-Json -Depth 5) + "`n",
            [System.Text.UTF8Encoding]::new($false)
        )
        Copy-Item -LiteralPath (Join-Path $repoRoot "build\BuildMetadata.json") `
            -Destination (Join-Path $stageDirectory "BuildMetadata.json")

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::CreateFromDirectory(
            $stageDirectory,
            $symbolsArchive,
            [System.IO.Compression.CompressionLevel]::Optimal,
            $false
        )
    }
    finally {
        if (Test-Path -LiteralPath $stageDirectory) {
            Remove-Item -LiteralPath $stageDirectory -Recurse -Force
        }
    }

    Write-Host "[BuildWindows] Private symbols: $symbolsArchive"
    return $symbolsArchive
}

function Remove-PublicSymbols {
    param([string]$PublishDirectory)

    foreach ($symbol in Get-ChildItem -LiteralPath $PublishDirectory -Recurse -Filter *.pdb -File) {
        Remove-Item -LiteralPath $symbol.FullName -Force
    }
}

function New-DetachedChecksumSignature {
    param(
        [string]$ChecksumPath,
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )

    if (-not $Certificate) {
        return $null
    }
    if (-not $Certificate.HasPrivateKey) {
        throw "The signing certificate does not contain a private key."
    }

    Add-Type -AssemblyName System.Security.Cryptography.Pkcs
    $content = [System.Security.Cryptography.Pkcs.ContentInfo]::new(
        [System.IO.File]::ReadAllBytes($ChecksumPath)
    )
    $signedCms = [System.Security.Cryptography.Pkcs.SignedCms]::new($content, $true)
    $signer = [System.Security.Cryptography.Pkcs.CmsSigner]::new($Certificate)
    $signer.IncludeOption = [System.Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    $signedCms.ComputeSignature($signer)

    $signaturePath = "$ChecksumPath.p7s"
    [System.IO.File]::WriteAllBytes($signaturePath, $signedCms.Encode())
    return $signaturePath
}

function Publish-VerifiedArchive {
    param(
        [string]$BuildConfiguration,
        [string]$PublishDirectory,
        [pscustomobject]$Metadata,
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$SigningCertificate
    )

    $publishParent = [System.IO.DirectoryInfo]::new($PublishDirectory).Parent.FullName
    $archiveName = $Metadata.archiveName
    $archivePath = Join-Path $publishParent $archiveName
    $checksumPath = "$archivePath.sha256"
    foreach ($outputPath in @($archivePath, $checksumPath, "$checksumPath.p7s")) {
        $outputParent = [System.IO.DirectoryInfo]::new($outputPath).Parent.FullName
        if ($outputParent -ne $publishParent) {
            throw "Refusing to replace unexpected publish artifact: $outputPath"
        }
        if (Test-Path -LiteralPath $outputPath) {
            Remove-Item -LiteralPath $outputPath -Force
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $PublishDirectory,
        $archivePath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    $tempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
    $verifyLeaf = "Funke Bundle Pr{0}fung {1}" -f [char]0x00FC, [guid]::NewGuid()
    $verifyDirectory = Join-Path $tempParent $verifyLeaf
    $verifyDirectory = [System.IO.Path]::GetFullPath($verifyDirectory)
    if ([System.IO.DirectoryInfo]::new($verifyDirectory).Parent.FullName -ne $tempParent) {
        throw "Refusing to use unexpected verification directory: $verifyDirectory"
    }

    try {
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $verifyDirectory)
        & cmd /d /c "scripts\verify-standalone-zip.bat" $BuildConfiguration $verifyDirectory
        if ($LASTEXITCODE -ne 0) {
            throw "Extracted publish archive verification failed."
        }
    }
    finally {
        if (Test-Path -LiteralPath $verifyDirectory) {
            Remove-Item -LiteralPath $verifyDirectory -Recurse -Force
        }
    }

    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksum = "$hash  $archiveName`r`n"
    [System.IO.File]::WriteAllText(
        $checksumPath,
        $checksum,
        [System.Text.UTF8Encoding]::new($false)
    )

    $signaturePath = New-DetachedChecksumSignature $checksumPath $SigningCertificate

    Write-Host "[BuildWindows] Archive: $archivePath"
    Write-Host "[BuildWindows] SHA-256: $hash"
    if ($signaturePath) {
        Write-Host "[BuildWindows] Checksum signature: $signaturePath"
    }
    return [pscustomobject]@{
        ArchivePath = $archivePath
        ChecksumPath = $checksumPath
        SignaturePath = $signaturePath
    }
}


Push-Location $repoRoot
try {
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw "Python 3.10 or newer was not found on PATH."
    }
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        throw ".NET 10 SDK was not found on PATH."
    }
    if ($ReleaseTag -and -not $PublishBundle) {
        throw "ReleaseTag is valid only with PublishBundle."
    }
    $isTagBuild = [bool]$ReleaseTag -or $env:GITHUB_REF_TYPE -eq "tag"
    if ($isTagBuild) {
        $RequireSigning = $true
    }

    $metadata = Invoke-BuildMetadataGeneration -ForRelease:$PublishBundle
    New-ManagedVersionResource
    Assert-DotNetHostVersion

    if ($PublishBundle) {
        Remove-StalePrivateSymbols
        Invoke-Premake
        Invoke-ManagedBuild "Release" @(
            "Funke-ScriptCore\Funke-ScriptCore.csproj",
            "Funke-PackageTool\Funke-PackageTool.csproj",
            "Funke-ScriptCompiler\Funke-ScriptCompiler.csproj",
            "Funke-Sandbox\Funke-Sandbox.csproj"
        )
        Invoke-NativeBuild "Release" @("Editor", "Runtime", "CLI")

        Invoke-ManagedBuild "Dist" @("Funke-ScriptCore\Funke-ScriptCore.csproj") -EmitSymbols
        Invoke-Premake -NoProfiler
        try {
            $staleProfilerDll = Join-Path $repoRoot "bin\Dist-windows-x86_64\Funke-Runtime\Tracy.dll"
            if (Test-Path -LiteralPath $staleProfilerDll) {
                Remove-Item -LiteralPath $staleProfilerDll -Force
            }
            Invoke-NativeBuild "Dist" @("Runtime") -EmitSymbols
        }
        finally {
            Invoke-Premake
        }

        Reset-PublishOutput
        Invoke-NativeBuild "Release" @("Launcher")

        $publishDirectory = Join-Path $repoRoot `
            "bin\Release-windows-x86_64\Funke-Launcher"
        Remove-EmbeddedGitMetadata $publishDirectory

        $signingIdentity = Get-SigningCertificate
        $signingCertificate = if ($signingIdentity) { $signingIdentity.Certificate } else { $null }
        $useMachineStore = $signingIdentity -and $signingIdentity.UseMachineStore
        Invoke-AuthenticodeSigning $publishDirectory $signingCertificate -UseMachineStore:$useMachineStore
        New-PrivateSymbolsArchive $publishDirectory $metadata | Out-Null
        Remove-PublicSymbols $publishDirectory

        & cmd /d /c "scripts\verify-standalone-zip.bat" Release $publishDirectory
        if ($LASTEXITCODE -ne 0) {
            throw "Standalone bundle verification failed."
        }

        $releaseVerificationArguments = @(
            "-BundleDirectory", $publishDirectory,
            "-MetadataPath", (Join-Path $repoRoot "build\BuildMetadata.json")
        )
        if ($isTagBuild) {
            $releaseVerificationArguments += "-RequireReleaseIdentity"
        }
        if ($RequireSigning) {
            $releaseVerificationArguments += "-RequireSigned"
        }
        & "scripts\ci\verify_windows_release.ps1" @releaseVerificationArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Windows release metadata and hardening verification failed."
        }

        $artifacts = Publish-VerifiedArchive "Release" $publishDirectory $metadata $signingCertificate
        $artifactVerificationArguments = @(
            "-ArchivePath", $artifacts.ArchivePath,
            "-ChecksumPath", $artifacts.ChecksumPath
        )
        if ($RequireSigning) {
            $artifactVerificationArguments += "-RequireSignature"
        }
        if ($signingCertificate) {
            $artifactVerificationArguments += @(
                "-ExpectedSignerThumbprint",
                $signingCertificate.Thumbprint
            )
        }
        & "scripts\VerifyRelease.ps1" @artifactVerificationArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Release checksum verification failed."
        }

        Write-Host "[BuildWindows] Publish bundle is ready under bin\Release-windows-x86_64."
        exit 0
    }

    Invoke-ManagedBuild $Configuration @(
        "Funke-ScriptCore\Funke-ScriptCore.csproj",
        "Funke-PackageTool\Funke-PackageTool.csproj",
        "Funke-ScriptCompiler\Funke-ScriptCompiler.csproj",
        "Funke-Sandbox\Funke-Sandbox.csproj"
    )
    Invoke-NativeBuild $Configuration $Targets
}
finally {
    Pop-Location
}
