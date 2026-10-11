@echo off
REM Verify that the Index-Launcher folder is shippable as a standalone zip.
REM Checks file presence directly under bin\<Config>-windows-x86_64\
REM Index-Launcher\ and inside the bundled EngineSDK so reach-backs into
REM the source tree are caught.
REM
REM This is a structural check only. The full clean-machine validation
REM must run on a separate Windows box with no source tree on disk, see
REM the plan's Verification section.
REM
REM Usage:
REM   scripts\verify-standalone-zip.bat            uses Release config
REM   scripts\verify-standalone-zip.bat Debug      override config
REM   scripts\verify-standalone-zip.bat Release C:\path\to\bundle

setlocal EnableDelayedExpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "PLATFORM=windows-x86_64"
set "ROOT=%~dp0.."
pushd "%ROOT%" >nul

set "LAUNCHER=%~2"
if "%LAUNCHER%"=="" set "LAUNCHER=%ROOT%\bin\%CONFIG%-%PLATFORM%\Index-Launcher"
set "SDK=%LAUNCHER%\EngineSDK"

echo [verify-standalone-zip] target: %LAUNCHER%
echo.

set "FAIL=0"

REM ---- Required files directly under the launcher folder
call :RequireFile "%LAUNCHER%\Index-Launcher.exe"
call :RequireFile "%LAUNCHER%\Index-Editor.exe"
call :RequireFile "%LAUNCHER%\Index-Runtime.exe"
call :RequireFile "%LAUNCHER%\Index-Engine.dll"
call :RequireFile "%LAUNCHER%\GLFW.dll"
call :RequireFile "%LAUNCHER%\Tracy.dll"
call :RequireFile "%LAUNCHER%\nethost.dll"
call :RequireFile "%LAUNCHER%\Index-ScriptCore.dll"
call :RequireFile "%LAUNCHER%\Index-PackageTool.exe"
call :RequireFile "%LAUNCHER%\Index-PackageTool.dll"
call :RequireFile "%LAUNCHER%\Index-PackageTool.runtimeconfig.json"
call :RequireFile "%LAUNCHER%\Index-PackageTool.deps.json"
call :RequireFile "%LAUNCHER%\Index-ScriptCore.runtimeconfig.json"
call :RequireFile "%LAUNCHER%\Index-ScriptCore.deps.json"
call :RequireFile "%LAUNCHER%\BuildMetadata.json"
call :RequireAbsent "%LAUNCHER%\Index-Launcher.pdb"
call :RequireDir  "%LAUNCHER%\IndexAssets"
call :RequireDir  "%LAUNCHER%\IndexAssets\Fonts"
call :RequireDir  "%LAUNCHER%\IndexAssets\Shader"

REM ---- EngineSDK folder must exist with the expected layout
call :RequireDir  "%SDK%"
call :RequireDir  "%SDK%\Index-Engine\src"
call :RequireFile "%SDK%\Index-ScriptCore\Index-ScriptCore.csproj"
call :RequireFile "%SDK%\Index-ScriptCore\Index-ScriptCore.dll"
call :RequireFile "%SDK%\premake5.exe"
call :RequireFile "%SDK%\premake\project-packages.lua"
call :RequireFile "%SDK%\premake\package-loader.lua"
call :RequireFile "%SDK%\premake\fix_csharp_platforms.lua"
call :RequireFile "%SDK%\scripts\BuildProjectPackages.ps1"
call :RequireFile "%SDK%\scripts\packages\NewPackage.py"
call :RequireFile "%SDK%\scripts\packages\RemovePackage.py"
call :RequireFile "%SDK%\scripts\packages\BumpPackageVersion.py"
call :RequireFile "%SDK%\Index-ScriptCore\Index-ScriptCore.runtimeconfig.json"
call :RequireFile "%SDK%\Index-ScriptCore\Index-ScriptCore.deps.json"
call :RequireFile "%SDK%\Index-Engine\src\Core\BuildMetadata.generated.hpp"
call :RequireFile "%SDK%\bin\%CONFIG%-%PLATFORM%\Index-Engine\Index-Engine.lib"
call :RequireFile "%SDK%\bin\%CONFIG%-%PLATFORM%\Index-Engine\Index-Engine.dll"
call :RequireFile "%SDK%\bin\Dist-%PLATFORM%\Index-Engine\Index-Engine.lib"
call :RequireFile "%SDK%\bin\Dist-%PLATFORM%\Index-Engine\Index-Engine.dll"
call :RequireFile "%SDK%\External\dawn\include\webgpu\webgpu_cpp.h"
call :RequireFile "%SDK%\External\dawn\build\gen\include\dawn\webgpu_cpp_print.h"

REM ---- Redistribution notices must be self-contained; source-tree links are not enough.
call :RequireFile "%SDK%\Licenses\Index\LICENSE.txt"
call :RequireFile "%SDK%\Licenses\Index\THIRD_PARTY_LICENSES.md"
call :RequireFile "%SDK%\Licenses\Box2D\LICENSE"
call :RequireFile "%SDK%\Licenses\EnTT\LICENSE"
call :RequireFile "%SDK%\Licenses\GLM\copying.txt"
call :RequireFile "%SDK%\Licenses\GLFW\LICENSE.md"
call :RequireFile "%SDK%\Licenses\Dear-ImGui\LICENSE.txt"
call :RequireFile "%SDK%\Licenses\ImGuizmo\LICENSE"
call :RequireFile "%SDK%\Licenses\magic_enum\LICENSE"
call :RequireFile "%SDK%\Licenses\miniaudio\LICENSE"
call :RequireFile "%SDK%\Licenses\spdlog\LICENSE"
call :RequireFile "%SDK%\Licenses\stb\LICENSE"
call :RequireFile "%SDK%\Licenses\Tracy\LICENSE"
call :RequireFile "%SDK%\Licenses\Dawn\LICENSE"
call :RequireFile "%SDK%\Licenses\concurrentqueue\LICENSE.md"
call :RequireFile "%SDK%\Licenses\RapidXML\LICENSE.txt"
call :RequireFile "%SDK%\Licenses\Fonts\LICENSES.md"
call :RequireFile "%SDK%\Licenses\DotNet\LICENSE.txt"
call :RequireFile "%SDK%\Licenses\DotNet\ThirdPartyNotices.txt"
call :RequireFile "%SDK%\Licenses\Dawn-Third-Party\abseil-cpp\LICENSE"
call :RequireFile "%SDK%\Licenses\Dawn-Third-Party\directx-shader-compiler\LICENSE"

REM ---- Active-config runtime used by repo-less Development exports.
call :RequireFile "%SDK%\Runtime-Development\Index-Runtime.exe"
call :RequireFile "%SDK%\Runtime-Development\Index-Engine.dll"
call :RequireFile "%SDK%\Runtime-Development\GLFW.dll"
call :RequireFile "%SDK%\Runtime-Development\Tracy.dll"
call :RequireFile "%SDK%\Runtime-Development\Index-ScriptCore.dll"
call :RequireFile "%SDK%\Runtime-Development\Index-ScriptCore.runtimeconfig.json"
call :RequireFile "%SDK%\Runtime-Development\Index-ScriptCore.deps.json"
call :RequireFile "%SDK%\Runtime-Development\nethost.dll"
call :RequireDir  "%SDK%\Runtime-Development\IndexAssets"
call :RequireFile "%SDK%\Runtime-Development\Licenses\Index\LICENSE.txt"
call :RequireFile "%SDK%\Runtime-Development\Licenses\Index\THIRD_PARTY_LICENSES.md"
call :RequireFile "%SDK%\Runtime-Development\Licenses\DotNet\ThirdPartyNotices.txt"

REM ---- Prebuilt editor-free Dist runtime bundled by CopyEngineSdk.bat. Must
REM      mirror the files that script copies (it skips silently when the Dist
REM      runtime is absent, so this is the only safety net).
call :RequireFile "%SDK%\Runtime-Dist\Index-Runtime.exe"
call :RequireFile "%SDK%\Runtime-Dist\Index-Engine.dll"
call :RequireFile "%SDK%\Runtime-Dist\GLFW.dll"
call :RequireFile "%SDK%\Runtime-Dist\Index-ScriptCore.dll"
call :RequireFile "%SDK%\Runtime-Dist\Index-ScriptCore.runtimeconfig.json"
call :RequireFile "%SDK%\Runtime-Dist\Index-ScriptCore.deps.json"
call :RequireFile "%SDK%\Runtime-Dist\nethost.dll"
call :RequireDir  "%SDK%\Runtime-Dist\IndexAssets"
call :RequireFile "%SDK%\Runtime-Dist\Licenses\Index\LICENSE.txt"
call :RequireFile "%SDK%\Runtime-Dist\Licenses\Index\THIRD_PARTY_LICENSES.md"
call :RequireFile "%SDK%\Runtime-Dist\Licenses\DotNet\LICENSE.txt"
call :RequireFile "%SDK%\Runtime-Dist\Licenses\DotNet\ThirdPartyNotices.txt"
call :RequireFile "%SDK%\Runtime-Dist\Licenses\Dawn-Third-Party\abseil-cpp\LICENSE"
call :RequireAbsent "%SDK%\Runtime-Dist\Tracy.dll"

REM ---- External vendored deps that the NativeScript CMakeLists references
call :RequireDir "%SDK%\External\spdlog\include"
call :RequireDir "%SDK%\External\glm"
call :RequireDir "%SDK%\External\entt\src"
call :RequireDir "%SDK%\External\stb"
call :RequireDir "%SDK%\External\magic_enum\include"
call :RequireDir "%SDK%\External\rapidxml"
call :RequireDir "%SDK%\External\glfw\include"
call :RequireDir "%SDK%\External\glad\include"
call :RequireDir "%SDK%\External\miniaudio"
call :RequireDir "%SDK%\External\box2d\include"
call :RequireDir "%SDK%\External\Index-Physics\include"
call :RequireDir "%SDK%\External\dotnet"
for /d %%D in ("%SDK%\External\*") do call :RequireAllowedExternal "%%~nxD"
for /r "%SDK%" %%G in (.git) do if exist "%%~fG" call :RejectGitMetadata "%%~fG"
for /r "%LAUNCHER%" %%P in (*.pdb) do if exist "%%~fP" call :RejectPrivateSymbol "%%~fP"

REM ---- Sanity: warn when the dev source tree is reachable above the launcher.
REM     ResolveEngineRoot in IndexProject.cpp walks exeDir\..\..\.. first so a
REM     dev build keeps using the source tree; EngineSDK is the fallback. The
REM     warning reminds the operator that a true standalone test must run on
REM     a machine with no source tree on disk anywhere reachable from the exe.
if exist "%LAUNCHER%\..\..\..\Index-Engine\src" goto :devTreeNote
goto :devTreeDone

:devTreeNote
echo [verify-standalone-zip] NOTE: dev source tree is reachable three dirs above the launcher.
echo [verify-standalone-zip]       Launcher will resolve the dev tree first; EngineSDK is
echo [verify-standalone-zip]       the fallback. A real standalone test must use a machine
echo [verify-standalone-zip]       without the repo anywhere reachable from the unzipped exe.

:devTreeDone
echo.
if "%FAIL%"=="0" goto :ok
goto :bad

:ok
echo [verify-standalone-zip] OK: launcher folder structure looks shippable.
popd >nul
endlocal
exit /b 0

:bad
echo [verify-standalone-zip] FAIL: %FAIL% structural issue(s). Build %CONFIG% and re-run.
popd >nul
endlocal
exit /b 1

:RequireFile
if exist "%~1" exit /b 0
echo   MISSING file:   %~1
set /a FAIL+=1
exit /b 0

:RequireDir
if exist "%~1\" exit /b 0
echo   MISSING dir:    %~1
set /a FAIL+=1
exit /b 0

:RequireAbsent
if not exist "%~1" exit /b 0
echo   UNEXPECTED file: %~1
set /a FAIL+=1
exit /b 0

:RequireAllowedExternal
for %%A in (spdlog glm entt stb magic_enum rapidxml glfw glad dawn miniaudio box2d Index-Physics dotnet) do if /I "%~1"=="%%A" exit /b 0
echo   UNEXPECTED dir:  %SDK%\External\%~1
set /a FAIL+=1
exit /b 0

:RejectGitMetadata
echo   UNEXPECTED Git metadata: %~1
set /a FAIL+=1
exit /b 0

:RejectPrivateSymbol
echo   UNEXPECTED private symbol: %~1
set /a FAIL+=1
exit /b 0
