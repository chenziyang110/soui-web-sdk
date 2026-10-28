[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [string]$DevkitRoot
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$toolsRoot=if($DevkitRoot){[IO.Path]::GetFullPath($DevkitRoot)}else{$root}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$saved=@{}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_TEST_AUTO_NONCE',
  'WEBVIEW2_BROWSER_EXECUTABLE_FOLDER','WEBVIEW2_USER_DATA_FOLDER','WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS',
  'WEBVIEW2_RELEASE_CHANNEL_PREFERENCE','WEBVIEW2_CHANNEL_SEARCH_KIND','WEBVIEW2_RELEASE_CHANNELS')
foreach($name in $names){
  $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
  $saved[$name]=@{present=($null-ne$item);value=if($null-ne$item){[string]$item.Value}else{$null}}
}
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  foreach($name in $names){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
  $env:SOUI_TEST_AUTO_NONCE=[Guid]::NewGuid().ToString('N')
  & node (Join-Path $root 'tools/internal/auto-smoke.cjs') run $Architecture $Configuration $toolsRoot
  if($LASTEXITCODE -ne 0){throw 'Auto backend startup/fallback validation failed; inspect the retained auto-owned attempt.'}
}finally{
  $errors=[Collections.Generic.List[string]]::new()
  foreach($name in $saved.Keys){try{
    $state=$saved[$name]
    if($state.present){Set-Item -LiteralPath ('Env:'+$name) -Value $state.value}
    else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
    $actual=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(($null-ne$actual)-ne$state.present-or($state.present-and[string]$actual.Value-cne$state.value)){throw 'Presence/value differs'}
  }catch{$errors.Add($name+': '+$_.Exception.Message)}}
  if($acquired){$lease.ReleaseMutex()}
  $lease.Dispose()
  if($errors.Count){throw ('Auto entry environment restoration failed: '+($errors -join '; '))}
}
