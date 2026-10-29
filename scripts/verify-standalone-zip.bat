@echo off
REM Verify the manifest and payload; no source checkout is required by the bundle.
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "BUNDLE=%~2"
if "%BUNDLE%"=="" set "BUNDLE=%~dp0..\bin\%CONFIG%-windows-x86_64\Funke-Launcher"
python "%~dp0sdk_manifest.py" "%BUNDLE%" %3
exit /b %errorlevel%
