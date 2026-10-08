@echo off
setlocal
set "ROOT=%~dp0"
set "RESULTS=C:\soui-title-results"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%soui_desktop_unicode_tests.exe" SHA256 > "%RESULTS%\test-exe-sha256.txt"
"%ROOT%soui_desktop_unicode_tests.exe" --window-thread-timeout 1> "%RESULTS%\title-stdout.log" 2> "%RESULTS%\title-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\title-exitcode.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI title timeout export finished with exit code %EXPORT_EXIT%
pause
exit /b
