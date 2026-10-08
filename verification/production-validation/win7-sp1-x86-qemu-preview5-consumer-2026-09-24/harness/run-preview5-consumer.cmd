@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-consumer-preview5"
set "RESULTS=C:\soui-consumer-results-preview5"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%7za.exe" SHA256 > "%RESULTS%\7za-sha256.txt"
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
call :Run bridge
call :Run reload
call :Run recreate
call :Run multiwindow
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI preview5 consumer export finished with exit code %EXPORT_EXIT%
pause
exit /b
:Run
set "MODE=%~1"
cd /d "%TARGET%"
SouiAcceptance.exe --mode=%MODE% --cycles=3 --warmup=0 --settle-ms=100 --timeout-ms=60000 --report="%RESULTS%\%MODE%-events.ndjson" 1> "%RESULTS%\%MODE%-stdout.log" 2> "%RESULTS%\%MODE%-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\%MODE%-exitcode.txt"
echo SOUI %MODE% finished with exit code %APP_EXIT%
exit /b