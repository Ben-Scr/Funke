@echo off
REM ============================================================================
REM SetupDawn.bat - Fetch and build the Dawn WebGPU implementation as a
REM                monolithic static library at External\dawn\, then
REM                regenerate Funke.sln so the engine is build-ready.
REM
REM Run once after cloning the engine. Subsequent engine builds link against
REM the pre-built webgpu_dawn.lib.
REM
REM Prerequisites:
REM   * Git (with HTTPS access to dawn.googlesource.com)
REM   * Visual Studio with the MSVC v143 C++ workload
REM   * Python 3 (Dawn's CMake invokes Python for code generation)
REM   * CMake -- either at vendor\cmake\bin\cmake.exe (vendored portable;
REM     run scripts\dawn\BootstrapCMake.bat to fetch it) or on PATH.
REM ============================================================================

setlocal EnableDelayedExpansion

pushd "%~dp0\..\.."

set "DAWN_DIR=External\dawn"
set "DAWN_BUILD_DIR=%DAWN_DIR%\build"
set "VENDOR_CMAKE_EXE=vendor\cmake\bin\cmake.exe"

echo ============================================
echo   Dawn WebGPU Setup
echo ============================================
echo Target: %DAWN_DIR%
echo.

REM --- Tool checks ------------------------------------------------------------
where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] git is not on PATH.
    goto :error
)

REM CMake resolution: prefer vendored portable, then PATH.
set "CMAKE_EXE="
if exist "%VENDOR_CMAKE_EXE%" set "CMAKE_EXE=%VENDOR_CMAKE_EXE%"
if not defined CMAKE_EXE (
    where cmake >nul 2>nul
    if not errorlevel 1 set "CMAKE_EXE=cmake"
)
if not defined CMAKE_EXE (
    echo [ERROR] CMake not found at %VENDOR_CMAKE_EXE% nor on PATH.
    echo         Run scripts\dawn\BootstrapCMake.bat to vendor a portable copy,
    echo         or install CMake yourself.
    goto :error
)
echo [info] Using cmake: !CMAKE_EXE!

where python >nul 2>nul
if not errorlevel 1 goto :python_ok
where py >nul 2>nul
if not errorlevel 1 goto :python_ok
echo [ERROR] python / py not on PATH. Dawn's codegen needs Python 3.
goto :error
:python_ok

REM --- Resolve pinned revision ------------------------------------------------
REM DAWN_REVISION overrides the versioned commit pin.
if not defined DAWN_REVISION (
    if exist "scripts\dawn\dawn-revision.txt" (
        for /f "usebackq tokens=1 delims= " %%R in ("scripts\dawn\dawn-revision.txt") do (
            if not defined DAWN_REVISION set "DAWN_REVISION=%%R"
        )
    )
)

REM --- Sources ----------------------------------------------------------------
echo [1/5] Verifying pinned Dawn sources and dependencies...
where python >nul 2>nul
if not errorlevel 1 (
    python "scripts\dawn\BootstrapDawn.py" --minimal --revision "!DAWN_REVISION!"
) else (
    py "scripts\dawn\BootstrapDawn.py" --minimal --revision "!DAWN_REVISION!"
)
if errorlevel 1 goto :error

REM --- Configure --------------------------------------------------------------
:configure
echo [3/5] Configuring Dawn via CMake...
REM Funke only feeds WGSL to Dawn, so the SPIR-V input reader and SPIRV-Tools
REM validation are dead weight (~1.6 MB of SPIRV-Tools in Funke-Engine.dll).
"!CMAKE_EXE!" -S "%DAWN_DIR%" -B "%DAWN_BUILD_DIR%" -A x64 -T v143 -DDAWN_FETCH_DEPENDENCIES=OFF -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC -DDAWN_ENABLE_INSTALL=OFF -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_USE_GLFW=OFF -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF -DTINT_BUILD_SPV_READER=OFF -DDAWN_ENABLE_SPIRV_VALIDATION=OFF
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    goto :error
)

REM --- Build ------------------------------------------------------------------
REM DAWN_BUILD_CONFIGS (env) selects which configs to build; defaults to both
REM for local dev and CI because the Windows job validates both configurations.
if not defined DAWN_BUILD_CONFIGS set "DAWN_BUILD_CONFIGS=Debug Release"
echo [4/5] Building webgpu_dawn ^(configs: !DAWN_BUILD_CONFIGS!^)...
for %%C in (!DAWN_BUILD_CONFIGS!) do (
    echo     - building %%C...
    "!CMAKE_EXE!" --build "%DAWN_BUILD_DIR%" --config %%C --target webgpu_dawn
    if errorlevel 1 (
        echo [ERROR] %%C build failed.
        goto :error
    )
)

REM --- Regenerate Funke.sln ---------------------------------------------------
where python >nul 2>nul
if not errorlevel 1 (
    python "scripts\dawn\ValidateDawnCache.py" --write
) else (
    py "scripts\dawn\ValidateDawnCache.py" --write
)
if errorlevel 1 goto :error

REM CI sets DAWN_SKIP_PREMAKE=1: the workflow regenerates project files itself
REM (scripts/Setup.py) with the correct module-profile / options right after
REM this, so a redundant default-option premake run here is skipped.
if defined DAWN_SKIP_PREMAKE (
    echo [5/5] DAWN_SKIP_PREMAKE set - skipping premake regen.
    goto :done
)
echo [5/5] Regenerating Funke.sln via Premake...
if not exist "vendor\bin\premake5.exe" goto :no_premake
"vendor\bin\premake5.exe" vs2022
if errorlevel 1 echo [WARN] Premake regen returned non-zero. Run manually: vendor\bin\premake5.exe vs2022
goto :done
:no_premake
echo [WARN] vendor\bin\premake5.exe not found. Run scripts\Setup.bat first.

:done
echo.
echo ============================================
echo   Dawn build complete.
echo ============================================
echo Headers : %DAWN_DIR%\include
echo Headers : %DAWN_BUILD_DIR%\gen\include
echo Lib(D)  : %DAWN_BUILD_DIR%\src\dawn\native\Debug\webgpu_dawn.lib
echo Lib(R)  : %DAWN_BUILD_DIR%\src\dawn\native\Release\webgpu_dawn.lib
echo.
echo Open Funke.sln in Visual Studio and build.
echo.

popd
exit /b 0

:error
popd
echo.
echo Setup aborted.
exit /b 1
