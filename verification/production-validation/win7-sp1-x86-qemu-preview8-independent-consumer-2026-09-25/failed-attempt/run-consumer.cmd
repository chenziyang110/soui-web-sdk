@echo off
setlocal
set "ROOT=%~dp0"
set "TARGET=C:\soui-consumer-validation-8"
set "RESULTS=C:\soui-consumer-results-8"
if not exist "%TARGET%" mkdir "%TARGET%"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
certutil -hashfile "%ROOT%7za.exe" SHA256 > "%RESULTS%\7za-sha256.txt"
certutil -hashfile "%ROOT%Preview8Win7.zip" SHA256 > "%RESULTS%\zip-sha256.txt"
"%ROOT%7za.exe" t "%ROOT%Preview8Win7.zip" > "%RESULTS%\zip-test.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\zip-test-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
"%ROOT%7za.exe" x -y "-o%TARGET%\Preview8Win7" "%ROOT%Preview8Win7.zip" > "%RESULTS%\extract.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\extract-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%verify-consumer.ps1" -Target "%TARGET%\Preview8Win7" -Manifest "%ROOT%payload-sha256.tsv" > "%RESULTS%\payload-verify.log" 2>&1
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\payload-verify-exitcode.txt"
if not "%APP_EXIT%"=="0" goto Export
certutil -hashfile "%TARGET%\Preview8Win7\Preview8Win7.exe" SHA256 > "%RESULTS%\app-exe-sha256.txt"
certutil -hashfile "%TARGET%\Preview8Win7\node.dll" SHA256 > "%RESULTS%\engine-sha256.txt"
certutil -hashfile "%TARGET%\Preview8Win7\soui.dll" SHA256 > "%RESULTS%\soui-sha256.txt"
start "" /wait "%TARGET%\Preview8Win7\Preview8Win7.exe" --smoke-test
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\app-exitcode.txt"
if exist "%TARGET%\Preview8Win7\smoke.log" copy /Y "%TARGET%\Preview8Win7\smoke.log" "%RESULTS%\app-smoke.log" > NUL
echo Independent SDK consumer finished with exit code %APP_EXIT%
:Export
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI consumer result export finished with exit code %EXPORT_EXIT%
pause
