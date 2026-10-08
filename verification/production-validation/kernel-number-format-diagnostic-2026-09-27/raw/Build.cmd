@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 65
cd /d "%~dp0"
cl.exe /nologo /std:c++17 /EHsc /O2 /Zi /MD /fsanitize=address /D_CRT_SECURE_NO_WARNINGS format-number-probe.cpp /Fe:format-number-probe.exe /Fo:format-number-probe.obj /link /INCREMENTAL:NO /DEBUG:FULL
exit /b %errorlevel%
