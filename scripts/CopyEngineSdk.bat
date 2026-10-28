@echo off
REM Materialize a self-contained EngineSDK folder under a consumer's
REM target directory. Invoked from the Funke-Launcher postbuild via the
REM CopyEngineSdk variable in premake5.lua.
REM
REM Args:
REM   %1 = SDK target dir, e.g. ...\bin\Debug-windows-x86_64\Funke-Launcher\EngineSDK
REM   %2 = engine repo root, the ROOT_DIR value from premake
REM   %3 = config-specific bin dir, e.g. ...\bin\Debug-windows-x86_64
REM
REM robocopy exit codes 0..7 are success; 8+ are failures. Each step uses
REM `if errorlevel 8 goto :robofail` rather than a multi-line if-block,
REM because cmd.exe paren-balances aggressively across compound statements
REM and any literal parens in echo arguments inside an if-block confuse it.
REM /NJH /NJS /NDL /NP /NS /NC suppress robocopy's banner/per-file noise.

setlocal EnableDelayedExpansion

set "SDK=%~1"
set "ROOT=%~2"
set "BIN=%~3"

if "%SDK%"=="" goto :missingSdk
if "%ROOT%"=="" goto :missingRoot
if "%BIN%"=="" goto :missingBin

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0PruneRetiredSdkDependencies.ps1" -SdkDirectory "%SDK%"
if errorlevel 1 goto :fail

set "ROBOFLAGS=/NJH /NJS /NDL /NP /NS /NC /XD .git /XF .git"

REM --- A missing source folder would only surface as robocopy errorlevel 16, and
REM     nothing after it (Licenses included) would be staged.
call :RequireBuildOutput Funke-Editor\Funke-Editor.exe
if errorlevel 1 goto :fail
call :RequireBuildOutput Funke-Runtime\Funke-Runtime.exe
if errorlevel 1 goto :fail
call :RequireBuildOutput Funke-ScriptCore\Funke-ScriptCore.dll
if errorlevel 1 goto :fail
call :RequireBuildOutput Funke-PackageTool\Funke-PackageTool.exe
if errorlevel 1 goto :fail

REM --- Flat host applications used by LauncherLayer.
robocopy "%BIN%\Funke-Editor" "%SDK%\.." Funke-Editor.exe %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%BIN%\Funke-Runtime" "%SDK%\.." Funke-Runtime.exe %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
REM --- `funke` command-line tool; optional so a Launcher built without it still packages.
if exist "%BIN%\Funke-CLI\funke.exe" robocopy "%BIN%\Funke-CLI" "%SDK%\.." funke.exe %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%BIN%\Funke-ScriptCore" "%SDK%\.." Funke-ScriptCore.dll Funke-ScriptCore.xml Funke-ScriptCore.runtimeconfig.json Funke-ScriptCore.deps.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
REM --- Editor's in-process hot-reload compiler; optional, the editor falls back to dotnet build without it.
if exist "%BIN%\Funke-ScriptCompiler\Funke-ScriptCompiler.dll" robocopy "%BIN%\Funke-ScriptCompiler" "%SDK%\..\ScriptCompiler" Funke-ScriptCompiler.dll Funke-ScriptCompiler.deps.json Funke-ScriptCompiler.runtimeconfig.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%ROOT%\build" "%SDK%\.." BuildMetadata.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail

REM --- Funke-Engine headers and sources. CMakeLists.txt adds this as an
REM     include path; .cpp files are harmless dead weight we accept here.
robocopy "%ROOT%\Funke-Engine\src" "%SDK%\Funke-Engine\src" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
REM --- Launcher project templates (ProjectTemplates resolves <engine root>\templates).
robocopy "%ROOT%\templates" "%SDK%\templates" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
REM --- Flat package tool used by Launcher and Editor package operations.
robocopy "%BIN%\Funke-PackageTool" "%SDK%\.." Funke-PackageTool.exe Funke-PackageTool.dll Funke-PackageTool.runtimeconfig.json Funke-PackageTool.deps.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail


REM --- Funke-ScriptCore project and runtime artifacts.
robocopy "%ROOT%\Funke-ScriptCore" "%SDK%\Funke-ScriptCore" Funke-ScriptCore.csproj %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%BIN%\Funke-ScriptCore" "%SDK%\Funke-ScriptCore" Funke-ScriptCore.dll Funke-ScriptCore.xml Funke-ScriptCore.runtimeconfig.json Funke-ScriptCore.deps.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
REM --- Project-local package build tooling.
robocopy "%ROOT%\vendor\bin" "%SDK%" premake5.exe %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%ROOT%\premake" "%SDK%\premake" project-packages.lua package-loader.lua fix_csharp_platforms.lua architecture.lua compat.lua %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%ROOT%\scripts" "%SDK%\scripts" BuildProjectPackages.ps1 %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%ROOT%\scripts\packages" "%SDK%\scripts\packages" NewPackage.py RemovePackage.py BumpPackageVersion.py %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail



REM --- External vendored deps. Only the directories CMakeLists references
REM     get shipped. Each entry mirrors a leaf directory so we do not pull
REM     in build outputs, .git folders, or test trees from the submodules.
call :CopyExternalDir spdlog\include
if errorlevel 1 goto :robofail
call :CopyExternalDir glm
if errorlevel 1 goto :robofail
call :CopyExternalDir entt\src
if errorlevel 1 goto :robofail
call :CopyExternalDir stb
if errorlevel 1 goto :robofail
call :CopyExternalDir magic_enum\include
if errorlevel 1 goto :robofail
call :CopyExternalDir rapidxml
if errorlevel 1 goto :robofail
call :CopyExternalDir SDL\include
if errorlevel 1 goto :robofail
call :CopyExternalDir dawn\include
if errorlevel 1 goto :robofail
call :CopyExternalDir dawn\build\gen\include
if errorlevel 1 goto :robofail
call :CopyExternalDir miniaudio
if errorlevel 1 goto :robofail
call :CopyExternalDir box2d\include
if errorlevel 1 goto :robofail
call :CopyExternalDir dotnet
if errorlevel 1 goto :robofail

REM --- Active-config engine import library and DLL. Native packages link the
REM     library and hash the exact DLL after linking to emit their ABI sidecar.
REM     Preserve the repo layout expected by project-packages.lua.
for %%I in ("%BIN%") do set "CFGPLAT=%%~nxI"
robocopy "%BIN%\Funke-Engine" "%SDK%\bin\%CFGPLAT%\Funke-Engine" Funke-Engine.lib Funke-Engine.dll %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
for /f "tokens=1* delims=-" %%A in ("!CFGPLAT!") do set "DISTPLAT=Dist-%%B"
if not exist "%ROOT%\bin\!DISTPLAT!\Funke-Engine\Funke-Engine.lib" goto :skipDistEngineSdk
if not exist "%ROOT%\bin\!DISTPLAT!\Funke-Engine\Funke-Engine.dll" goto :skipDistEngineSdk
robocopy "%ROOT%\bin\!DISTPLAT!\Funke-Engine" "%SDK%\bin\!DISTPLAT!\Funke-Engine" Funke-Engine.lib Funke-Engine.dll %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
:skipDistEngineSdk

REM --- Self-contained redistribution notices.
call :CopyRequiredLicense "%ROOT%" "Funke" "LICENSE.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%" "Funke" "THIRD_PARTY_LICENSES.md"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\box2d" "Box2D" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\JoltPhysics" "JoltPhysics" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\entt" "EnTT" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\glm" "GLM" "copying.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\SDL" "SDL3" "LICENSE.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\imgui" "Dear-ImGui" "LICENSE.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\ImGuizmo" "ImGuizmo" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\magic_enum" "magic_enum" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\miniaudio" "miniaudio" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\spdlog" "spdlog" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\stb" "stb" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\tracy" "Tracy" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\dawn" "Dawn" "LICENSE"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\concurrentqueue" "concurrentqueue" "LICENSE.md"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\External\rapidxml" "RapidXML" "LICENSE.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "%ROOT%\Funke-Runtime\FunkeAssets\Fonts" "Fonts" "LICENSES.md"
if errorlevel 1 goto :robofail

if not exist "%ROOT%\External\dawn\third_party" goto :missingDawnLicenses
robocopy "%ROOT%\External\dawn\third_party" "%SDK%\Licenses\Dawn-Third-Party" LICENSE* NOTICE* COPYING* ThirdPartyNotices.txt /S %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail

set "DOTNET_EXE="
for %%I in (dotnet.exe) do set "DOTNET_EXE=%%~$PATH:I"
if not defined DOTNET_EXE goto :missingDotNetLicenses
for %%I in ("!DOTNET_EXE!") do set "DOTNET_LICENSE_DIR=%%~dpI"
if "!DOTNET_LICENSE_DIR:~-1!"=="\" set "DOTNET_LICENSE_DIR=!DOTNET_LICENSE_DIR:~0,-1!"
call :CopyRequiredLicense "!DOTNET_LICENSE_DIR!" "DotNet" "LICENSE.txt"
if errorlevel 1 goto :robofail
call :CopyRequiredLicense "!DOTNET_LICENSE_DIR!" "DotNet" "ThirdPartyNotices.txt"
if errorlevel 1 goto :robofail

REM --- Active-config runtime for repo-less Development exports.
set "DEVRT=%BIN%\Funke-Runtime"
if not exist "%DEVRT%\Funke-Runtime.exe" goto :skipRuntimeDevelopment
if exist "%SDK%\Runtime-Development" rmdir /s /q "%SDK%\Runtime-Development"
robocopy "%DEVRT%" "%SDK%\Runtime-Development" Funke-Runtime.exe Funke-Engine.dll SDL3.dll nethost.dll %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%BIN%\Funke-ScriptCore" "%SDK%\Runtime-Development" Funke-ScriptCore.dll Funke-ScriptCore.runtimeconfig.json Funke-ScriptCore.deps.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
if exist "%DEVRT%\Tracy.dll" robocopy "%DEVRT%" "%SDK%\Runtime-Development" Tracy.dll %ROBOFLAGS% >nul
robocopy "%DEVRT%\FunkeAssets" "%SDK%\Runtime-Development\FunkeAssets" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%SDK%\Licenses" "%SDK%\Runtime-Development\Licenses" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
goto :afterRuntimeDevelopment
:skipRuntimeDevelopment
echo CopyEngineSdk: active-config runtime missing at %BIN%\Funke-Runtime; Runtime-Development not bundled 1>&2
:afterRuntimeDevelopment

REM --- Prebuilt editor-free Dist runtime, bundled under Runtime-Dist\ so a
REM     shipped repo-less Launcher/editor carries a fully runnable runtime and
REM     a future copy-based Release export has everything it needs. The Dist
REM     runtime always comes from bin\Dist-<plat>\Funke-Runtime regardless of
REM     the Launcher's own config, so derive Dist-<plat> from CFGPLAT set above.
REM     Copy the distribution artifacts by name plus the FunkeAssets tree --
REM     NOT a /MIR of the whole folder -- so stray files left by running a game
REM     from that dir (imgui.ini, a project's Assets\, Packages\,
REM     funke-project.json) never leak into the bundle. The dest is wiped first
REM     for a deterministic, stale-free bundle. Skipped with a stderr note if
REM     the Dist runtime is not built yet (mirrors the :CopyExternalDir
REM     missing-source behavior), so a Debug-only or partial build still
REM     succeeds. Build Funke-Runtime in the Dist config BEFORE this Launcher
REM     postbuild for the bundle to populate.
set "DISTRT=%ROOT%\bin\!DISTPLAT!\Funke-Runtime"
if not exist "%DISTRT%\Funke-Runtime.exe" goto :skipRuntimeDist
if exist "%SDK%\Runtime-Dist" rmdir /s /q "%SDK%\Runtime-Dist"
robocopy "%DISTRT%" "%SDK%\Runtime-Dist" Funke-Runtime.exe Funke-Engine.dll SDL3.dll nethost.dll %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%ROOT%\bin\!DISTPLAT!\Funke-ScriptCore" "%SDK%\Runtime-Dist" Funke-ScriptCore.dll Funke-ScriptCore.runtimeconfig.json Funke-ScriptCore.deps.json %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%DISTRT%\FunkeAssets" "%SDK%\Runtime-Dist\FunkeAssets" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
robocopy "%SDK%\Licenses" "%SDK%\Runtime-Dist\Licenses" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :robofail
goto :afterRuntimeDist
:skipRuntimeDist
echo CopyEngineSdk: Dist runtime missing at bin\!DISTPLAT!\Funke-Runtime; Runtime-Dist not bundled 1>&2
:afterRuntimeDist

endlocal
exit /b 0

:CopyExternalDir
REM %1 = sub-path inside External\ to mirror into the SDK. Missing submodules
REM are skipped silently so a partial checkout still builds; the
REM verify-standalone-zip script catches gaps that matter.
set "REL=%~1"
if not exist "%ROOT%\External\%REL%" exit /b 0
robocopy "%ROOT%\External\%REL%" "%SDK%\External\%REL%" /MIR %ROBOFLAGS% >nul
if errorlevel 8 goto :externalFail
exit /b 0

:externalFail
echo CopyEngineSdk: robocopy failed mirroring External\%REL% with errorlevel %errorlevel% 1>&2
exit /b 1

:RequireBuildOutput
if exist "%BIN%\%~1" exit /b 0
echo CopyEngineSdk: required build output missing at %BIN%\%~1; build that project before Funke-Launcher 1>&2
exit /b 1

:CopyRequiredLicense
if not exist "%~1\%~3" echo CopyEngineSdk: required license file missing at %~1\%~3 1>&2
if not exist "%~1\%~3" exit /b 1
robocopy "%~1" "%SDK%\Licenses\%~2" "%~3" %ROBOFLAGS% >nul
if errorlevel 8 exit /b 1
exit /b 0

:missingDawnLicenses
echo CopyEngineSdk: Dawn third-party licenses missing at %ROOT%\External\dawn\third_party 1>&2
endlocal
exit /b 1

:missingDotNetLicenses
echo CopyEngineSdk: dotnet.exe was not found on PATH; .NET redistribution notices cannot be staged 1>&2
endlocal
exit /b 1

:robofail
echo CopyEngineSdk: robocopy failed with errorlevel %errorlevel% 1>&2
endlocal
exit /b 1

:fail
endlocal
exit /b 1

:missingSdk
echo CopyEngineSdk: missing target SDK dir argument 1>&2
endlocal
exit /b 1

:missingRoot
echo CopyEngineSdk: missing repo root argument 1>&2
endlocal
exit /b 1

:missingBin
echo CopyEngineSdk: missing bin dir argument 1>&2
endlocal
exit /b 1
