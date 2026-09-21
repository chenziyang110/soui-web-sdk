[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
      [ValidateSet('Debug','Release')][string]$Configuration='Release',
      [switch]$NativeOnly,
      [switch]$UnitOnly)
$ErrorActionPreference='Stop'
if($NativeOnly -and $UnitOnly){throw 'NativeOnly and UnitOnly cannot be used together.'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=Join-Path $root "out/$Architecture"
if(-not $NativeOnly){
  & node (Join-Path $root 'tests/sdk/sdk-package-smoke.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK package tests failed'}
  & node --test (Join-Path $root 'tests/web/sdk-client.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Web SDK tests failed'}
  & ctest --test-dir $output -C $Configuration --output-on-failure
  if($LASTEXITCODE -ne 0){throw 'Native SDK contract tests failed'}
}
if($UnitOnly){Write-Output 'Unit and contract tests passed; desktop smoke was intentionally skipped.';exit 0}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
$runs=@()
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  foreach($target in @('SouiStarter','SouiWorkbench')){
    $bin=Join-Path $output "bin/$Configuration/$target"
    $exe=Join-Path $bin "$target.exe"
    $arguments=if($target -eq 'SouiStarter'){@('--smoke-test')}else{@('--webview-smoke-test',"--arch=$Architecture",'--react')}
    $log=Join-Path $bin $(if($target -eq 'SouiStarter'){'smoke.log'}else{'webview-smoke.log'})
    $process=Start-Process -FilePath $exe -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList $arguments -PassThru
    try{
      $finished=$process.WaitForExit(45000)
      if(-not $finished){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}
      $process.Refresh()
      $text=if(Test-Path -LiteralPath $log){[IO.File]::ReadAllText($log)}else{''}
      $ok=$finished -and $process.ExitCode -eq 0 -and $text.Contains('application-ready') -and $text.Contains('host-close-complete')
      $runs+=@{target=$target;passed=$ok;exitCode=$process.ExitCode;timedOut=(-not $finished);executableSha256=(Get-FileHash -LiteralPath $exe).Hash;kernelSha256=(Get-FileHash -LiteralPath (Join-Path $bin 'node.dll')).Hash;log=$text}
    }finally{$process.Dispose()}
  }
}finally{if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
$report=[ordered]@{schema='soui.native-smoke/v1';architecture=$Architecture;configuration=$Configuration;releaseReady=$false;runs=$runs;passed=(@($runs|Where-Object {-not $_.passed}).Count -eq 0)}
[IO.File]::WriteAllText((Join-Path $output "smoke-$Configuration.json"),($report|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
$report|ConvertTo-Json -Depth 7
if(-not $report.passed){throw 'Native startup/bridge/close validation failed.'}
