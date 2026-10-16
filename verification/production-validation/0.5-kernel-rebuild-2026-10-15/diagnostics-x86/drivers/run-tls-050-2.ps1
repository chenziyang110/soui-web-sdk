param([ValidateSet('x64','x86')][string]$Architecture='x64',[string]$Attempt='2')
$ErrorActionPreference='Stop'
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
try {
  try {$acquired=$lease.WaitOne(0)} catch [Threading.AbandonedMutexException] {$acquired=$true;throw 'Abandoned desktop lease'}
  if(-not $acquired){throw 'Desktop lease is held by another validation'}
  & 'C:/Users/11034/AppData/Local/nvm/v24.14.0/node.exe' 'F:/work/SOUI_WEB/out/release-050/run-tls-050-2.cjs' $Architecture $Attempt
  $code=$LASTEXITCODE
} finally {if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
exit $code
