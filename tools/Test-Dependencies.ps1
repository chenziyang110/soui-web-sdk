[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
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
