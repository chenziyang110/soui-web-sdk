@echo off
setlocal
set "ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%run-apps.ps1" -Transfer "%ROOT%."
set "RUN_EXIT=%ERRORLEVEL%"
echo Preview.15 guest harness exit code %RUN_EXIT%
exit /b %RUN_EXIT%
