param([string]$Node,[string]$Controller,[string]$Architecture,[string]$ReferenceAttempt,[string]$ControllerAttempt,[string]$RunAttempt)
$ErrorActionPreference='Stop'
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation');$acquired=$false
try {
  try {$acquired=$lease.WaitOne(0)} catch [Threading.AbandonedMutexException] {$acquired=$true;throw 'Abandoned SOUI desktop validation lease'}
  if(-not $acquired){throw 'Another SOUI desktop validation is running'}
  & $Node $Controller inner $Architecture $ReferenceAttempt $ControllerAttempt $RunAttempt
  exit $LASTEXITCODE
} finally {if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
