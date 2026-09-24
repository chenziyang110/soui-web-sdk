@echo off
setlocal
set "ROOT=%~dp0"
set "RESULTS=C:\soui-validation-results"
if not exist "%RESULTS%" mkdir "%RESULTS%"
ver > "%RESULTS%\os-version.txt"
wmic os get Caption,Version,OSArchitecture,ServicePackMajorVersion /value >> "%RESULTS%\os-version.txt" 2>&1
call :Run bridge
call :Run reload
call :Run recreate
call :Run multiwindow
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%export-serial.ps1" -Results "%RESULTS%"
set "EXPORT_EXIT=%ERRORLEVEL%"
echo SOUI serial export finished with exit code %EXPORT_EXIT%
pause
exit /b

:Run
set "MODE=%~1"
cd /d "%ROOT%runtime"
SouiAcceptance.exe --mode=%MODE% --cycles=3 --warmup=0 --settle-ms=100 --timeout-ms=60000 --report="%RESULTS%\%MODE%-events.ndjson" 1> "%RESULTS%\%MODE%-stdout.log" 2> "%RESULTS%\%MODE%-stderr.log"
set "APP_EXIT=%ERRORLEVEL%"
echo %APP_EXIT% > "%RESULTS%\%MODE%-exitcode.txt"
echo SOUI %MODE% finished with exit code %APP_EXIT%
exit /b
