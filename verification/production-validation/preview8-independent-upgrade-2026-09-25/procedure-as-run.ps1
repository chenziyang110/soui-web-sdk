[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project='C:\SOUI-SDK-production-validation\preview8-upgrade-project-20260925'
$evidence='C:\SOUI-SDK-production-validation\preview8-upgrade-evidence-20260925'
$oldPackage='C:\SOUI-SDK-production-validation\preview7-final-source-clean-check-20260925\dist\sdk\0.3.0-preview.7'
$newPackage='C:\SOUI-SDK-production-validation\preview8-isolated-source-x64-20260925\dist\sdk\0.3.0-preview.8'
$tool=Join-Path $project 'tools/sdk/soui-sdk.cjs'
$marker='business-marker=customer-state-2026'
if(Test-Path -LiteralPath $evidence){throw 'Evidence directory already exists'}
New-Item -ItemType Directory -Path $evidence | Out-Null
$owned=@('app/main.cpp','app/business_marker.h')
$business=@{}
foreach($relative in $owned){$business[$relative]=(Get-FileHash -LiteralPath (Join-Path $project $relative) -Algorithm SHA256).Hash}
function Assert-Business {
  foreach($relative in $owned){
    if((Get-FileHash -LiteralPath (Join-Path $project $relative) -Algorithm SHA256).Hash -ne $business[$relative]){
      throw "Business file changed: $relative"
    }
  }
}
foreach($package in @($oldPackage,$newPackage)){
  & node $tool verify --package $package | Out-Null
  if($LASTEXITCODE -ne 0){throw "Package verification failed: $package"}
}
function Validate-Stage([string]$name,[string]$version){
  $stage=Join-Path $evidence $name
  New-Item -ItemType Directory -Path $stage | Out-Null
  & node $tool verify-project --project $project *> (Join-Path $stage 'verify-project.log')
  if($LASTEXITCODE -ne 0){throw "SDK lock verification failed: $name"}
  $lock=Get-Content -LiteralPath (Join-Path $project 'sdk-lock.json') -Raw|ConvertFrom-Json
  if($lock.manifest.version -ne $version){throw "Unexpected SDK version: $name"}
  Assert-Business
  Copy-Item -LiteralPath (Join-Path $project 'sdk-lock.json') -Destination (Join-Path $stage 'sdk-lock.json')
  $archResults=@()
  foreach($arch in @('x86','x64')){
    $dir=Join-Path $stage $arch
    New-Item -ItemType Directory -Path $dir | Out-Null
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $project 'tools/Build.ps1') -Architecture $arch -Configuration Release *> (Join-Path $dir 'build.log')
    if($LASTEXITCODE -ne 0){throw "Build failed: $name/$arch"}
    $metadataText=& node (Join-Path $project 'tools/internal/build-evidence.cjs') begin-smoke $arch Release
    if($LASTEXITCODE -ne 0){throw "Smoke evidence could not begin: $name/$arch"}
    $metadata=$metadataText|ConvertFrom-Json
    if($metadata.scenarios.Count -ne 1 -or $metadata.scenarios[0].target -ne 'UpgradeProbe'){
      throw "Unexpected smoke scenario: $name/$arch"
    }
    $bin=Join-Path $project "out/$arch/bin/Release/UpgradeProbe"
    $exe=Join-Path $bin 'UpgradeProbe.exe'
    $log=Join-Path $bin 'smoke.log'
    if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
    $process=Start-Process -FilePath $exe -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList '--smoke-test' -PassThru
    try{
      $done=$process.WaitForExit(45000)
      if(-not $done){$process.Kill();$process.WaitForExit()}
      $process.Refresh()
      $body=if(Test-Path -LiteralPath $log){[IO.File]::ReadAllText($log)}else{''}
      $ok=$done -and $process.ExitCode -eq 0 -and $body.Contains($marker) -and
        $body.Contains('application-ready') -and $body.Contains('host-close-complete')
      $run=[ordered]@{target='UpgradeProbe';scenario='default';passed=$ok;exitCode=$process.ExitCode;timedOut=(-not $done);log=$body}
    }finally{$process.Dispose()}
    if(-not $ok){throw "Real smoke failed: $name/$arch"}
    $smoke=[ordered]@{schema=$metadata.schema;buildId=$metadata.buildId;sourceSha256=$metadata.sourceSha256;
      version=$metadata.version;startedAt=$metadata.startedAt;architecture=$arch;configuration='Release';
      releaseReady=$false;runs=@($run);passed=$false}
    [IO.File]::WriteAllText((Join-Path $project "out/$arch/smoke-Release.json"),($smoke|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
    & node (Join-Path $project 'tools/internal/build-evidence.cjs') finish-smoke $arch Release *> (Join-Path $dir 'test.log')
    if($LASTEXITCODE -ne 0){throw "Smoke identity failed: $name/$arch"}
    foreach($file in @("out/$arch/build-Release.json","out/$arch/smoke-Release.json")){
      Copy-Item -LiteralPath (Join-Path $project $file) -Destination (Join-Path $dir ([IO.Path]::GetFileName($file)))
    }
    Copy-Item -LiteralPath $log -Destination (Join-Path $dir 'smoke.log')
    $build=Get-Content -LiteralPath (Join-Path $dir 'build-Release.json') -Raw|ConvertFrom-Json
    $smokeResult=Get-Content -LiteralPath (Join-Path $dir 'smoke-Release.json') -Raw|ConvertFrom-Json
    if($build.nativeBuild -ne 'passed' -or $smokeResult.passed -ne $true -or $build.buildId -ne $smokeResult.buildId){
      throw "Build/smoke identity mismatch: $name/$arch"
    }
    Assert-Business
    $archResults+=@{architecture=$arch;buildId=$build.buildId;sourceSha256=$build.source.sha256;
      exeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;smokePassed=$true}
  }
  return @{stage=$name;sdkVersion=$version;architectures=$archResults;businessFilesPreserved=$true}
}
$stages=@()
$stages+=Validate-Stage 'old-initial' '0.3.0-preview.7'
& node $tool install --package $newPackage --project $project *> (Join-Path $evidence 'upgrade.log')
if($LASTEXITCODE -ne 0){throw 'Upgrade failed'}
$stages+=Validate-Stage 'new-upgraded' '0.3.0-preview.8'
& node $tool install --package $oldPackage --project $project *> (Join-Path $evidence 'rollback.log')
if($LASTEXITCODE -ne 0){throw 'Rollback failed'}
$stages+=Validate-Stage 'old-rolled-back' '0.3.0-preview.7'
& node $tool install --package $newPackage --project $project *> (Join-Path $evidence 'reupgrade.log')
if($LASTEXITCODE -ne 0){throw 'Re-upgrade failed'}
$stages+=Validate-Stage 'new-restored' '0.3.0-preview.8'
$result=[ordered]@{schema='soui.upgrade-trial/v2';oldVersion='0.3.0-preview.7';newVersion='0.3.0-preview.8';
  projectName='UpgradeProbe';businessFiles=$business;stages=$stages;passed=$true;releaseReady=$false;
  finishedAt=(Get-Date).ToString('o')}
[IO.File]::WriteAllText((Join-Path $evidence 'result.json'),($result|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Output "Upgrade chain passed: $evidence"
