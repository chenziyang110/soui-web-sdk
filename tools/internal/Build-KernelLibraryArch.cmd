@echo off
setlocal
if "%~5"=="" exit /b 64
set "Path="
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"
set "VSARCH=%~5"
set "PLATFORM=%~5"
if /I "%VSARCH%"=="Win32" set "VSARCH=x86"
if /I "%PLATFORM%"=="x86" set "PLATFORM=Win32"
call "%~1" -no_logo -arch=%VSARCH% -host_arch=x64 -vcvars_ver=14.44.35207 -winsdk=10.0.19041.0
if errorlevel 1 exit /b 65
set "CL=/MP2"
set "_CL_="
set "LINK="
set "_LINK_="
set "KERNEL_PROJECT_DIR=%~4"
if /I "%~4"=="v8_5_7_1" set "KERNEL_PROJECT_DIR=v8_5_7"
set "KERNEL_PROJECT=%~2/build/%KERNEL_PROJECT_DIR%/%~4.vcxproj"
if /I "%~4"=="openssl" set "KERNEL_PROJECT=%~2/node/openssl/openssl.vcxproj"
"%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" "%KERNEL_PROJECT%" /t:Build /m:1 /nr:false /p:Configuration=Release /p:Platform=%PLATFORM% /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false /p:VcpkgEnabled=false /p:VcpkgEnableManifest=false /p:VCPkgLocalAppDataDisabled=true /p:PlatformToolset=v143 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:UseEnv=false /p:SolutionDir="%~2/build/" /p:IntDir="%~3/obj/%~4-%~5/" /p:OutDir="%~3/lib/" /p:BuildProjectReferences=false /v:minimal
exit /b %errorlevel%
