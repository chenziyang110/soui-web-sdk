[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$BuildDirectory,
  [ValidateSet('all','reload','recreate')][string]$Scenario='all',
  [ValidateRange(1,1000)][int]$Cycles=3,
  [ValidateRange(1000,43200000)][int]$TimeoutMs=120000,
  [ValidateRange(1,120000)][int]$StepTimeoutMs=20000
)
$ErrorActionPreference='Stop'
$suite=[IO.Path]::GetFullPath($BuildDirectory)
$evidence=Join-Path $PSScriptRoot 'evidence.cjs'
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$failed=$false
try {
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  $modes=if($Scenario -eq 'all'){@('reload','recreate')}else{@($Scenario)}
  foreach($mode in $modes){
    & node.exe $evidence run $suite $mode $Cycles $TimeoutMs $StepTimeoutMs
    if($LASTEXITCODE -ne 0){$failed=$true}
  }
} finally {
  if($acquired){$lease.ReleaseMutex()}
  $lease.Dispose()
}
if($failed){throw 'Experimental WebView2 validation failed; retained reports are under the build directory runs/.'}
Write-Output 'Experimental native scenarios completed; production, resource and process-tree gates remain unevaluated.'
