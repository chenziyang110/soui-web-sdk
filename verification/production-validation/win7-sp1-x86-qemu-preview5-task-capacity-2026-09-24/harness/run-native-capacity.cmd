@echo off
setlocal
set "ROOT=%~dp0"
set "RESULTS=C:\soui-task-capacity-results-preview5"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%soui_task_store_capacity_tests.exe" SHA256 > "%RESULTS%\test-exe-sha256.txt"
cd /d "%ROOT%"
soui_task_store_capacity_tests.exe 1> "%RESULTS%\test-stdout.log" 2> "%RESULTS%\test-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\test-exitcode.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI task capacity test exit code %APP_EXIT%; export exit code %EXPORT_EXIT%
pause
exit /b
