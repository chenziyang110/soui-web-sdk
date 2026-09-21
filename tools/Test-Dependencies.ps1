[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$deps=Join-Path $root "out/dependencies/$Architecture"
$kernelName=if($Architecture -eq 'x86'){'node.dll'}else{'miniblink.dll'}
$kernel=Join-Path $deps "miniblink/bin/$kernelName"
$souiBin=Join-Path $root "third_party/sources/soui/bin$(if($Architecture -eq 'x64'){'64'})"
if(-not(Test-Path -LiteralPath $kernel)){throw 'Build the Miniblink source first.'}
foreach($name in @('soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource')){
  if(-not(Test-Path -LiteralPath (Join-Path $souiBin "$name.dll"))){throw "Build SOUI first: missing $name"}
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
      Copy-Item -LiteralPath (Join-Path $souiBin "$name.dll") -Destination (Join-Path $bin "$name.dll") -Force
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
