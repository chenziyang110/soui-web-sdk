$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$transfer=Join-Path $root 'guest-public-task-transfer-20260925'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk=Join-Path $root 'system.qcow2'
$serial=Join-Path $root 'public-task-serial-20260925.txt'
$stdout=Join-Path $root 'public-task-qemu-out-20260925.log'
$stderr=Join-Path $root 'public-task-qemu-err-20260925.log'
$launch=Join-Path $root 'public-task-qemu-launch-20260925.json'
foreach($path in @($qemu,$disk,(Join-Path $transfer 'SouiAcceptance.zip'),(Join-Path $transfer 'input.json'),
    (Join-Path $transfer 'run-public-task.cmd'))){
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing QEMU input: $path"}
}
if(-not (Test-Path -LiteralPath (Join-Path $root 'mixed-soak-20260924\status.json') -PathType Leaf)){
  throw 'The host eight-hour validation has no terminal result yet'
}
if(Get-Process -Name SouiAcceptance,qemu-system-i386 -ErrorAction SilentlyContinue){
  throw 'Another host acceptance or QEMU process is still running'
}
foreach($path in @($serial,$stdout,$stderr,$launch)){
  if(Test-Path -LiteralPath $path){throw "QEMU output already exists: $path"}
}
$inputRecord=Get-Content -LiteralPath (Join-Path $transfer 'input.json') -Raw | ConvertFrom-Json
function Same-Text([string]$leftPath,[string]$rightPath){
  $left=[IO.File]::ReadAllText($leftPath).Replace("`r`n","`n")
  $right=[IO.File]::ReadAllText($rightPath).Replace("`r`n","`n")
  return [string]::Equals($left,$right,[StringComparison]::Ordinal)
}
if(-not (Same-Text (Join-Path $transfer 'input.json') (Join-Path $PSScriptRoot '..\input.json'))){
  throw 'The guest input record differs from the committed harness'
}
foreach($name in @('run-public-task.cmd','export-serial.ps1')){
  if(-not (Same-Text (Join-Path $transfer $name) (Join-Path $PSScriptRoot $name))){
    throw "The guest script differs from the committed harness: $name"
  }
}
if((Get-FileHash -LiteralPath (Join-Path $transfer '7za.exe') -Algorithm SHA256).Hash -cne $inputRecord.sevenZipSha256){
  throw 'The guest archive tool differs from its frozen input record'
}
if((Get-FileHash -LiteralPath (Join-Path $transfer 'SouiAcceptance.zip') -Algorithm SHA256).Hash -cne $inputRecord.zipSha256){
  throw 'The public-task ZIP differs from its frozen input record'
}
$arguments=@('-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
  '-drive',"file=$disk,format=qcow2,if=ide",
  '-drive',"file=fat:ro:$transfer,format=raw,if=ide,index=1",
  '-snapshot','-net','none','-vga','std','-display','none',
  '-vnc','127.0.0.1:99','-monitor','tcp:127.0.0.1:4445,server,nowait',
  '-serial',"file:$serial")
$process=Start-Process -FilePath $qemu -ArgumentList $arguments -PassThru -WindowStyle Hidden `
  -WorkingDirectory $root -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$record=[ordered]@{pid=$process.Id;startedAt=[DateTimeOffset]::UtcNow.ToString('o');arguments=$arguments;
  sourceSha256=$inputRecord.sourceSha256;zipSha256=$inputRecord.zipSha256;releaseReady=$false}
[IO.File]::WriteAllText($launch,($record | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$client=$null
for($attempt=0;$attempt -lt 20 -and $null -eq $client;$attempt++){
  try {$client=[Net.Sockets.TcpClient]::new('127.0.0.1',4445)}
  catch {Start-Sleep -Milliseconds 500}
}
if($null -eq $client){throw 'QEMU monitor did not become available; inspect the QEMU log'}
try {
  $stream=$client.GetStream()
  $writer=[IO.StreamWriter]::new($stream,[Text.Encoding]::ASCII)
  $writer.AutoFlush=$true
  $writer.WriteLine('sendkey meta_l-r')
  Start-Sleep -Milliseconds 900
  foreach($character in 'd:\run-public-task.cmd'.ToCharArray()){
    $key=if($character -eq ':'){'shift-semicolon'} elseif($character -eq [char]92){'backslash'}
      elseif($character -eq '-'){'minus'} elseif($character -eq '.'){'dot'} else{[string]$character}
    $writer.WriteLine('sendkey '+$key)
    Start-Sleep -Milliseconds 90
  }
  $writer.WriteLine('sendkey ret')
} finally {$client.Dispose()}
Write-Output ("QEMU public-task guest launched: PID $($process.Id)")
