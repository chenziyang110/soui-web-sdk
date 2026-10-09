@echo off
setlocal
if "%~6"=="" exit /b 64
set "VSARCH=%~6"
set "PLATFORM=%~6"
if /I "%VSARCH%"=="Win32" set "VSARCH=x86"
if /I "%PLATFORM%"=="x86" set "PLATFORM=Win32"
set "Path="
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"
call "%~1" -no_logo -arch=%VSARCH% -host_arch=x64 -vcvars_ver=14.44.35207 -winsdk=10.0.19041.0
if errorlevel 1 exit /b 65
set "CL=/MP2"
set "_CL_="
set "LINK="
set "_LINK_="
set "GPU_PROPS="
if /I not "%~5"=="none" if not "%~5"=="" set "GPU_PROPS=/p:GpuExperiment=true /p:GpuDependencyRoot="%~5""
"%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" "%~2/build/miniblink/miniblink.vcxproj" /t:Build /m:1 /nr:false /p:Configuration=Release /p:Platform=%PLATFORM% /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false /p:VcpkgEnabled=false /p:VcpkgEnableManifest=false /p:VCPkgLocalAppDataDisabled=true /p:PlatformToolset=v143 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PreferredToolArchitecture=x64 /p:UseEnv=false /p:SolutionDir="%~2/build/" /p:IntDir="%~3/obj/" /p:OutDir="%~3/bin/" /p:KernelDependencyRoot="%~4" /p:GpuExperiment=false %GPU_PROPS% /p:BuildProjectReferences=false /v:minimal /bl:"%~3/build.binlog"
exit /b %errorlevel%
