@echo off
setlocal
if "%~5"=="" exit /b 64
set "Path="
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"
call "%~1" -no_logo -arch=%~5 -host_arch=x64
if errorlevel 1 exit /b 65
set "CL=/MP2"
set "KERNEL_PROJECT_DIR=%~4"
if /I "%~4"=="v8_5_7_1" set "KERNEL_PROJECT_DIR=v8_5_7"
set "KERNEL_PROJECT=%~2/build/%KERNEL_PROJECT_DIR%/%~4.vcxproj"
if /I "%~4"=="openssl" set "KERNEL_PROJECT=%~2/node/openssl/openssl.vcxproj"
"%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" "%KERNEL_PROJECT%" /t:Build /m:1 /p:Configuration=Release /p:Platform=%~5 /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false /p:VcpkgEnabled=false /p:VCPkgLocalAppDataDisabled=true /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:SolutionDir="%~2/build/" /p:IntDir="%~3/obj/%~4-%~5/" /p:OutDir="%~3/lib/" /p:BuildProjectReferences=false /v:minimal
exit /b %errorlevel%
