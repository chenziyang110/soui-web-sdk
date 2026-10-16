$ErrorActionPreference='Stop'
$root='C:/work/sm05-release-050'
. (Join-Path $root 'tools/internal/Find-LlvmToolchain.ps1')
. (Join-Path $root 'tools/internal/CMake-Configure.ps1')
$paths=@{LlvmRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64';MsvcRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133';WindowsSdkRoot='C:/Program Files (x86)/Windows Kits/10';NinjaExe='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'}
$profile=Get-SouiLlvmToolchain -Architecture x64 @paths -ToolIdentityBridge (Join-Path $PSScriptRoot 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $PSScriptRoot 'tools/internal/Tool-FileIdentity.ps1')
$cmake='C:/Program Files/CMake/bin/cmake.exe'
$build=Join-Path $PSScriptRoot 'out/native-build3'
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $PSScriptRoot -SourceDirectory (Join-Path $PSScriptRoot 'native') -BuildDirectory $build -Architecture x64 -FreshConfigure -NativeToolchain LLVM22MSVC142 -LlvmToolchain $profile)
& $cmake @configure '-DSouiSdk_DIR=C:/work/sm05-release-050/out/install/x64/lib/cmake/SouiSdk'
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
& $cmake --build $build --config Release --parallel 2
exit $LASTEXITCODE
