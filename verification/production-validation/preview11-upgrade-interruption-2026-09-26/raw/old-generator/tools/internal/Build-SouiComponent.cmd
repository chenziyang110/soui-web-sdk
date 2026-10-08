@echo off
setlocal
if "%~4"=="" exit /b 64
set "Path="
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"
call "%~1" -no_logo -arch=%~3 -host_arch=x64
if errorlevel 1 exit /b 65
set "SOUI_PLATFORM=%~3"
if /I "%~3"=="x86" set "SOUI_PLATFORM=Win32"
set "CL=/MP2"
"%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" "%~2" /t:Build /m:1 /p:Configuration=Release /p:Platform=%SOUI_PLATFORM% /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false /p:VcpkgEnabled=false /p:VCPkgLocalAppDataDisabled=true /p:IntDir="%~4/" /v:minimal
exit /b %errorlevel%
