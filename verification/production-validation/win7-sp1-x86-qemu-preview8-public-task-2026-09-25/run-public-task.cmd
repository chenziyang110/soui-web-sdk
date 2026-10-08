@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-task-preview8"
set "RESULTS=C:\soui-task-results-preview8"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%7za.exe" SHA256 > "%RESULTS%\7za-sha256.txt"
certutil -hashfile "%ROOT%SouiAcceptance.zip" SHA256 > "%RESULTS%\zip-sha256.txt"
"%ROOT%7za.exe" t "%ROOT%SouiAcceptance.zip" > "%RESULTS%\zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%" "%ROOT%SouiAcceptance.zip" > "%RESULTS%\extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%verify-payload.ps1" -Target "%TARGET%" -Manifest "%ROOT%payload-sha256.tsv" > "%RESULTS%\payload-verify.log" 2>&1
set "PS_EXIT=%ERRORLEVEL%"
echo %PS_EXIT% > "%RESULTS%\raw-powershell-exitcode.txt"
set "APP_EXIT=0"
if not "%PS_EXIT%"=="0" set "APP_EXIT=1"
findstr /C:"Payload files=76,hashes=matched" "%RESULTS%\payload-verify.log" > NUL
if errorlevel 1 set "APP_EXIT=1"
echo %APP_EXIT% > "%RESULTS%\payload-verify-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
certutil -hashfile "%TARGET%\SouiAcceptance.exe" SHA256 > "%RESULTS%\app-exe-sha256.txt"
certutil -hashfile "%TARGET%\node.dll" SHA256 > "%RESULTS%\engine-sha256.txt"
certutil -hashfile "%TARGET%\soui.dll" SHA256 > "%RESULTS%\soui-sha256.txt"
cd /d "%TARGET%"
SouiAcceptance.exe --mode=bridge --cycles=3 --warmup=0 --settle-ms=100 --timeout-ms=60000 --report="%RESULTS%\bridge-events.ndjson" 1> "%RESULTS%\bridge-stdout.log" 2> "%RESULTS%\bridge-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\bridge-exitcode.txt"
echo Public SDK task bridge finished with exit code %APP_EXIT%
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI public task export finished with exit code %EXPORT_EXIT%
pause
