@echo off
setlocal
if "%~6"=="" exit /b 64
if /I "%~7"=="LLVM22MSVC142" goto llvm
if not "%~7"=="" exit /b 64
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

:llvm
setlocal DisableDelayedExpansion
set "SOURCE=%~2"
set "OUTPUT=%~3"
set "VSARCH=%~6"
set "SELECTED_VCVARS=%~1"
set "DEPENDENCIES=%~4"
if /I not "%~5"=="none" exit /b 64
if /I not "%VSARCH%"=="x86" if /I not "%VSARCH%"=="x64" exit /b 64
shift
shift
shift
shift
shift
shift
if /I not "%~1"=="LLVM22MSVC142" exit /b 64
if "%~6"=="" exit /b 64
if not "%~7"=="" exit /b 64
for %%P in ("%~2/.") do set "VS=%%~fP"
for %%P in ("%~3/.") do set "LLVM=%%~fP"
for %%P in ("%~4/.") do set "MSVC=%%~fP"
for %%P in ("%~5/.") do set "SDK=%%~fP"
for %%P in ("%~6") do set "PORTABLE_TARGETS=%%~fP"
for %%P in ("%SELECTED_VCVARS%") do set "SELECTED_VCVARS=%%~fP"
if /I not "%SELECTED_VCVARS%"=="%VS%\VC\Auxiliary\Build\vcvarsall.bat" exit /b 64
if /I not "%MSVC%"=="%VS%\VC\Tools\MSVC\14.29.30133" exit /b 64
if not exist "%VS%\MSBuild\Current\Bin\amd64\MSBuild.exe" exit /b 64
if not exist "%PORTABLE_TARGETS%" exit /b 64
set "PLATFORM=x64"
set "VCVARSARCH=amd64"
if /I "%VSARCH%"=="x86" set "PLATFORM=Win32"
if /I "%VSARCH%"=="x86" set "VCVARSARCH=amd64_x86"
set "PATH=%SystemRoot%\System32;%SystemRoot%"
for %%E in (CL _CL_ LINK _LINK_ INCLUDE LIB LIBPATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH CCC_OVERRIDE_OPTIONS CLANG_CONFIG_FILE_USER_DIR CLANG_CONFIG_FILE_SYSTEM_DIR EXTERNAL_INCLUDE CC CXX AR RC CFLAGS CXXFLAGS LDFLAGS RCFLAGS MSBuildExtensionsPath MSBuildExtensionsPath32 MSBuildExtensionsPath64 MSBuildUserExtensionsPath MSBuildSDKsPath MSBuildOverrideTasksPath VSCMD_VER __VSCMD_PREINIT_PATH VSCMD_ARG_HOST_ARCH VSCMD_ARG_TGT_ARCH) do set "%%E="
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "MSBUILDDISABLENODEREUSE=1"
set "VcpkgEnabled=false"
set "VcpkgEnableManifest=false"
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %VCVARSARCH% 10.0.19041.0 -vcvars_ver=14.29.30133
if errorlevel 1 exit /b 65
if /I not "%VCToolsVersion%"=="14.29.30133" exit /b 66
if /I not "%WindowsSDKVersion%"=="10.0.19041.0\" exit /b 67
if /I not "%VSCMD_ARG_HOST_ARCH%"=="x64" exit /b 68
if /I not "%VSCMD_ARG_TGT_ARCH%"=="%VSARCH%" exit /b 69
for %%P in ("%VCToolsInstallDir%/.") do set "ACTUALVC=%%~fP"
for %%P in ("%WindowsSdkDir%/.") do set "ACTUALSDK=%%~fP"
if /I not "%ACTUALVC%"=="%MSVC%" exit /b 70
if /I not "%ACTUALSDK%"=="%SDK%" exit /b 71
set "INCLUDE=%LLVM%\lib\clang\22\include;%MSVC%\include;%SDK%\Include\10.0.19041.0\ucrt;%SDK%\Include\10.0.19041.0\shared;%SDK%\Include\10.0.19041.0\um"
set "LIB=%MSVC%\lib\%VSARCH%;%SDK%\Lib\10.0.19041.0\ucrt\%VSARCH%;%SDK%\Lib\10.0.19041.0\um\%VSARCH%"
set "LIBPATH="
set "PATH=%LLVM%\bin;%MSVC%\bin\Hostx64\%VSARCH%;%VS%\MSBuild\Current\Bin\amd64;%SystemRoot%\System32;%SystemRoot%"
set "CL="
set "_CL_="
set "LINK="
set "_LINK_="
set "PROJECT=%SOURCE%/build/miniblink/miniblink.vcxproj"
set "TARGET=miniblink"
if /I "%VSARCH%"=="x86" set "TARGET=node"
set "INTDIR=%OUTPUT%/obj/"
set "OUTDIR=%OUTPUT%/bin/"
set "TEMP=%OUTPUT%/llvm-temp"
set "TMP=%TEMP%"
if not exist "%TEMP%/." exit /b 72
if not exist "%OUTPUT%/empty-user-props/." exit /b 73
if exist "%INTDIR%/." exit /b 74
if exist "%OUTDIR%/%TARGET%.dll" exit /b 74
"%VS%\MSBuild\Current\Bin\amd64\MSBuild.exe" "%PROJECT%" /t:Build /m:1 /nr:false /p:Configuration=Release /p:Platform=%PLATFORM% /p:PlatformToolset=ClangCL /p:VCToolsVersion=14.29.30133 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PreferredToolArchitecture=x64 /p:LLVMInstallDir="%LLVM%" /p:LLVMToolsVersion=22.1.3 /p:CLToolExe=clang-cl.exe /p:CLToolPath="%LLVM%/bin" /p:LinkToolExe=lld-link.exe /p:LinkToolPath="%LLVM%/bin" /p:LIBToolExe=llvm-lib.exe /p:LIBToolPath="%LLVM%/bin" /p:SOUIKernelProfile=LLVM22MSVC142 /p:SOUIKernelArchitecture=%VSARCH% /p:SOUIKernelMsvcRoot="%MSVC%" /p:SOUIKernelSdkRoot="%SDK%" /p:TargetName=%TARGET% /p:TargetExt=.dll /p:UseEnv=false /p:TrackFileAccess=true /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false /p:UserRootDir="%OUTPUT%/empty-user-props/" /p:VcpkgEnabled=false /p:VcpkgEnableManifest=false /p:VCPkgLocalAppDataDisabled=true /p:BuildProjectReferences=false /p:SolutionDir="%SOURCE%/build/" /p:IntDir="%INTDIR%" /p:OutDir="%OUTDIR%" /p:KernelDependencyRoot="%DEPENDENCIES%" /p:GpuExperiment=false /p:GpuDependencyRoot= /p:RCToolExe=rc.exe /p:RCToolPath="%SDK%/bin/10.0.19041.0/x64" /p:MtToolExe=mt.exe /p:MtToolPath="%SDK%/bin/10.0.19041.0/x64" /p:ForceImportBeforeCppTargets="%PORTABLE_TARGETS%" /v:minimal /bl:"%OUTPUT%/build.binlog"
exit /b %errorlevel%
