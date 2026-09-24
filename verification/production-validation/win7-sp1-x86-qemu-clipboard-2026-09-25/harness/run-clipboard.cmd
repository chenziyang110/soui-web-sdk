@echo off
setlocal
set "ROOT=%~dp0"
set "RESULTS=C:\soui-clipboard-results"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%soui_desktop_unicode_tests.exe" SHA256 > "%RESULTS%\test-exe-sha256.txt"
"%ROOT%soui_desktop_unicode_tests.exe" --clipboard-roundtrip 1> "%RESULTS%\clipboard-stdout.log" 2> "%RESULTS%\clipboard-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\clipboard-exitcode.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI clipboard export finished with exit code %EXPORT_EXIT%
pause
exit /b
