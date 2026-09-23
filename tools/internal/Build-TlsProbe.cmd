@echo off
setlocal
if "%~6"=="" exit /b 64
call "%~1" -no_logo -arch=%~6 -host_arch=x64
if errorlevel 1 exit /b 65
cl /nologo /EHsc /std:c++17 /W4 /Fo:"%~dp3tls-loader.obj" /I"%~4" /I"%~5" "%~2" /link /OUT:"%~3" user32.lib
exit /b %errorlevel%
