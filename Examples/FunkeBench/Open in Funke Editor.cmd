@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Start.ps1" -Editor
if errorlevel 1 pause
