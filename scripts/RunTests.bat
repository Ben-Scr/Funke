@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0RunTests.ps1" %*
exit /b %errorlevel%
