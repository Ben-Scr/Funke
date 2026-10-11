$ErrorActionPreference = "Stop"
$configuration = "Debug"
$testArguments = [System.Collections.Generic.List[string]]::new()

foreach ($argument in $args) {
    if ($argument -ieq "--release") {
        $configuration = "Release"
    }
    elseif ($argument -ieq "--dist") {
        $configuration = "Dist"
    }
    else {
        $testArguments.Add($argument)
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$pathValue = $env:PATH
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")

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

Push-Location $repoRoot
try {
    if (-not (Test-Path -LiteralPath "Index.sln")) {
        Write-Host "[RunTests] Generating Visual Studio projects."
        & "vendor\bin\premake5.exe" vs2022
        if ($LASTEXITCODE -ne 0) {
            throw "Premake generation failed."
        }
    }

    $msbuild = Get-MSBuild
    foreach ($testProject in @("Index-Engine-Tests", "Index-Editor-Tests")) {
        Write-Host "[RunTests] Building $testProject ($configuration x64)."
        $projectPath = "Tests\$testProject\$testProject.vcxproj"
        & $msbuild $projectPath /m:1 /nr:false /p:CL_MPCount=1 `
            /p:UseMultiToolTask=false "/p:Configuration=$configuration" `
            /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for $testProject."
        }

        $testExe = Join-Path $repoRoot "bin\$configuration-windows-x86_64\$testProject\$testProject.exe"
        if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
            throw "Test binary was not produced: $testExe"
        }

        Write-Host "[RunTests] Running $testProject."
        & $testExe @testArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed in $testProject."
        }
    }
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Pop-Location
}

exit 0
