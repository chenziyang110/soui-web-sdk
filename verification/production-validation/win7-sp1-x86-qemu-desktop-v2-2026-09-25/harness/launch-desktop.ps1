$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$transfer=Join-Path $root 'guest-desktop-v2-transfer-20260925'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk=Join-Path $root 'system.qcow2'
$serial=Join-Path $root 'desktop-v2-serial-20260925.txt'
$stdout=Join-Path $root 'desktop-v2-qemu-out-20260925.log'
$stderr=Join-Path $root 'desktop-v2-qemu-err-20260925.log'
$launch=Join-Path $root 'desktop-v2-qemu-launch-20260925.json'
foreach($path in @($qemu,$disk,(Join-Path $transfer 'soui_desktop_unicode_tests.exe'),
    (Join-Path $transfer 'input.json'),(Join-Path $transfer 'run-desktop.cmd'),
    (Join-Path $transfer 'export-serial.ps1'))){
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing QEMU input: $path"}
}
if(-not (Test-Path -LiteralPath (Join-Path $root 'mixed-soak-20260924\status.json') -PathType Leaf)){
  throw 'The host eight-hour validation has no terminal result yet'
}
if(Get-Process -Name SouiAcceptance,qemu-system-i386,qemu-system-x86_64 -ErrorAction SilentlyContinue){
  throw 'Another host acceptance or QEMU process is still running'
}
foreach($path in @($serial,$stdout,$stderr,$launch)){
  if(Test-Path -LiteralPath $path){throw "QEMU output already exists: $path"}
}
$inputRecord=Get-Content -LiteralPath (Join-Path $transfer 'input.json') -Raw | ConvertFrom-Json
if($inputRecord.schema -ne 'soui.qemu-desktop-input/v2' -or
    $inputRecord.releaseReady -ne $false -or $inputRecord.runtimeFiles.Count -ne 52){
  throw 'Unexpected desktop v2 frozen input'
}
function Same-Text([string]$leftPath,[string]$rightPath){
  $left=[IO.File]::ReadAllText($leftPath).Replace("`r`n","`n")
  $right=[IO.File]::ReadAllText($rightPath).Replace("`r`n","`n")
  return [string]::Equals($left,$right,[StringComparison]::Ordinal)
}
if(-not (Same-Text (Join-Path $transfer 'input.json') (Join-Path $PSScriptRoot '..\input.json'))){
  throw 'The desktop guest input record differs from the committed harness'
}
foreach($name in @('run-desktop.cmd','export-serial.ps1')){
  if(-not (Same-Text (Join-Path $transfer $name) (Join-Path $PSScriptRoot $name))){
    throw "The guest script differs from the committed harness: $name"
  }
  if((Get-FileHash -LiteralPath (Join-Path $transfer $name) -Algorithm SHA256).Hash -cne
      $inputRecord.scripts.$name){throw "Guest script hash differs: $name"}
}
$exe=Join-Path $transfer 'soui_desktop_unicode_tests.exe'
if((Get-Item -LiteralPath $exe).Length -ne $inputRecord.testExeBytes -or
    (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $inputRecord.testExeSha256){
  throw 'The desktop guest executable differs from its frozen input record'
}
foreach($entry in $inputRecord.runtimeFiles){
  if($entry.path -cnotmatch '^[A-Za-z0-9_.-]+\.dll$'){throw 'Invalid app-local runtime file name'}
  $file=Join-Path $transfer $entry.path
  if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or
      (Get-Item -LiteralPath $file).Length -ne $entry.bytes -or
      (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -cne $entry.sha256.ToUpperInvariant()){
    throw "App-local runtime changed: $($entry.path)"
  }
}
if(@(Get-ChildItem -LiteralPath $transfer -File).Count -ne ($inputRecord.runtimeFiles.Count+4)){
  throw 'Desktop v2 transfer file count differs from frozen input'
}
$arguments=@('-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
  '-drive',"file=$disk,format=qcow2,if=ide",
  '-drive',"file=fat:ro:$transfer,format=raw,if=ide,index=1",
  '-snapshot','-net','none','-vga','std','-display','none',
  '-vnc','127.0.0.1:97','-monitor','tcp:127.0.0.1:4449,server,nowait',
  '-serial',"file:$serial")
$process=Start-Process -FilePath $qemu -ArgumentList $arguments -PassThru -WindowStyle Hidden `
  -WorkingDirectory $root -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$record=[ordered]@{pid=$process.Id;startedAt=[DateTimeOffset]::UtcNow.ToString('o');arguments=$arguments;
  sourceCommit=$inputRecord.sourceCommit;testExeSha256=$inputRecord.testExeSha256;releaseReady=$false}
[IO.File]::WriteAllText($launch,($record | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output ("QEMU desktop guest launched: PID $($process.Id). Verify the Win7 desktop, then run run-desktop-keys.ps1.")
