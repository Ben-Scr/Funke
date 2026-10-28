param([Parameter(Mandatory = $true)][string]$SdkDirectory)

$ErrorActionPreference = 'Stop'

function Get-OptionalItem([string]$Path) {
    try {
        Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    }
    catch [System.Management.Automation.ItemNotFoundException] {
        return $null
    }
}

function Assert-NotLinked($Item) {
    if ($null -ne $Item -and ($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to traverse linked SDK dependency: $($Item.FullName)"
    }
}

try {
    $sdkPath = [System.IO.Path]::GetFullPath($SdkDirectory)
    $sdkPrefix = $sdkPath.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $ancestor = $sdkPath
    while ($ancestor) {
        Assert-NotLinked (Get-OptionalItem $ancestor)
        $ancestor = [System.IO.Path]::GetDirectoryName($ancestor)
    }
    $externalPath = Join-Path $sdkPath 'External'
    Assert-NotLinked (Get-OptionalItem $externalPath)
    $targets = @()
    foreach ($relative in @('External\Index-Physics', 'External\Funke-Physics', 'External\glad', 'Index-Engine')) {
        $targetPath = [System.IO.Path]::GetFullPath((Join-Path $sdkPath $relative))
        if (-not $targetPath.StartsWith($sdkPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Retired dependency escapes SDK: $targetPath"
        }
        $target = Get-OptionalItem $targetPath
        if ($null -eq $target) { continue }
        $pending = [System.Collections.Generic.Stack[System.IO.FileSystemInfo]]::new()
        $pending.Push($target)
        while ($pending.Count -gt 0) {
            $item = $pending.Pop()
            Assert-NotLinked $item
            if ($item.PSIsContainer) {
                foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force -ErrorAction Stop) {
                    $pending.Push($child)
                }
            }
        }
        $targets += $targetPath
    }
    foreach ($targetPath in $targets) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force -ErrorAction Stop
    }
}
catch {
    [Console]::Error.WriteLine("CopyEngineSdk: $($_.Exception.Message)")
    exit 1
}
