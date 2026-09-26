@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-demo-ui-preview8"
set "RESULTS=C:\soui-demo-ui-results-preview8"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%7za.exe" SHA256 > "%RESULTS%\7za-sha256.txt"
certutil -hashfile "%ROOT%SouiWorkbench.zip" SHA256 > "%RESULTS%\zip-sha256.txt"
"%ROOT%7za.exe" t "%ROOT%SouiWorkbench.zip" > "%RESULTS%\zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%" "%ROOT%SouiWorkbench.zip" > "%RESULTS%\extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%verify-payload.ps1" -Target "%TARGET%" -Manifest "%ROOT%payload-sha256.tsv" > "%RESULTS%\payload-verify.log" 2>&1
set "PS_EXIT=%ERRORLEVEL%"
echo %PS_EXIT% > "%RESULTS%\raw-powershell-exitcode.txt"
set "APP_EXIT=0"
if not "%PS_EXIT%"=="0" set "APP_EXIT=1"
findstr /C:"Payload files=475,hashes=matched" "%RESULTS%\payload-verify.log" > NUL
if errorlevel 1 set "APP_EXIT=1"
findstr /C:"Legal files=5,hashes=matched" "%RESULTS%\payload-verify.log" > NUL
if errorlevel 1 set "APP_EXIT=1"
echo %APP_EXIT% > "%RESULTS%\payload-verify-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
certutil -hashfile "%TARGET%\SouiWorkbench.exe" SHA256 > "%RESULTS%\app-exe-sha256.txt"
certutil -hashfile "%TARGET%\node.dll" SHA256 > "%RESULTS%\engine-sha256.txt"
certutil -hashfile "%TARGET%\soui.dll" SHA256 > "%RESULTS%\soui-sha256.txt"
set "SOUI_INITIAL_PAGE=data"
cd /d "%TARGET%"
start "" /wait "%TARGET%\SouiWorkbench.exe" --arch=x86 --renderer=cpu --react --width=760 --height=520
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\app-exitcode.txt"
if exist "%TARGET%\webview-smoke.log" copy /Y "%TARGET%\webview-smoke.log" "%RESULTS%\app-host.log" > NUL
echo Workbench UI finished with exit code %APP_EXIT%
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI Demo UI export finished with exit code %EXPORT_EXIT%
pause
