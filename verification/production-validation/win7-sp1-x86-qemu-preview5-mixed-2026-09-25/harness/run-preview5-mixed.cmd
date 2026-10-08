@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-preview5-mixed"
set "RESULTS=C:\soui-preview5-mixed-results"
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
echo %DATE% %TIME% > "%RESULTS%\mixed-start.txt"
cd /d "%TARGET%"
SouiAcceptance.exe --mode=mixed --duration-ms=60000 --idle-ms=10000 --settle-ms=20 --timeout-ms=60000 --report="%RESULTS%\mixed-events.ndjson" 1> "%RESULTS%\mixed-stdout.log" 2> "%RESULTS%\mixed-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\mixed-exitcode.txt"
echo %DATE% %TIME% > "%RESULTS%\mixed-finish.txt"
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI preview5 Win7 mixed export finished with exit code %EXPORT_EXIT%
pause
exit /b
