@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-preview5-timeout-reload"
set "RESULTS=C:\soui-preview5-timeout-reload-results"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%SouiAcceptance.zip" SHA256 > "%RESULTS%\runtime-zip-sha256.txt"
"%ROOT%7za.exe" t "%ROOT%SouiAcceptance.zip" > "%RESULTS%\runtime-zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\runtime-zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%" "%ROOT%SouiAcceptance.zip" > "%RESULTS%\runtime-extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\runtime-extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
certutil -hashfile "%TARGET%\SouiAcceptance.exe" SHA256 > "%RESULTS%\acceptance-exe-sha256.txt"
certutil -hashfile "%TARGET%\node.dll" SHA256 > "%RESULTS%\engine-sha256.txt"
certutil -hashfile "%TARGET%\soui.dll" SHA256 > "%RESULTS%\sdk-sha256.txt"
echo %DATE% %TIME% > "%RESULTS%\reload-start.txt"
cd /d "%TARGET%"
SouiAcceptance.exe --mode=reload --cycles=100 --warmup=3 --settle-ms=1000 --timeout-ms=60000 --report="%RESULTS%\reload-events.ndjson" 1> "%RESULTS%\reload-stdout.log" 2> "%RESULTS%\reload-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\reload-exitcode.txt"
echo %DATE% %TIME% > "%RESULTS%\reload-finish.txt"
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI preview5 Win7 reload export finished with exit code %EXPORT_EXIT%
pause
exit /b
