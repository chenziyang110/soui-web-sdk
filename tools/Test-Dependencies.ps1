[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$KernelBuildReport,[string]$KernelOwnedCapture,[string]$SouiBuildReport,[string]$SouiOwnedCapture,
  [string]$ProfileRequest,[string]$DevkitRoot,[string]$BuildRunner,
  [string]$KernelProducerRoot,[string]$SouiProducerRoot,
  [string]$KernelInputArchive,[string]$SouiInputArchive)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($NativeToolchain -eq 'LLVM22MSVC142'){
  if(@($KernelBuildReport,$KernelOwnedCapture,$SouiBuildReport,$SouiOwnedCapture,$ProfileRequest,$BuildRunner)|Where-Object{[string]::IsNullOrWhiteSpace($_)}){
    throw 'LLVM dependency verification requires both actual reports/captures, profile request and build runner.'
  }
  if(-not $DevkitRoot){$DevkitRoot=$root}
  $producerArgs=@()
  if($KernelProducerRoot -or $SouiProducerRoot -or $KernelInputArchive -or $SouiInputArchive){
    if(-not $KernelProducerRoot){$KernelProducerRoot=$root}
    if(-not $SouiProducerRoot){$SouiProducerRoot=$root}
    $producerArgs=@($KernelProducerRoot,$SouiProducerRoot)
    if($KernelInputArchive -or $SouiInputArchive){
      if(-not $KernelInputArchive){$KernelInputArchive='-'}
      if(-not $SouiInputArchive){$SouiInputArchive='-'}
      $producerArgs+=@($KernelInputArchive,$SouiInputArchive)
    }
  }
  $lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation');$acquired=$false
  $previousNonce=Get-Item -LiteralPath Env:SOUI_TEST_DEPENDENCIES_NONCE -ErrorAction SilentlyContinue
  try{
    try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true;throw 'Abandoned SOUI desktop validation lease'}
    if(-not $acquired){throw 'Another desktop validation is running.'}
    $env:SOUI_TEST_DEPENDENCIES_NONCE=[Guid]::NewGuid().ToString('N')
    & node (Join-Path $PSScriptRoot 'internal/dependency-evidence.cjs') run $Architecture $KernelBuildReport $KernelOwnedCapture $SouiBuildReport $SouiOwnedCapture $ProfileRequest $DevkitRoot $BuildRunner @producerArgs
    if($LASTEXITCODE -ne 0){throw 'Rebuilt LLVM dependency validation failed; retain the original evidence.'}
  }finally{
    if($acquired){$lease.ReleaseMutex()};$lease.Dispose()
    if($null -ne $previousNonce){$env:SOUI_TEST_DEPENDENCIES_NONCE=$previousNonce.Value}else{Remove-Item -LiteralPath Env:SOUI_TEST_DEPENDENCIES_NONCE -ErrorAction SilentlyContinue}
  }
  return
}
if(@($KernelBuildReport,$KernelOwnedCapture,$SouiBuildReport,$SouiOwnedCapture,$ProfileRequest,$DevkitRoot,$BuildRunner,$KernelProducerRoot,$SouiProducerRoot,$KernelInputArchive,$SouiInputArchive)|Where-Object{$_}){
  throw 'Explicit LLVM dependency inputs require NativeToolchain LLVM22MSVC142.'
}
$deps=Join-Path $root "out/dependencies/$Architecture"
$kernelName=if($Architecture -eq 'x86'){'node.dll'}else{'miniblink.dll'}
$kernel=Join-Path $deps "miniblink/bin/$kernelName"
if(-not(Test-Path -LiteralPath $kernel)){throw 'Build the Miniblink source first.'}
$build=$null
foreach($file in @(Get-ChildItem -LiteralPath $deps -Filter 'build-*.json' -File | Sort-Object LastWriteTimeUtc -Descending)){
  $candidate=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
  if($candidate.schema -eq 'soui.dependency-build/v1' -and $candidate.architecture -eq $Architecture -and
      $candidate.status -eq 'built' -and $candidate.souiSourceStage){$build=$candidate;break}
}
if(-not $build){throw 'Build SOUI from an isolated source stage first.'}
$stage=[IO.Path]::GetFullPath([string]$build.souiSourceStage)
$allowed=[IO.Path]::GetFullPath($deps).TrimEnd('\')+'\'
if(-not $stage.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'SOUI build report points outside dependency output.'}
$souiFiles=@{}
foreach($name in @('soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource')){
  $matching=@($build.artifacts|Where-Object {$_.kind -eq $name})
  if($matching.Count -ne 1){throw "SOUI build report is missing $name"}
  $file=[IO.Path]::GetFullPath([string]$matching[0].path)
  if(-not $file.StartsWith($stage.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or
      -not(Test-Path -LiteralPath $file) -or
      (Get-FileHash -LiteralPath $file).Hash -ne $matching[0].sha256){throw "SOUI artifact does not match build report: $name"}
  $souiFiles[$name]=$file
}
$testRoot=Join-Path $deps ('runtime-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$runs=@()
$lease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$acquired=$false
try{
  try{$acquired=$lease.WaitOne(0)}catch [Threading.AbandonedMutexException]{$acquired=$true}
  if(-not $acquired){throw 'Another desktop validation is running.'}
  foreach($target in @('SouiStarter','SouiWorkbench')){
    $original=Join-Path $root "out/$Architecture/bin/Release/$target"
    $bin=Join-Path $testRoot $target
    Copy-Item -LiteralPath $original -Destination $bin -Recurse
    Copy-Item -LiteralPath $kernel -Destination (Join-Path $bin 'node.dll') -Force
    foreach($name in @('soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource')){
      Copy-Item -LiteralPath $souiFiles[$name] -Destination (Join-Path $bin "$name.dll") -Force
    }
    $arguments=if($target -eq 'SouiStarter'){@('--smoke-test')}else{@('--webview-smoke-test',"--arch=$Architecture",'--react')}
    $logPath=Join-Path $bin $(if($target -eq 'SouiStarter'){'smoke.log'}else{'webview-smoke.log'})
    if(Test-Path -LiteralPath $logPath){Remove-Item -LiteralPath $logPath}
    $process=Start-Process -FilePath (Join-Path $bin "$target.exe") -WorkingDirectory $bin -WindowStyle Hidden -ArgumentList $arguments -PassThru
    try{
      $finished=$process.WaitForExit(45000)
      if(-not $finished){$process.Kill();$process.WaitForExit()}
      $process.Refresh()
      $log=if(Test-Path -LiteralPath $logPath){[IO.File]::ReadAllText($logPath)}else{''}
      $runs+=@{target=$target;exitCode=$process.ExitCode;timedOut=(-not $finished);passed=($finished -and $process.ExitCode -eq 0 -and $log.Contains('application-ready') -and $log.Contains('host-close-complete'));log=$log}
    }finally{$process.Dispose()}
  }
}finally{if($acquired){$lease.ReleaseMutex()};$lease.Dispose()}
$hashes=@(Get-ChildItem -LiteralPath (Join-Path $testRoot 'SouiStarter') -Filter *.dll | ForEach-Object {@{file=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$report=[ordered]@{schema='soui.rebuilt-dependency-smoke/v1';architecture=$Architecture;releaseReady=$false;kernelAndSouiBuiltFromBundledSource=$true;passed=(@($runs|Where-Object {-not $_.passed}).Count -eq 0);runs=$runs;runtimeHashes=$hashes}
[IO.File]::WriteAllText((Join-Path $deps 'rebuilt-smoke.json'),($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$report|ConvertTo-Json -Depth 8
if(-not $report.passed){throw 'Fresh dependency runtime validation failed; fixed prebuilt files were not modified.'}
