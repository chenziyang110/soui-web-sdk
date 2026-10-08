@echo off
setlocal
set "ROOT=%~dp0"
set "RESULTS=C:\soui-desktop-results"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%soui_desktop_unicode_tests.exe" SHA256 > "%RESULTS%\test-exe-sha256.txt"
"%ROOT%soui_desktop_unicode_tests.exe" --desktop-roundtrip 1> "%RESULTS%\desktop-stdout.log" 2> "%RESULTS%\desktop-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\desktop-exitcode.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI desktop export finished with exit code %EXPORT_EXIT%
pause
exit /b
