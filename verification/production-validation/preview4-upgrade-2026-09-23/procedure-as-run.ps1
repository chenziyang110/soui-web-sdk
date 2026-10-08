[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot)

$ErrorActionPreference='Stop'
$sdkRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $project){throw 'Use a new project directory; no existing data is overwritten.'}
if($project.StartsWith($sdkRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){
  throw 'The test project must be outside the SDK source tree.'
}
$oldVersion='0.3.0-preview.3'
$newVersion='0.3.0-preview.4'
$oldPackage=Join-Path $sdkRoot "dist/sdk/$oldVersion"
$newPackage=Join-Path $sdkRoot "dist/sdk/$newVersion"
$installer=Join-Path $sdkRoot 'tools/sdk/soui-sdk.cjs'
$evidence=$project+'-evidence'
if(Test-Path -LiteralPath $evidence){throw 'Use a fresh evidence directory.'}
$desktopLease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$desktopAvailable=$false
try {
  while(-not $desktopAvailable) {
    try {$desktopAvailable=$desktopLease.WaitOne(5000)}
    catch [Threading.AbandonedMutexException] {$desktopAvailable=$true}
  }
  $desktopLease.ReleaseMutex()
} finally {$desktopLease.Dispose()}
New-Item -ItemType Directory -Path $evidence | Out-Null

foreach($package in @($oldPackage,$newPackage)) {
  & node $installer verify --package $package
  if($LASTEXITCODE -ne 0){throw "SDK package failed verification: $package"}
}
& (Join-Path $sdkRoot 'tools/New-Project.ps1') -Name ProductTrial -OutputRoot $project -SdkPackage $oldPackage *> (Join-Path $evidence 'create.log')
if($LASTEXITCODE -ne 0){throw 'Independent project creation failed'}

$cmakeFile=Join-Path $project 'CMakeLists.txt'
$cmakeText=[IO.File]::ReadAllText($cmakeFile)
if(-not $cmakeText.Contains('SOURCES app/main.cpp)')){throw 'Template CMake entry changed'}
[IO.File]::WriteAllText($cmakeFile,$cmakeText.Replace('SOURCES app/main.cpp)','SOURCES app/main.cpp app/business_mark.cpp)'),[Text.UTF8Encoding]::new($false))

$header=Join-Path $project 'app/business_mark.h'
$source=Join-Path $project 'app/business_mark.cpp'
[IO.File]::WriteAllText($header,"#pragma once`nconst char* product_trial_marker() noexcept;`n",[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($source,"#include `"business_mark.h`"`nconst char* product_trial_marker() noexcept { return `"business-marker-preview4-upgrade`"; }`n",[Text.UTF8Encoding]::new($false))

$mainFile=Join-Path $project 'app/main.cpp'
$mainText=[IO.File]::ReadAllText($mainFile)
if(-not $mainText.Contains('#include <algorithm>') -or -not $mainText.Contains('window.append("host-started");')){
  throw 'Template startup markers changed'
}
$mainText=$mainText.Replace('#include <algorithm>','#include <algorithm>' + "`n" + '#include "business_mark.h"')
$mainText=$mainText.Replace('window.append("host-started");','window.append("host-started");' + "`n" + '  window.append(product_trial_marker());')
[IO.File]::WriteAllText($mainFile,$mainText,[Text.UTF8Encoding]::new($false))

$webFile=Join-Path $project 'web/src/main.tsx'
$webText=[IO.File]::ReadAllText($webFile)
if(-not $webText.Contains('从这里开始构建你的桌面应用。')){throw 'Template Web content changed'}
[IO.File]::WriteAllText($webFile,$webText.Replace('从这里开始构建你的桌面应用。','Business Trial Workspace'),[Text.UTF8Encoding]::new($false))

$businessFiles=@($cmakeFile,$header,$source,$mainFile,$webFile)
$businessHashes=@{}
foreach($file in $businessFiles){$businessHashes[$file.Substring($project.Length+1).Replace('\','/')]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
function Assert-BusinessFiles {
  foreach($file in $businessFiles){
    $relative=$file.Substring($project.Length+1).Replace('\','/')
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $businessHashes[$relative]){
      throw "Business file changed during SDK install: $relative"
    }
  }
}
function Validate-Stage([string]$stage,[string]$expectedVersion) {
  $stageDir=Join-Path $evidence $stage
  New-Item -ItemType Directory -Path $stageDir | Out-Null
  & node (Join-Path $project 'tools/sdk/soui-sdk.cjs') verify-project --project $project *> (Join-Path $stageDir 'verify-project.log')
  if($LASTEXITCODE -ne 0){throw "Project lock verification failed: $stage"}
  $lock=Get-Content -LiteralPath (Join-Path $project 'sdk-lock.json') -Raw|ConvertFrom-Json
  if($lock.manifest.version -ne $expectedVersion){throw "Unexpected SDK version at $stage"}
  Assert-BusinessFiles
  & (Join-Path $project 'tools/Build.ps1') -Architecture x64 -Configuration Release *> (Join-Path $stageDir 'build.log')
  if($LASTEXITCODE -ne 0){throw "Independent build failed: $stage"}
  $smokeLog=Join-Path $project 'out/x64/bin/Release/ProductTrial/smoke.log'
  $evidenceTool=Join-Path $project 'tools/internal/build-evidence.cjs'
  $metadataText=& node $evidenceTool begin-smoke x64 Release
  if($LASTEXITCODE -ne 0){throw "Smoke evidence could not begin: $stage"}
  $metadata=$metadataText|ConvertFrom-Json
  if($metadata.scenarios.Count -ne 1 -or $metadata.scenarios[0].target -ne 'ProductTrial'){
    throw "Unexpected project smoke scenarios: $stage"
  }
  [IO.File]::WriteAllText($smokeLog,'',[Text.UTF8Encoding]::new($false))
  $bin=Join-Path $project 'out/x64/bin/Release/ProductTrial'
  $process=Start-Process -FilePath (Join-Path $bin 'ProductTrial.exe') -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList @('--smoke-test') -PassThru
  try {
    $finished=$process.WaitForExit(45000)
    if(-not $finished){Stop-Process -Id $process.Id -Force;$process.WaitForExit()}
    $process.Refresh()
    $smoke=[IO.File]::ReadAllText($smokeLog)
    $run=[ordered]@{target='ProductTrial';scenario='default';passed=($finished -and $process.ExitCode -eq 0 -and $smoke.Contains('application-ready') -and $smoke.Contains('host-close-complete'));exitCode=$process.ExitCode;timedOut=(-not $finished);log=$smoke}
  } finally {$process.Dispose()}
  $smokeReport=[ordered]@{schema=$metadata.schema;buildId=$metadata.buildId;sourceSha256=$metadata.sourceSha256;version=$metadata.version;startedAt=$metadata.startedAt;architecture='x64';configuration='Release';releaseReady=$false;runs=@($run);passed=$false}
  [IO.File]::WriteAllText((Join-Path $project 'out/x64/smoke-Release.json'),($smokeReport|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
  & node $evidenceTool finish-smoke x64 Release *> (Join-Path $stageDir 'test.log')
  if($LASTEXITCODE -ne 0){throw "Real startup/close failed: $stage"}
  $smoke=[IO.File]::ReadAllText($smokeLog)
  if(-not $smoke.Contains('business-marker-preview4-upgrade') -or
     -not $smoke.Contains('application-ready') -or -not $smoke.Contains('host-close-complete')){
    throw "Business marker or startup/close missing: $stage"
  }
  Assert-BusinessFiles
  foreach($name in @('sdk-lock.json','out/x64/build-Release.json','out/x64/smoke-Release.json')){
    Copy-Item -LiteralPath (Join-Path $project $name) -Destination (Join-Path $stageDir ([IO.Path]::GetFileName($name)))
  }
  Copy-Item -LiteralPath $smokeLog -Destination (Join-Path $stageDir 'smoke.log')
  $build=Get-Content (Join-Path $stageDir 'build-Release.json') -Raw|ConvertFrom-Json
  $result=Get-Content (Join-Path $stageDir 'smoke-Release.json') -Raw|ConvertFrom-Json
  if($build.nativeBuild -ne 'passed' -or $result.passed -ne $true -or $result.buildId -ne $build.buildId){
    throw "Build/smoke identity mismatch: $stage"
  }
  return [ordered]@{stage=$stage;version=$expectedVersion;buildId=$build.buildId;sourceSha256=$build.source.sha256;smokePassed=$true;businessFilesPreserved=$true}
}

$stages=@()
$stages+=Validate-Stage 'old-initial' $oldVersion
& node $installer install --package $newPackage --project $project *> (Join-Path $evidence 'upgrade.log')
if($LASTEXITCODE -ne 0){throw 'SDK upgrade failed'}
$stages+=Validate-Stage 'new-upgraded' $newVersion
& node $installer install --package $oldPackage --project $project *> (Join-Path $evidence 'rollback.log')
if($LASTEXITCODE -ne 0){throw 'SDK rollback failed'}
$stages+=Validate-Stage 'old-rolled-back' $oldVersion
& node $installer install --package $newPackage --project $project *> (Join-Path $evidence 'reupgrade.log')
if($LASTEXITCODE -ne 0){throw 'SDK re-upgrade failed'}
$stages+=Validate-Stage 'new-restored' $newVersion
$report=[ordered]@{schema='soui.upgrade-trial/v1';oldVersion=$oldVersion;newVersion=$newVersion;projectName='ProductTrial';businessFiles=$businessHashes;stages=$stages;passed=$true;releaseReady=$false;finishedAt=(Get-Date).ToString('o')}
[IO.File]::WriteAllText((Join-Path $evidence 'result.json'),($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output "Upgrade/rollback/re-upgrade trial passed: $evidence"
