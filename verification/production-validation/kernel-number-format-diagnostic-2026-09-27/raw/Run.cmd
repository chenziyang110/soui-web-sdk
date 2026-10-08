@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 65
cd /d "%~dp0"
set "ASAN_OPTIONS=halt_on_error=1:abort_on_error=1"
format-number-probe.exe
exit /b %errorlevel%
