@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildNativeSdk.ps1" %*
exit /b %ERRORLEVEL%
