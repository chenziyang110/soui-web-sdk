@echo off
setlocal
set "ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%run-preflight.ps1" -Transfer "%ROOT%."
set "RUN_EXIT=%ERRORLEVEL%"
echo Current V02 Win7 preflight exit code %RUN_EXIT%
exit /b %RUN_EXIT%
