# SOUI source-delivery command entry. Internal callers do not own this lease.
$ErrorActionPreference='Stop'
$commandArguments=@($args | ForEach-Object { [string]$_ })
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$previous=@{}
$keys=@('SOUI_DEVKIT_ENTRY_NONCE','SOUI_DEVKIT_POWERSHELL','SOUI_DEVKIT_CTEST')
$exitCode=1
try {
  try { $acquired=$lease.WaitOne(0) }
  catch [Threading.AbandonedMutexException] { $acquired=$true }
  if(-not $acquired) { throw 'E_DESKTOP_BUSY' }
  foreach($key in $keys) { $previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process') }
  $node=(Get-Command node.exe -ErrorAction Stop).Source
  $powershell=Join-Path $PSHOME $(if($PSVersionTable.PSEdition -eq 'Core'){'pwsh.exe'}else{'powershell.exe'})
  if(-not [IO.File]::Exists($powershell)) { throw 'E_POWERSHELL_MISSING' }
  $env:SOUI_DEVKIT_ENTRY_NONCE=[Guid]::NewGuid().ToString('N')
  $env:SOUI_DEVKIT_POWERSHELL=$powershell
  $ctest=Get-Command ctest.exe -ErrorAction SilentlyContinue
  $env:SOUI_DEVKIT_CTEST=if($ctest){$ctest.Source}else{''}
  & $node (Join-Path $workspace 'tools/internal/devkit-command.cjs') @commandArguments
  $exitCode=$LASTEXITCODE
  if($null -eq $exitCode) { $exitCode=1 }
} catch {
  $code=if($_.Exception.Message -eq 'E_DESKTOP_BUSY'){'E_DESKTOP_BUSY'}else{'E_ENTRY_SETUP'}
  $fix=if($code -eq 'E_DESKTOP_BUSY'){'Wait for the current SOUI desktop/build/dev owner to finish.'}else{'Install the required Node.js/PowerShell tools and inspect the entry setup error.'}
  [Console]::Error.WriteLine('SOUI '+$code+': '+$fix)
  [ordered]@{schema='soui.devkit-event/v1';command=if($commandArguments.Count){$commandArguments[0]}else{''};stage='entry';status='failed';code=$code;fix=$fix;releaseReady=$false} | ConvertTo-Json -Compress
  $exitCode=1
} finally {
  foreach($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
  if($acquired) { $lease.ReleaseMutex() }
  $lease.Dispose()
}
exit $exitCode
