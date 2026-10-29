@echo off
REM Developer staging retains the optional native SDK; installers default to binary payloads.
setlocal
python "%~dp0CopyEngineSdk.py" %*
exit /b %errorlevel%
