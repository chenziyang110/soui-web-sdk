@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-app-validation-5"
set "RESULTS=C:\soui-app-results-5"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%7za.exe" SHA256 > "%RESULTS%\7za-sha256.txt"
certutil -hashfile "%ROOT%SouiStarter.zip" SHA256 > "%RESULTS%\starter-zip-sha256.txt"
certutil -hashfile "%ROOT%SouiWorkbench.zip" SHA256 > "%RESULTS%\workbench-zip-sha256.txt"
"%ROOT%7za.exe" t "%ROOT%SouiStarter.zip" > "%RESULTS%\starter-zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\starter-zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" t "%ROOT%SouiWorkbench.zip" > "%RESULTS%\workbench-zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\workbench-zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%\SouiStarter" "%ROOT%SouiStarter.zip" > "%RESULTS%\starter-extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\starter-extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%\SouiWorkbench" "%ROOT%SouiWorkbench.zip" > "%RESULTS%\workbench-extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\workbench-extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%verify-apps.ps1" > "%RESULTS%\verify-apps.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\verify-apps-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
certutil -hashfile "%TARGET%\SouiStarter\SouiStarter.exe" SHA256 > "%RESULTS%\starter-exe-sha256.txt"
certutil -hashfile "%TARGET%\SouiWorkbench\SouiWorkbench.exe" SHA256 > "%RESULTS%\workbench-exe-sha256.txt"
certutil -hashfile "%TARGET%\SouiWorkbench\node.dll" SHA256 > "%RESULTS%\engine-sha256.txt"
certutil -hashfile "%TARGET%\SouiWorkbench\soui.dll" SHA256 > "%RESULTS%\sdk-sha256.txt"
start "" /wait "%TARGET%\SouiStarter\SouiStarter.exe" --smoke-test
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\starter-exitcode.txt"
if exist "%TARGET%\SouiStarter\smoke.log" copy /Y "%TARGET%\SouiStarter\smoke.log" "%RESULTS%\starter-smoke.log" > NUL
echo SOUI Starter finished with exit code %APP_EXIT%
start "" /wait "%TARGET%\SouiWorkbench\SouiWorkbench.exe" --webview-smoke-test --arch=x86 --renderer=cpu
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\workbench-default-exitcode.txt"
if exist "%TARGET%\SouiWorkbench\webview-smoke.log" copy /Y "%TARGET%\SouiWorkbench\webview-smoke.log" "%RESULTS%\workbench-default-smoke.log" > NUL
echo SOUI Workbench default finished with exit code %APP_EXIT%
start "" /wait "%TARGET%\SouiWorkbench\SouiWorkbench.exe" --webview-smoke-test --arch=x86 --renderer=cpu --react
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\workbench-react-exitcode.txt"
if exist "%TARGET%\SouiWorkbench\webview-smoke.log" copy /Y "%TARGET%\SouiWorkbench\webview-smoke.log" "%RESULTS%\workbench-react-smoke.log" > NUL
echo SOUI Workbench React finished with exit code %APP_EXIT%
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI app result export finished with exit code %EXPORT_EXIT%
pause