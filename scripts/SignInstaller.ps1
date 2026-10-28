[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$SignToolPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function ConvertTo-WindowsArgument {
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.IndexOf([char]0) -ge 0) { throw "A native argument contains a null character." }
    $quoted = [Text.StringBuilder]::new()
    [void]$quoted.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]92) {
            $backslashes++
            continue
        }
        if ($character -eq [char]34) {
            [void]$quoted.Append([char]92, 2 * $backslashes + 1)
        }
        else {
            [void]$quoted.Append([char]92, $backslashes)
        }
        [void]$quoted.Append($character)
        $backslashes = 0
    }
    [void]$quoted.Append([char]92, 2 * $backslashes)
    [void]$quoted.Append('"')
    return $quoted.ToString()
}

function Invoke-SigningTool {
    param([string]$Executable, [string[]]$Arguments)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    # Explicit CRT quoting bypasses Windows PowerShell 5.1's lossy native argument conversion.
    $startInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw "The signing tool did not start." }
        # Drain both pipes without logging diagnostics that could repeat a password.
        $standardOutput = $process.StandardOutput.ReadToEndAsync()
        $standardError = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $null = $standardOutput.GetAwaiter().GetResult()
        $null = $standardError.GetAwaiter().GetResult()
        return $process.ExitCode
    }
    catch { throw "The signing tool could not be started or completed." }
    finally {
        $startInfo.Arguments = ""
        $process.Dispose()
    }
}

function Find-SignTool {
    if ($SignToolPath) {
        return (Get-Item -LiteralPath $SignToolPath -ErrorAction Stop).FullName
    }

    $sdkRoots = @()
    foreach ($key in @(
        "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots"
    )) {
        $root = Get-ItemPropertyValue -LiteralPath $key -Name KitsRoot10 -ErrorAction SilentlyContinue
        if ($root) { $sdkRoots += Join-Path $root "bin" }
    }
    if (${env:ProgramFiles(x86)}) {
        $sdkRoots += Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    }
    foreach ($root in ($sdkRoots | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $versions = Get-ChildItem -LiteralPath $root -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName "x64\signtool.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
        $candidate = Join-Path $root "x64\signtool.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $command = Get-Command signtool.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) { return $command.Source }
    throw "signtool.exe was not found. Install the Windows SDK signing tools."
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "Installer signing requires Windows."
}
$target = Get-Item -LiteralPath $Path -ErrorAction Stop
if ($target.PSIsContainer) { throw "The signing target must be a file." }
$certificatePath = $env:FUNKE_INSTALLER_SIGN_CERT_PATH
$thumbprint = $env:FUNKE_INSTALLER_SIGN_THUMBPRINT
$passwordVariable = $env:FUNKE_INSTALLER_SIGN_PASSWORD_ENV
$timestampUrl = $env:FUNKE_INSTALLER_SIGN_TIMESTAMP
if ($certificatePath -and $thumbprint) {
    throw "Configure a signing certificate path or thumbprint, not both."
}
if (-not $certificatePath -and -not $thumbprint) {
    throw "No installer signing certificate was configured."
}
if ($timestampUrl) {
    $timestampUri = $null
    if (-not [Uri]::TryCreate($timestampUrl, [UriKind]::Absolute, [ref]$timestampUri) -or
        $timestampUri.Scheme -notin @("http", "https") -or $timestampUri.UserInfo) {
        throw "The timestamp URL must be an HTTP(S) URL without credentials."
    }
}

$certificate = $null
$password = $null
$signArguments = @("sign", "/q", "/fd", "SHA256")
try {
    if ($certificatePath) {
        $certificateFile = Get-Item -LiteralPath $certificatePath -ErrorAction Stop
        if ($certificateFile.PSIsContainer) { throw "The signing certificate must be a PFX file." }
        if (-not $passwordVariable) { $passwordVariable = "FUNKE_SIGNING_CERTIFICATE_PASSWORD" }
        if ($passwordVariable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
            throw "The signing password environment variable name is invalid."
        }
        $password = [Environment]::GetEnvironmentVariable($passwordVariable, "Process")
        try {
            $certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $certificateFile.FullName, $password,
                [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
        }
        catch { throw "Could not open the signing PFX. Check its format and password environment variable." }
        $signArguments += @("/f", $certificateFile.FullName)
        if ($password) { $signArguments += @("/p", $password) }
    }
    else {
        $normalized = ($thumbprint -replace '\s', '').ToUpperInvariant()
        if ($normalized -notmatch '^[0-9A-F]{40}$') { throw "The certificate thumbprint must contain 40 hexadecimal digits." }
        foreach ($store in @("Cert:\CurrentUser\My", "Cert:\LocalMachine\My")) {
            $certificate = Get-ChildItem -LiteralPath $store |
                Where-Object { $_.Thumbprint -eq $normalized -and $_.HasPrivateKey } |
                Select-Object -First 1
            if ($certificate) {
                if ($store -eq "Cert:\LocalMachine\My") { $signArguments += "/sm" }
                break
            }
        }
        if (-not $certificate) { throw "The requested certificate and private key were not found in the My certificate stores." }
        $signArguments += @("/sha1", $certificate.Thumbprint)
    }
    if (-not $certificate.HasPrivateKey) { throw "The signing certificate does not contain a private key." }
    if ($timestampUrl) { $signArguments += @("/tr", $timestampUrl, "/td", "SHA256") }
    $signArguments += $target.FullName
    $signTool = Find-SignTool

    $exitCode = Invoke-SigningTool $signTool $signArguments
    if ($exitCode -ne 0) { throw "Installer signing failed (signtool exit code $exitCode)." }
    $verifyArguments = @("verify", "/q", "/pa", "/all")
    if ($timestampUrl) { $verifyArguments += "/tw" }
    $verifyArguments += $target.FullName
    $exitCode = Invoke-SigningTool $signTool $verifyArguments
    if ($exitCode -ne 0) { throw "Installer signature verification failed (signtool exit code $exitCode)." }
    $signature = Get-AuthenticodeSignature -LiteralPath $target.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw "The installer signature is not valid for the configured certificate."
    }
    if ($timestampUrl -and -not $signature.TimeStamperCertificate) {
        throw "The installer signature has no trusted timestamp."
    }
    Write-Host "[SignInstaller] Signed and verified $($target.Name)"
}
finally {
    $password = $null
    $signArguments = $null
    if ($certificate) { $certificate.Dispose() }
}
