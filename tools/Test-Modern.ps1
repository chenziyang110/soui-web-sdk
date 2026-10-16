[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,
  [string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,
  [string]$ToolchainRequest,[string]$DevkitRoot
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$toolsRoot=if($DevkitRoot){[IO.Path]::GetFullPath($DevkitRoot)}else{$root}
$externalInputs=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe)
if($NativeToolchain -eq 'MSVC143'){
  if($ToolchainRequest -or @($externalInputs|Where-Object{$_}).Count){throw 'LLVM inputs require explicit NativeToolchain LLVM22MSVC142.'}
}else{
  if(@($externalInputs|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'Modern LLVM verification requires all six explicit paths.'}
  if(-not $ToolchainRequest){$ToolchainRequest=Join-Path $root "out/$Architecture/llvm-profile-request-$Configuration.json"}
  $ToolchainRequest=[IO.Path]::GetFullPath($ToolchainRequest)
}
# The application uses SHGetFolderPathW, not an assumed LOCALAPPDATA override.
$localAppData=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
if([string]::IsNullOrWhiteSpace($localAppData) -or -not [IO.Path]::IsPathRooted($localAppData)){throw 'The actual LocalApplicationData known folder is unavailable.'}
$arguments=@('run',$Architecture,$Configuration,$toolsRoot,[IO.Path]::GetFullPath($localAppData),'--native-toolchain',$NativeToolchain)
if($NativeToolchain -eq 'LLVM22MSVC142'){
  $arguments+=@('--toolchain-request',$ToolchainRequest,'--llvm-root',[IO.Path]::GetFullPath($LlvmRoot),
    '--msvc-root',[IO.Path]::GetFullPath($MsvcRoot),'--windows-sdk-root',[IO.Path]::GetFullPath($WindowsSdkRoot),
    '--ninja-exe',[IO.Path]::GetFullPath($NinjaExe),'--windows-cmd-exe',[IO.Path]::GetFullPath($WindowsCmdExe),
    '--powershell-exe',[IO.Path]::GetFullPath($PowerShellExe))
}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$saved=@{}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_TEST_MODERN_NONCE')
foreach($name in $names){
  $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
  $saved[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
}
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  foreach($name in $names){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw ('Cannot clear Modern entry environment: '+$name)}}
  $env:SOUI_TEST_MODERN_NONCE=[Guid]::NewGuid().ToString('N')
  & node (Join-Path $root 'tools/internal/modern-smoke.cjs') @arguments
  if($LASTEXITCODE -ne 0){throw 'Modern startup or natural Owned tree closure failed; inspect the retained modern-owned attempt.'}
}finally{
  $restoreErrors=[Collections.Generic.List[string]]::new()
  foreach($name in $saved.Keys){try{
    $state=$saved[$name]
    if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value -ErrorAction Stop}
    else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
    $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(($null-ne$actual)-ne$state.present-or($state.present-and[string]$actual.Value-cne$state.value)){throw 'Presence/value differs'}
  }catch{$restoreErrors.Add($name+': '+$_.Exception.Message)}}
  if($acquired){$lease.ReleaseMutex()}
  $lease.Dispose()
  if($restoreErrors.Count){throw ('Modern entry environment restoration failed: '+($restoreErrors -join '; '))}
}
