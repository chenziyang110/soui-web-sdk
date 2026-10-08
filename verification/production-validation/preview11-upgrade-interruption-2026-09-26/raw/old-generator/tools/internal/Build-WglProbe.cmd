@echo off
setlocal
if "%~4"=="" exit /b 64
call "%~1" -no_logo -arch=%~4 -host_arch=x64
if errorlevel 1 exit /b 65
cl /nologo /EHsc /W4 /Fo:"%~dp3wgl-probe.obj" "%~2" /link /OUT:"%~3" user32.lib gdi32.lib opengl32.lib
exit /b %errorlevel%
