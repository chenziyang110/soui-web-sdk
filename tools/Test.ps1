[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
      [ValidateSet('Debug','Release')][string]$Configuration='Release',
      [switch]$NativeOnly,
      [switch]$UnitOnly)
$ErrorActionPreference='Stop'
if($NativeOnly -and $UnitOnly){throw 'NativeOnly and UnitOnly cannot be used together.'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=Join-Path $root "out/$Architecture"
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
if(-not $NativeOnly){
  $validationRunner=& (Join-Path $PSScriptRoot 'internal/Build-ValidationTools.ps1') -Architecture $Architecture
  if($LASTEXITCODE -ne 0){throw 'Owned process supervision tools build failed'}
  $previousOwnedRunner=$env:SOUI_OWNED_PROCESS_RUNNER
  $previousBenchmarkRunner=$env:BENCHMARK_OWNED_RUNNER_EXE
  try{
    $env:SOUI_OWNED_PROCESS_RUNNER=$validationRunner
    $env:BENCHMARK_OWNED_RUNNER_EXE=$validationRunner
    & node --test (Join-Path $root 'tests/benchmark/owned-process-runner.test.cjs') (Join-Path $root 'tests/benchmark/owned-process-client.test.cjs') (Join-Path $root 'tests/benchmark/owned-process-client-boundaries.test.cjs') (Join-Path $root 'tests/acceptance/owned-supervision-boundaries.test.cjs') (Join-Path $root 'tests/benchmark/run-one-cleanup.test.cjs') (Join-Path $root 'tests/sdk/cmake-sdk-selection.test.cjs')
    if($LASTEXITCODE -ne 0){throw 'Owned supervision or SDK selection tests failed'}
  }finally{
    $env:SOUI_OWNED_PROCESS_RUNNER=$previousOwnedRunner
    $env:BENCHMARK_OWNED_RUNNER_EXE=$previousBenchmarkRunner
  }
  & node --test (Join-Path $root 'tests/sdk/actiongen.test.mjs') (Join-Path $root 'tests/web/generated-actions.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Single-source action generation or generated Web contract tests failed'}
  & node (Join-Path $root 'tests/sdk/sdk-package-smoke.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK package tests failed'}
  & node --test (Join-Path $root 'tests/sdk/sdk-native-payload.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK native payload tests failed'}
  & node --test (Join-Path $root 'tests/sdk/installation-interruption.test.cjs') (Join-Path $root 'tests/sdk/profile-installation-integration.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'SDK installation interruption tests failed'}
  & node --test (Join-Path $root 'tests/sdk/delivery-identity.test.cjs') (Join-Path $root 'tests/sdk/release-verification.test.cjs') (Join-Path $root 'tests/sdk/stage-soui-source.test.cjs') (Join-Path $root 'tests/native/miniblink_header_abi.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Delivery identity tests failed'}
  & node --test (Join-Path $root 'tests/sdk/modern-evidence.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Modern delivery evidence tests failed'}
  & node --test (Join-Path $root 'tests/web/sdk-client.test.mjs') (Join-Path $root 'tests/web/capabilities.test.mjs') (Join-Path $root 'tests/web/action-contracts.test.mjs') (Join-Path $root 'tests/web/gui.test.mjs') (Join-Path $root 'tests/web/native-resolvers.test.mjs') (Join-Path $root 'tests/acceptance/runner.test.cjs') (Join-Path $root 'tests/acceptance/monotonic-supervisor.test.cjs') (Join-Path $root 'tests/acceptance/mixed-validation.test.cjs') (Join-Path $root 'tests/acceptance/mixed-analysis.test.cjs') (Join-Path $root 'tests/acceptance/timeout-diagnostics.test.cjs') (Join-Path $root 'tests/benchmark/page-pair.test.cjs') (Join-Path $root 'tests/benchmark/run-traces.test.cjs')
  if($LASTEXITCODE -ne 0){throw 'Web SDK, acceptance, or benchmark protocol tests failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') protocol $Architecture
  if($LASTEXITCODE -ne 0){throw 'Measurement protocol reader regressions failed'}
  & node (Join-Path $root 'tools/internal/measurement-validation.cjs') native $Architecture
  if($LASTEXITCODE -ne 0){throw 'Native measurement identity or owned lifecycle regressions failed'}
  & node --test (Join-Path $root 'tests/web/web-features.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Web runtime feature tests failed'}
  & node --test (Join-Path $root 'tests/web/session-renewal.test.mjs') (Join-Path $root 'tests/web/agent-session-renewal.test.mjs') (Join-Path $root 'tests/web/webview2-preload.test.mjs')
  if($LASTEXITCODE -ne 0){throw 'Web session renewal tests failed'}
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
  $metadataText=& node $evidence begin-smoke $Architecture $Configuration
  if($LASTEXITCODE -ne 0){throw 'Current build must be verified before desktop testing'}
  $metadata=$metadataText | ConvertFrom-Json
  foreach($scenario in $metadata.scenarios){
    $target=$scenario.target
    $bin=Join-Path $output "bin/$Configuration/$target"
    $exe=Join-Path $bin "$target.exe"
    $arguments=if($target -eq 'SouiWorkbench'){@('--webview-smoke-test',"--arch=$Architecture")}else{@('--smoke-test')}
    if($scenario.scenario -eq 'react'){$arguments+='--react'}
    $log=Join-Path $bin $(if($target -eq 'SouiWorkbench'){'webview-smoke.log'}else{'smoke.log'})
    if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
    $process=Start-Process -FilePath $exe -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList $arguments -PassThru
    try{
      $finished=$process.WaitForExit(45000)
      if(-not $finished){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}
      $process.Refresh()
      $text=if(Test-Path -LiteralPath $log){[IO.File]::ReadAllText($log)}else{''}
      $ok=$finished -and $process.ExitCode -eq 0 -and $text.Contains('application-ready') -and $text.Contains('host-close-complete')
      $runs+=@{target=$target;scenario=$scenario.scenario;passed=$ok;exitCode=$process.ExitCode;timedOut=(-not $finished);log=$text}
    }finally{$process.Dispose()}
  }
}finally{if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
$report=[ordered]@{schema=$metadata.schema;buildId=$metadata.buildId;sourceSha256=$metadata.sourceSha256;version=$metadata.version;startedAt=$metadata.startedAt;architecture=$Architecture;configuration=$Configuration;releaseReady=$false;runs=$runs;passed=$false}
[IO.File]::WriteAllText((Join-Path $output "smoke-$Configuration.json"),($report|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
& node $evidence finish-smoke $Architecture $Configuration
if($LASTEXITCODE -ne 0){throw 'Native startup/bridge/close validation or payload identity failed.'}
