[CmdletBinding()]
param([switch]$FreshConfigure,
      [ValidateSet('x86','x64')][string]$Architecture='x64',
      [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
      [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,
      [string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,
      [string]$ToolchainRequest)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'CMake-Configure.ps1')
$cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
$validationSource=Join-Path $workspace 'benchmarks/protocol'
$validationBuild=Join-Path $workspace $(if($Architecture -eq 'x64'){'out/benchmark-protocol'}else{'out/benchmark-protocol-x86'})
$measurement=Join-Path $workspace 'tools/internal/measurement-validation.cjs'
$profileHelper=Join-Path $workspace 'tools/internal/llvm-profile-inputs.cjs'
$llvm=$null;$profileArgs=@();$buildEnvironment=@{}
$nodeReusePrevious=Get-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -ErrorAction SilentlyContinue
$buildEnvironment['MSBUILDDISABLENODEREUSE']=@{present=($null-ne$nodeReusePrevious);value=if($null-ne$nodeReusePrevious){[string]$nodeReusePrevious.Value}else{$null}}
$externalPaths=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe)
if($NativeToolchain -eq 'MSVC143'){
  if(@($externalPaths|Where-Object{$_}).Count-or$ToolchainRequest){throw 'LLVM inputs require explicit NativeToolchain LLVM22MSVC142.'}
}else{
  if(@($externalPaths|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'LLVM measurement preparation requires all six explicit tool paths.'}
  . (Join-Path $PSScriptRoot 'Find-LlvmToolchain.ps1')
  $llvm=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot `
    -WindowsSdkRoot $WindowsSdkRoot -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe `
    -ToolIdentityBridge (Join-Path $PSScriptRoot 'Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $PSScriptRoot 'Tool-FileIdentity.ps1')
}
$llvmRemove=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
  'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
  'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
  'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
  'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH')
if($llvm){foreach($name in @($llvmRemove)+@('ComSpec','VcpkgEnabled','VcpkgEnableManifest')){
  $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
  $buildEnvironment[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
}}
try{
  # Configuration also launches toolchain checks; do not leave their MSBuild
  # nodes reusable when this script is called outside the standard observer.
  $env:MSBUILDDISABLENODEREUSE='1'
  if($llvm){
    foreach($name in $llvmRemove){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw "Could not clear measurement provider: $name"}}
    $env:ComSpec=$llvm.NativeProcessPaths.CMD
    $env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false'
    New-Item -ItemType Directory -Force -Path $validationBuild | Out-Null
    $requestPath=Join-Path $validationBuild 'llvm-profile-request-Release.json'
    $toolchainPath=Join-Path $workspace 'cmake/toolchains/SouiLLVM22.cmake'
    $request=[ordered]@{schema='soui.build-toolchain-request/v1';profile=$NativeToolchain;architecture=$Architecture;discovery=$llvm;
      toolchain=[ordered]@{path=$toolchainPath;bytes=(Get-Item -LiteralPath $toolchainPath).Length;sha256=(Get-FileHash -LiteralPath $toolchainPath).Hash.ToLowerInvariant()}}
    # This is measurement's own request. The parent Build request is never overwritten.
    [IO.File]::WriteAllText($requestPath,($request|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    if($ToolchainRequest){
      $comparisonPath=Join-Path $validationBuild 'llvm-discovery-input.json'
      [IO.File]::WriteAllText($comparisonPath,($llvm|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
      & node $profileHelper request ([IO.Path]::GetFullPath($ToolchainRequest)) $Architecture $workspace $comparisonPath | Out-Null
      if($LASTEXITCODE-ne0){throw 'Measurement discovery differs from the requested Build profile.'}
    }
    $profileArgs=@('--toolchain-request',$requestPath)
    if(-not$FreshConfigure-and[IO.File]::Exists((Join-Path $validationBuild 'measurement-tools.json'))){
      $reuseText=& node $measurement prepared $Architecture @profileArgs
      if($LASTEXITCODE-eq0){
        $reuse=$reuseText|ConvertFrom-Json
        # Retain the historical real compile/dependency records; no-op output is not new compile evidence.
        Write-Output $reuse.runner
        return
      }
    }
    if(-not$FreshConfigure-and[IO.File]::Exists((Join-Path $validationBuild 'CMakeCache.txt'))){
      throw 'Existing LLVM measurement outputs have no current verified v2 proof; prepare with -FreshConfigure before the standard controller uses their Runner.'
    }
  }
  & node $measurement invalidate $Architecture @profileArgs | Out-Null
  if($LASTEXITCODE -ne 0){throw 'Cannot invalidate previous validation tool evidence'}
  $measurementInputText=& node $measurement source $Architecture @profileArgs
  if($LASTEXITCODE -ne 0){throw 'Cannot fingerprint validation tool inputs'}
  $measurementInputs=$measurementInputText | ConvertFrom-Json
  $arguments=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $workspace `
    -SourceDirectory $validationSource -BuildDirectory $validationBuild -Architecture $Architecture -FreshConfigure:$FreshConfigure `
    -NativeToolchain $NativeToolchain -LlvmToolchain $llvm)
  if($llvm){$arguments+='-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON'}
  New-Item -ItemType Directory -Force -Path $validationBuild | Out-Null
  & $cmake @arguments *> (Join-Path $validationBuild 'configure.log')
  if($LASTEXITCODE -ne 0){throw "Validation tools configure failed: $validationBuild/configure.log"}
  $buildArgs=@('--build',$validationBuild,'--config','Release','--target','OwnedProcessRunner','OwnedProcessAssignmentFixture',
    'ProcessTreeSampler','InputTrace','NativeTraceIdentity','OwnedTailProcessFixture','OwnedTailMutationFixture','OwnedTailApiFixture','DisplayCalibration','--parallel','2')
  if($llvm){$buildArgs+='--verbose';if($FreshConfigure){$buildArgs+='--clean-first'}}else{$buildArgs+=@('--','/nr:false')}
  $nativeInvocation=[ordered]@{schema='soui.native-build-invocation/v1';nativeToolchain=$NativeToolchain;
    executable=$cmake;arguments=$buildArgs;
    environment=[ordered]@{MSBUILDDISABLENODEREUSE=$env:MSBUILDDISABLENODEREUSE};
    compilerHelperSuppressionConfigured=$false}
  [IO.File]::WriteAllText((Join-Path $validationBuild 'native-invocation-Release.json'),($nativeInvocation|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  & $cmake @buildArgs *> (Join-Path $validationBuild 'compile.log')
  if($LASTEXITCODE -ne 0){throw "Validation tools build failed: $validationBuild/compile.log"}
  if($llvm){
    foreach($read in @(@{mode='commands';name='ninja-commands-Release.log'},@{mode='deps';name='ninja-deps-Release.log'})){
      $lines=& $llvm.ToolPins.NINJA.path -C $validationBuild -f build-Release.ninja -t $read.mode
      if($LASTEXITCODE-ne0){throw "Actual measurement Ninja $($read.mode) query failed."}
      $text=([string]::Join([char]10,[string[]]$lines))+[char]10
      if([Text.Encoding]::UTF8.GetByteCount($text)-gt16777216){throw 'Measurement Ninja observation exceeds 16 MiB.'}
      [IO.File]::WriteAllText((Join-Path $validationBuild $read.name),$text,[Text.UTF8Encoding]::new($false))
    }
  }
  & node $measurement seal $Architecture $measurementInputs.sha256 @profileArgs | Out-Null
  if($LASTEXITCODE -ne 0){throw 'Validation tools architecture/source/output identity failed'}
  Write-Output (Join-Path $validationBuild 'Release/OwnedProcessRunner.exe')
}finally{
  $restoreErrors=[Collections.Generic.List[string]]::new()
  foreach($name in $buildEnvironment.Keys){try{
    $state=$buildEnvironment[$name]
    if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
    $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(($null-ne$actual)-ne$state.present-or($state.present-and[string]$actual.Value-cne$state.value)){throw 'Presence/value differs'}
  }catch{$restoreErrors.Add($name+': '+$_.Exception.Message)}}
  if($restoreErrors.Count){throw ('Measurement environment restore failed: '+($restoreErrors-join'; '))}
}
