[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [ValidateSet('all','reload','recreate','multiwindow','bridge','mixed','session')][string]$Scenario='all',
  [ValidateRange(1,10000)][int]$Cycles=3,
  [ValidateRange(0,100)][int]$Warmup=0,
  [ValidateRange(1000,43200000)][int]$DurationMs=60000,
  [ValidateRange(1,60000)][int]$IdleMs=10000,
  [ValidateRange(0,60000)][int]$SettleMs=500,
  [ValidateRange(1,120000)][int]$StepTimeoutMs=20000,
  [string]$RuntimeBinary,
  [ValidateSet('ordinary','message-port')][string]$Fixture='ordinary',
  [switch]$ResourceCapture,
  [switch]$CacheObservation,
  [switch]$LongRunCapture,
  [switch]$PrepareOnly,
  [switch]$SkipSdkBuild,
  [switch]$FreshConfigure,
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,
  [string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,
  [string]$DevkitRoot
)
$ErrorActionPreference='Stop'
if($CacheObservation -and -not $ResourceCapture){throw 'CacheObservation requires the explicit ResourceCapture calibration route'}
$llvmPaths=[ordered]@{LlvmRoot=$LlvmRoot;MsvcRoot=$MsvcRoot;WindowsSdkRoot=$WindowsSdkRoot;
  NinjaExe=$NinjaExe;WindowsCmdExe=$WindowsCmdExe;PowerShellExe=$PowerShellExe}
if($NativeToolchain -eq 'LLVM22MSVC142'){
  foreach($value in $llvmPaths.Values){
    if([string]::IsNullOrWhiteSpace($value)-or$value-notmatch'^[A-Za-z]:[\\/]'-or$value-match'[;"\x00-\x1f]'){
      throw 'LLVM acceptance requires all six explicit absolute safe tool paths'
    }
  }
  if($DevkitRoot-and($DevkitRoot-notmatch'^[A-Za-z]:[\\/]'-or$DevkitRoot-match'[;"\x00-\x1f]')){throw 'DevkitRoot must be an explicit absolute safe stock owner'}
  if($Fixture -ne 'ordinary'){throw 'Installed LLVM acceptance supports only the ordinary fixture'}
}elseif(@($llvmPaths.Values|Where-Object{$_}).Count-or$PSBoundParameters.ContainsKey('DevkitRoot')){
  throw 'LLVM tool paths and DevkitRoot require explicit LLVM22MSVC142'
}
if($LongRunCapture){
  if($ResourceCapture){throw 'LongRunCapture and ResourceCapture are distinct frozen protocols'}
  if($PrepareOnly -or $Fixture -ne 'ordinary'){throw 'LongRunCapture requires actual ordinary acceptance execution'}
  $longRequired=[ordered]@{SettleMs=500;StepTimeoutMs=20000}
  if($Scenario -in @('mixed','session')){
    $longRequired.DurationMs=28800000
    $longRequired.IdleMs=if($Scenario -eq 'session'){60000}else{10000}
  }else{$longRequired.Cycles=1000;$longRequired.Warmup=0}
  foreach($entry in $longRequired.GetEnumerator()){
    if($PSBoundParameters.ContainsKey($entry.Key)){
      if((Get-Variable -Name $entry.Key -ValueOnly) -cne $entry.Value){throw ('LongRunCapture requires '+$entry.Key+'='+$entry.Value)}
    }else{Set-Variable -Name $entry.Key -Value $entry.Value}
  }
}
if($ResourceCapture){
  if($PrepareOnly){throw 'ResourceCapture requires sampling; prepare separately without ResourceCapture'}
  if($Fixture -ne 'ordinary'){throw 'Resource capture requires the ordinary acceptance fixture'}
  if($PSBoundParameters.ContainsKey('DurationMs') -or $PSBoundParameters.ContainsKey('IdleMs')){
    throw 'Resource capture uses the frozen reload protocol, not duration acceptance'
  }
  $resourceRequired=[ordered]@{Scenario='reload';Cycles=100;Warmup=10;SettleMs=5000;StepTimeoutMs=20000}
  foreach($entry in $resourceRequired.GetEnumerator()){
    if($PSBoundParameters.ContainsKey($entry.Key)){
      if((Get-Variable -Name $entry.Key -ValueOnly) -cne $entry.Value){
        throw ('Resource capture requires '+$entry.Key+'='+$entry.Value)
      }
    }else{Set-Variable -Name $entry.Key -Value $entry.Value}
  }
}
if($Fixture -eq 'message-port' -and -not $PrepareOnly){throw 'Message-port preparation requires PrepareOnly; use its separate authenticated behavior controller'}
if($Scenario -in @('mixed','session') -and ($PSBoundParameters.ContainsKey('Cycles') -or $PSBoundParameters.ContainsKey('Warmup'))){
  throw 'Duration acceptance uses DurationMs and IdleMs, not Cycles or Warmup'
}
if($Scenario -notin @('mixed','session') -and ($PSBoundParameters.ContainsKey('DurationMs') -or $PSBoundParameters.ContainsKey('IdleMs'))){
  throw 'DurationMs and IdleMs are only valid for mixed or session acceptance'
}
if($Scenario -eq 'session'){
  if(-not $PSBoundParameters.ContainsKey('DurationMs')){$DurationMs=28800000}
  if(-not $PSBoundParameters.ContainsKey('IdleMs')){$IdleMs=60000}
  if($DurationMs -lt 3120000){throw 'Session continuity requires at least 52 real minutes'}
}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$failed=$false
$llvmEnvironment=@{}
try {
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned SOUI desktop validation lease'}
  if(-not $acquired){throw 'Another SOUI desktop validation is running'}
. (Join-Path $PSScriptRoot 'internal/CMake-Configure.ps1')
$runner=Join-Path $root 'tests/acceptance/runner.cjs'
if($NativeToolchain -eq 'LLVM22MSVC142'){
  $toolsRoot=if($DevkitRoot){[IO.Path]::GetFullPath($DevkitRoot)}else{$root}
  Assert-SouiCMakePath $toolsRoot
  $providerNames=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
    'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
    'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
    'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
    'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH',
    'ComSpec','VcpkgEnabled','VcpkgEnableManifest','MSBUILDDISABLENODEREUSE')
  foreach($name in $providerNames){
    $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    $llvmEnvironment[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
  }
  foreach($name in $providerNames){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw ('Could not clear LLVM acceptance provider: '+$name)}}
  $env:ComSpec=[IO.Path]::GetFullPath($WindowsCmdExe);$env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'out') | Out-Null
& npm.cmd --prefix (Join-Path $root 'tests/acceptance') ci --no-audit --no-fund *> (Join-Path $root 'out/acceptance-toolchain-install.log')
if($LASTEXITCODE -ne 0){throw 'Independent Web consumer toolchain install failed'}
if($NativeToolchain -eq 'LLVM22MSVC142'){
  $stockArgs=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',
    (Join-Path $toolsRoot 'tools/internal/Build-ValidationTools.ps1'),'-Architecture',$Architecture,'-NativeToolchain',$NativeToolchain)
  foreach($entry in $llvmPaths.GetEnumerator()){$stockArgs+=@('-'+$entry.Key,[string]$entry.Value)}
  if($FreshConfigure){$stockArgs+='-FreshConfigure'}
  & $PowerShellExe @stockArgs
  if($LASTEXITCODE -ne 0){throw 'Selected architecture LLVM stock preparation failed'}
  if(-not $SkipSdkBuild){
    $sdkArgs=@((Join-Path $PSScriptRoot 'internal/standard-build.cjs'),'--root',$root,'--devkit-root',$toolsRoot,
      '--architecture',$Architecture,'--configuration',$Configuration,'--sdk-only','--native-toolchain',$NativeToolchain)
    $llvmFlags=[ordered]@{LlvmRoot='--llvm-root';MsvcRoot='--msvc-root';WindowsSdkRoot='--windows-sdk-root';
      NinjaExe='--ninja-exe';WindowsCmdExe='--windows-cmd-exe';PowerShellExe='--powershell-exe'}
    foreach($entry in $llvmFlags.GetEnumerator()){$sdkArgs+=@($entry.Value,[string]$llvmPaths[$entry.Key])}
    if($FreshConfigure){$sdkArgs+='--fresh-configure'}
    & node @sdkArgs
    if($LASTEXITCODE -ne 0){throw 'Naturally accepted standard LLVM SDK build is required'}
  }
  & node (Join-Path $PSScriptRoot 'internal/llvm-build-observer.cjs') verify $root $Architecture $Configuration sdk-only `
    (Join-Path $root "out/sdk-$Architecture/llvm-profile-request-$Configuration.json")
  if($LASTEXITCODE -ne 0){throw 'Current independently accepted LLVM SDK build is required'}
}else{
  if(-not $SkipSdkBuild){
    & (Join-Path $PSScriptRoot 'Build.ps1') -SdkOnly -Architecture $Architecture -Configuration $Configuration -FreshConfigure:$FreshConfigure
    if($LASTEXITCODE -ne 0){throw 'SDK build failed'}
  }
  & (Join-Path $PSScriptRoot 'internal/Build-ValidationTools.ps1') -FreshConfigure:$FreshConfigure | Out-Null
  if($LASTEXITCODE -ne 0){throw 'Owned process supervision tools build failed'}
}
$suite=Join-Path $root ('out/acceptance/'+[guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $suite | Out-Null
$runtime=if($RuntimeBinary){[IO.Path]::GetFullPath($RuntimeBinary)}else{Join-Path $root "third_party/prebuilt/$Architecture/miniblink/node.dll"}
$prepareCommand=if($Fixture -eq 'message-port'){'prepare-message-port'}else{'prepare'}
if($NativeToolchain -eq 'LLVM22MSVC142'){
  & node $runner prepare-llvm $suite $Architecture $Configuration $runtime $toolsRoot
}else{& node $runner $prepareCommand $suite $Architecture $Configuration $runtime}
if($LASTEXITCODE -ne 0){throw 'Current SDK build evidence is required'}
$ErrorActionPreference='Continue'
& node (Join-Path $root 'tests/acceptance/build-web.mjs') $suite *> (Join-Path $suite 'web-build.log')
$ErrorActionPreference='Stop'
if($LASTEXITCODE -ne 0){throw "Independent public Web SDK consumer build failed: $suite"}
$sdkBuild=Join-Path $root "out/sdk-$Architecture"
$install=Join-Path $suite 'install'
& cmake --install $sdkBuild --config $Configuration --prefix $install *> (Join-Path $suite 'install.log')
if($LASTEXITCODE -ne 0){throw "SDK install failed: $suite"}
$consumer=Join-Path $suite 'consumer'
if($NativeToolchain -eq 'LLVM22MSVC142'){
  . (Join-Path $PSScriptRoot 'internal/Find-LlvmToolchain.ps1')
  $consumerProfile=Get-SouiLlvmToolchain -Architecture $Architecture @llvmPaths `
    -ToolIdentityBridge (Join-Path $PSScriptRoot 'internal/Invoke-ToolFileIdentity.ps1') `
    -ToolIdentityHelper (Join-Path $PSScriptRoot 'internal/Tool-FileIdentity.ps1')
  New-Item -ItemType Directory -Path $consumer | Out-Null
  $toolchainFile=Join-Path $root 'cmake/toolchains/SouiLLVM22.cmake'
  $ownToolchainValues=@{path=$toolchainFile;bytes=(Get-Item -LiteralPath $toolchainFile).Length;
    sha256=(Get-FileHash -LiteralPath $toolchainFile -Algorithm SHA256).Hash.ToLowerInvariant()}
  # The installed reader retains the product request's JSON property order.
  # Only its three key names determine serialization; every value is independently measured above.
  $sdkRequestPath=Join-Path $root "out/sdk-$Architecture/llvm-profile-request-$Configuration.json"
  if((Get-Item -LiteralPath $sdkRequestPath).Length -gt 1048576){throw 'SDK request exceeds the fixed 1 MiB request boundary'}
  $sdkToolchain=([IO.File]::ReadAllText($sdkRequestPath)|ConvertFrom-Json).toolchain
  $toolchainNames=@($sdkToolchain.PSObject.Properties.Name)
  if($toolchainNames.Count-ne3-or@($toolchainNames|Where-Object{$_-cnotin@('path','bytes','sha256')}).Count){throw 'SDK toolchain key membership differs'}
  $ownToolchain=[ordered]@{};foreach($name in $toolchainNames){$ownToolchain[$name]=$ownToolchainValues[$name]}
  $consumerRequest=[ordered]@{schema='soui.build-toolchain-request/v1';profile=$NativeToolchain;architecture=$Architecture;discovery=$consumerProfile;
    toolchain=$ownToolchain}
  [IO.File]::WriteAllText((Join-Path $consumer "llvm-profile-request-$Configuration.json"),($consumerRequest|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
  $cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
  $configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $root `
    -SourceDirectory (Join-Path $suite 'consumer-source') -BuildDirectory $consumer -Architecture $Architecture -FreshConfigure `
    -NativeToolchain $NativeToolchain -LlvmToolchain $consumerProfile)
  $configure+=@("-DSouiSdk_DIR=$install/lib/cmake/SouiSdk","-DSOUI_ACCEPTANCE_WEB_ROOT=$suite/web-output")
  $command=[ordered]@{schema='soui.installed-acceptance-llvm-configure/v1';cmake=$cmake;args=$configure}
  [IO.File]::WriteAllText((Join-Path $suite 'consumer-llvm-configure.json'),($command|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  & node (Join-Path $PSScriptRoot 'internal/llvm-installed-acceptance.cjs') build $suite
  if($LASTEXITCODE -ne 0){throw "Independent controlled LLVM consumer build failed: $suite"}
}else{
  $configure=@(Get-SouiCMakeConfigureArguments -CMake (Get-Command cmake.exe -ErrorAction Stop).Source -WorkspaceRoot $root `
    -SourceDirectory (Join-Path $suite 'consumer-source') -BuildDirectory $consumer -Architecture $Architecture -FreshConfigure:$FreshConfigure)
  & cmake @configure "-DSouiSdk_DIR=$install/lib/cmake/SouiSdk" "-DSOUI_ACCEPTANCE_WEB_ROOT=$suite/web-output" *> (Join-Path $suite 'configure.log')
  if($LASTEXITCODE -ne 0){throw "Independent consumer configure failed: $suite"}
  & cmake --build $consumer --config $Configuration --parallel 2 *> (Join-Path $suite 'compile.log')
  if($LASTEXITCODE -ne 0){throw "Independent consumer build failed: $suite"}
}
& node $runner stage $suite
if($LASTEXITCODE -ne 0){throw 'Acceptance build/source identity failed'}
if($PrepareOnly){Write-Output "Acceptance build prepared; no desktop scenarios executed: $suite";exit 0}
  if($LongRunCapture){
    if($Scenario -in @('mixed','session')){
      $command=if($Scenario -eq 'session'){'run-long-session'}else{'run-long-mixed'}
      & node $runner $command $suite
      if($LASTEXITCODE -ne 0){$failed=$true}
    }else{
      $longModes=if($Scenario -eq 'all'){@('reload','recreate','multiwindow','bridge')}else{@($Scenario)}
      foreach($mode in $longModes){
        & node $runner run-long $suite $mode
        if($LASTEXITCODE -ne 0){$failed=$true}
      }
    }
  } elseif($Scenario -in @('mixed','session')){
    $command=if($Scenario -eq 'session'){'run-session'}else{'run-mixed'}
    & node $runner $command $suite $DurationMs $SettleMs $StepTimeoutMs $IdleMs
    if($LASTEXITCODE -ne 0){$failed=$true}
  } elseif($ResourceCapture){
    $resourceArgs=@('run-resource',$suite)
    if($CacheObservation){$resourceArgs+='--cache-observation=1'}
    & node $runner @resourceArgs
    if($LASTEXITCODE -ne 0){$failed=$true}
  } else {
    $modes=if($Scenario -eq 'all'){@('reload','recreate','multiwindow','bridge')}else{@($Scenario)}
    foreach($mode in $modes){
      & node $runner run $suite $mode $Cycles $Warmup $SettleMs $StepTimeoutMs
      if($LASTEXITCODE -ne 0){$failed=$true}
    }
  }
} finally {
  $restoreErrors=[Collections.Generic.List[string]]::new()
  foreach($name in $llvmEnvironment.Keys){try{
    $state=$llvmEnvironment[$name]
    if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
    $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(($null-ne$actual)-ne$state.present-or($state.present-and[string]$actual.Value-cne$state.value)){throw 'Presence/value differs'}
  }catch{$restoreErrors.Add($name+': '+$_.Exception.Message)}}
  try{if($acquired){$lease.ReleaseMutex()}}finally{$lease.Dispose()}
  if($restoreErrors.Count){throw ('LLVM acceptance environment restore failed: '+($restoreErrors-join'; '))}
}
Write-Output "Acceptance evidence: $suite"
if($failed){
  if($ResourceCapture){throw ('Resource capture has no independently passed resource gate; see retained operation and resource results: '+$suite)}
  throw 'One or more real SDK acceptance scenarios failed; see retained reports'
}
