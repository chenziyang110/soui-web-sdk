[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
      [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=Join-Path $root "out/$Architecture"
$reportPath=Join-Path $output "modern-smoke-$Configuration.json"
New-Item -ItemType Directory -Force -Path $output | Out-Null
[IO.File]::WriteAllText($reportPath,'{"passed":false,"releaseReady":false,"status":"running"}',[Text.UTF8Encoding]::new($false))
$metadataText=& node (Join-Path $root 'tools/internal/build-evidence.cjs') verify-build $Architecture $Configuration
if($LASTEXITCODE -ne 0){throw 'Current build identity is required.'}
$build=$metadataText | ConvertFrom-Json
$target='SouiModernStarter'
$payload=$build.optionalPayloads.SouiModernStarter
if(Test-Path -LiteralPath (Join-Path $root 'project.json')){
  $project=Get-Content -Raw -LiteralPath (Join-Path $root 'project.json') | ConvertFrom-Json
  if($project.runtimeProfile -ne 'modern'){throw 'This project does not use the modern profile.'}
  $target=$project.nativeTarget
  $payload=$build.payloads.$target
}
if(-not $payload -or -not $build.optionalLibraries.webview2){throw 'Build with -WithWebView2 before running the modern template.'}
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
try {
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another SOUI desktop validation is running.'}
  $exe=Join-Path $output "bin/$Configuration/$target/$target.exe"
  $process=Start-Process -FilePath $exe -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
  try {
    $finished=$process.WaitForExit(40000)
    if(-not $finished){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}
    $process.Refresh()
    $logPath=Join-Path $env:LOCALAPPDATA ($target+'/smoke-'+$process.Id+'/smoke.log')
    $log=if(Test-Path -LiteralPath $logPath){[IO.File]::ReadAllText($logPath)}else{''}
    $passed=$finished -and $process.ExitCode -eq 0 -and $log.Contains('application-ready') -and $log.Contains('host-close-complete')
    & node (Join-Path $root 'tools/internal/build-evidence.cjs') verify-build $Architecture $Configuration | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Build or payload changed during native execution.'}
    $report=[ordered]@{schema='soui.modern-smoke/1';version=$build.version;buildId=$build.buildId;
      sourceSha256=$build.source.sha256;architecture=$Architecture;configuration=$Configuration;
      target=$target;payload=$payload;pid=$process.Id;exitCode=$process.ExitCode;
      timedOut=(-not $finished);log=$log;passed=$passed;releaseReady=$false;finishedAt=[DateTimeOffset]::UtcNow.ToString('o')}
    [IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    if(-not $passed){throw "Modern application failed; inspect $reportPath"}
  } finally {$process.Dispose()}
} finally {if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
Write-Output "Modern application verified: $Architecture/$Configuration"
