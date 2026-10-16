param([ValidateSet('on','consumer','off','off-reject')][string]$Mode='on',[ValidateSet('x64','x86')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root='C:/work/sm05-release-050'
. (Join-Path $root 'tools/internal/Find-LlvmToolchain.ps1')
. (Join-Path $root 'tools/internal/CMake-Configure.ps1')
$paths=@{LlvmRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64';MsvcRoot='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133';WindowsSdkRoot='C:/Program Files (x86)/Windows Kits/10';NinjaExe='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'}
$profile=Get-SouiLlvmToolchain -Architecture $Architecture @paths -ToolIdentityBridge (Join-Path $PSScriptRoot 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $PSScriptRoot 'tools/internal/Tool-FileIdentity.ps1')
$cmake='C:/Program Files/CMake/bin/cmake.exe'
$consumer=$Mode-in@('consumer','off-reject')
$build=Join-Path $PSScriptRoot ('out/'+$Architecture+'/'+$(if($consumer){$Mode}else{'sdk'}))
$source=Join-Path $PSScriptRoot $(if($consumer){'consumer'}else{'native'})
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $PSScriptRoot -SourceDirectory $source -BuildDirectory $build -Architecture $Architecture -NativeToolchain LLVM22MSVC142 -LlvmToolchain $profile)
if($consumer){
  $prefix=Join-Path $PSScriptRoot ('prefix/'+$Architecture+'/'+$(if($Mode-eq'consumer'){'relocated-on'}else{'off'}))
  & $cmake @configure ('-DSOUI_CONSUMER_PREFIX='+$prefix)
}else{
  & $cmake @configure ('-DSOUI_BUILD_UIA='+$(if($Mode-eq'on'){'ON'}else{'OFF'})) '-DSOUI_BUILD_WEBVIEW2=OFF'
}
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
if($Mode-eq'off-reject'){throw 'Unexpected successful configuration of required Uia from OFF installation'}
& $cmake --build $build --config Release --parallel 2
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
if(!$consumer){
  & $cmake --install $build --config Release --prefix (Join-Path $PSScriptRoot ('prefix/'+$Architecture+'/'+$Mode))
  if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
}
exit 0
