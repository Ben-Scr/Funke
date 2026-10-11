$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe was not found."
}
$msbuild = & $vswhere -latest -products * `
    -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msbuild) {
    throw "Visual Studio MSBuild was not found."
}

Push-Location $repoRoot
try {
    & "vendor\bin\premake5.exe" --core-validation --module-profile=core vs2022
    if ($LASTEXITCODE -ne 0) {
        throw "Core workspace generation failed."
    }
    $project = Join-Path $repoRoot "premake\generated\core\Index-Engine\Index-Engine.vcxproj"
    & $msbuild $project /t:Build /m:1 /nr:false `
        /p:CL_MPCount=1 /p:UseMultiToolTask=false `
        /p:Configuration=Release /p:Platform=x64 `
        /p:PreferredToolArchitecture=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Core-only Index-Engine build failed."
    }
}
finally {
    Pop-Location
}
