param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",

    [string]$EngineRoot = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$packageEngineRoot = $repoRoot
if ($EngineRoot) {
    $packageEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
}
$projectRoot = Join-Path $repoRoot "Tests\Fixtures\ProjectPackages"
$packageBuildParent = [System.IO.Path]::GetFullPath(
    (Join-Path $projectRoot ".funke\build")
)
$packageBuildRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $packageBuildParent "packages")
)
$resolvedPackageBuildParent = [System.IO.DirectoryInfo]::new($packageBuildRoot).Parent.FullName
$unexpectedPackageBuildParent = $resolvedPackageBuildParent -ne $packageBuildParent
$unexpectedPackageBuildLeaf = [System.IO.Path]::GetFileName($packageBuildRoot) -ne "packages"
if ($unexpectedPackageBuildParent -or $unexpectedPackageBuildLeaf) {
    throw "Refusing to clear unexpected package smoke output: $packageBuildRoot"
}
if (Test-Path -LiteralPath $packageBuildRoot) {
    Remove-Item -LiteralPath $packageBuildRoot -Recurse -Force
}
$premake = Join-Path $packageEngineRoot "vendor\bin\premake5.exe"
if (-not (Test-Path -LiteralPath $premake -PathType Leaf)) {
    $premake = Join-Path $packageEngineRoot "premake5.exe"
}
$generator = Join-Path $packageEngineRoot "premake\project-packages.lua"
$newPackageScript = Join-Path $packageEngineRoot "scripts\packages\NewPackage.py"
$removePackageScript = Join-Path $packageEngineRoot "scripts\packages\RemovePackage.py"
$bumpPackageScript = Join-Path $packageEngineRoot "scripts\packages\BumpPackageVersion.py"
foreach ($requiredTool in @($premake, $generator, $newPackageScript, $removePackageScript, $bumpPackageScript)) {
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Package smoke dependency missing: $requiredTool"
    }
}

& $premake "--file=$generator" "--funke-project=$projectRoot" vs2022
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -version "[17.0,)" -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild.exe not found via vswhere."
}

$generatedRoot = Join-Path $projectRoot ".funke\build\packages\generated"
$nativeProject = Join-Path $generatedRoot "Pkg.Game.TestPackage.Native\Pkg.Game.TestPackage.Native.vcxproj"
$managedProject = Join-Path $generatedRoot "Pkg.Game.TestPackage\Pkg.Game.TestPackage.csproj"
$pathValue = $env:PATH
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")

& $msbuild $nativeProject /m:1 /nr:false /p:CL_MPCount=1 /p:UseMultiToolTask=false /p:PreferredToolArchitecture=x64 "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

dotnet build $managedProject -c $Configuration -p:Platform=x64 --nologo -v minimal -nodeReuse:false -p:UseSharedCompilation=false
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$outputRoot = Join-Path $projectRoot ".funke\build\packages\bin\$Configuration-windows-x86_64"
$expected = @(
    (Join-Path $outputRoot "Pkg.Game.TestPackage\Pkg.Game.TestPackage.dll"),
    (Join-Path $outputRoot "Pkg.Game.TestPackage.Native\Pkg.Game.TestPackage.Native.dll"),
    (Join-Path $outputRoot "Pkg.Game.TestPackage.Native\Pkg.Game.TestPackage.Native.dll.funkeabi")
)
foreach ($artifact in $expected) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Project package artifact missing: $artifact"
    }
}
$standaloneMetadata = Get-Content -Raw -LiteralPath $expected[2]
if ($standaloneMetadata -notmatch "(?m)^schema=1\r?$" -or
    $standaloneMetadata -notmatch "(?m)^boundary=standalone_c\r?$") {
    throw "Standalone package ABI metadata was invalid: $($expected[2])"
}

if (-not ("FunkePackageSmoke.NativeMethods" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace FunkePackageSmoke
{
    public static class NativeMethods
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr LoadLibrary(string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        public static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool FreeLibrary(IntPtr module);
    }
}
'@
}

$nativeHandle = [FunkePackageSmoke.NativeMethods]::LoadLibrary($expected[1])
if ($nativeHandle -eq [IntPtr]::Zero) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "Project package library was not loadable (Win32 error $errorCode): $($expected[1])"
}
try {
    $nativeExport = [FunkePackageSmoke.NativeMethods]::GetProcAddress(
        $nativeHandle,
        "FunkeTestPackage_Value"
    )
    if ($nativeExport -eq [IntPtr]::Zero) {
        throw "Project package export was not loadable: FunkeTestPackage_Value"
    }
}
finally {
    [void][FunkePackageSmoke.NativeMethods]::FreeLibrary($nativeHandle)
}

$lifecycleRoot = Join-Path $repoRoot ".scratch\package-lifecycle-smoke"
if (Test-Path -LiteralPath $lifecycleRoot) {
    Remove-Item -LiteralPath $lifecycleRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $lifecycleRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "project.json") -Destination $lifecycleRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "Packages") -Destination $lifecycleRoot -Recurse

try {
    & $premake "--file=$generator" "--funke-project=$lifecycleRoot" vs2022
    if ($LASTEXITCODE -ne 0) {
        throw "Initial lifecycle workspace generation failed."
    }

    python $removePackageScript Game.TestPackage --project $lifecycleRoot --keep-files
    if ($LASTEXITCODE -ne 0) {
        throw "Project-local package removal failed."
    }

    $removedProject = Join-Path $lifecycleRoot ".funke\build\packages\generated\Pkg.Game.TestPackage"
    if (Test-Path -LiteralPath $removedProject) {
        throw "Removed package remained in the generated workspace."
    }

    $emptyProject = Join-Path $lifecycleRoot ".funke\build\packages\generated\FunkeProjectPackages.Empty\FunkeProjectPackages.Empty.vcxproj"
    if (-not (Test-Path -LiteralPath $emptyProject -PathType Leaf)) {
        throw "The empty package workspace was not generated."
    }

    python $newPackageScript Game.GeneratedSmoke --layers native+csharp --project $lifecycleRoot --description "CI package lifecycle smoke."
    if ($LASTEXITCODE -ne 0) {
        throw "Project-local package creation failed."
    }

    $lifecycleGenerated = Join-Path $lifecycleRoot ".funke\build\packages\generated"
    $lifecycleNative = Join-Path $lifecycleGenerated "Pkg.Game.GeneratedSmoke.Native\Pkg.Game.GeneratedSmoke.Native.vcxproj"
    $lifecycleManaged = Join-Path $lifecycleGenerated "Pkg.Game.GeneratedSmoke\Pkg.Game.GeneratedSmoke.csproj"
    $restoreAssets = Join-Path (Split-Path $lifecycleManaged) "obj\project.assets.json"
    foreach ($artifact in @($lifecycleNative, $lifecycleManaged, $restoreAssets)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Package lifecycle artifact missing: $artifact"
        }
    }

    & $msbuild $lifecycleNative /m:1 /nr:false /p:CL_MPCount=1 /p:UseMultiToolTask=false /p:PreferredToolArchitecture=x64 "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Generated native package build failed."
    }

    dotnet build $lifecycleManaged -c $Configuration -p:Platform=x64 --nologo -v minimal -nodeReuse:false -p:UseSharedCompilation=false
    if ($LASTEXITCODE -ne 0) {
        throw "Generated managed package build failed."
    }

    $lifecycleOutput = Join-Path $lifecycleRoot ".funke\build\packages\bin\$Configuration-windows-x86_64"
    $lifecycleExpected = @(
        (Join-Path $lifecycleOutput "Pkg.Game.GeneratedSmoke\Pkg.Game.GeneratedSmoke.dll"),
        (Join-Path $lifecycleOutput "Pkg.Game.GeneratedSmoke.Native\Pkg.Game.GeneratedSmoke.Native.dll"),
        (Join-Path $lifecycleOutput "Pkg.Game.GeneratedSmoke.Native\Pkg.Game.GeneratedSmoke.Native.dll.funkeabi")
    )
    foreach ($artifact in $lifecycleExpected) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Generated package output missing: $artifact"
        }
    }
    $engineDll = Join-Path $packageEngineRoot "bin\$Configuration-windows-x86_64\Funke-Engine\Funke-Engine.dll"
    if (-not (Test-Path -LiteralPath $engineDll -PathType Leaf)) {
        throw "Matching engine DLL was not found: $engineDll"
    }
    $engineHash = (Get-FileHash -LiteralPath $engineDll -Algorithm SHA256).Hash.ToLowerInvariant()
    $nativeMetadata = Get-Content -Raw -LiteralPath $lifecycleExpected[2]
    if ($nativeMetadata -notmatch "(?m)^schema=1\r?$" -or
        $nativeMetadata -notmatch "(?m)^boundary=engine_cpp\r?$" -or
        $nativeMetadata -notmatch "(?m)^engine_sha256=$engineHash\r?$") {
        throw "Engine-linked package ABI metadata did not match $engineDll"
    }
    python $bumpPackageScript Game.GeneratedSmoke --patch --project $lifecycleRoot --dry-run
    if ($LASTEXITCODE -ne 0) {
        throw "Package version tool failed."
    }

}
finally {
    if (Test-Path -LiteralPath $lifecycleRoot) {
        Remove-Item -LiteralPath $lifecycleRoot -Recurse -Force
    }
}

Write-Host "[project-package-smoke] OK: lifecycle passed with engine root $packageEngineRoot"
