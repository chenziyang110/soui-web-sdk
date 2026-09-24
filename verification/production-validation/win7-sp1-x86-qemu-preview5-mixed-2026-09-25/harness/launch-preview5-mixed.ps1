$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$transfer=Join-Path $root 'guest-preview5-mixed-transfer'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk=Join-Path $root 'system.qcow2'
$serial=Join-Path $root 'preview5-mixed-serial.txt'
$output=Join-Path $root 'preview5-mixed-qemu-out.log'
$errorLog=Join-Path $root 'preview5-mixed-qemu-err.log'
$launch=Join-Path $root 'preview5-mixed-qemu-launch.json'
foreach($path in @($qemu,$disk,(Join-Path $transfer 'SouiAcceptance.zip'),(Join-Path $transfer 'input.json'))){
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing QEMU input: $path"}
}
if(-not (Test-Path -LiteralPath (Join-Path $root 'mixed-soak-20260924\status.json'))){
  throw 'The host eight-hour desktop validation has no terminal result yet'
}
if(Get-Process -Name SouiAcceptance,qemu-system-i386,qemu-system-x86_64 -ErrorAction SilentlyContinue){
  throw 'Another host acceptance or QEMU process is still running'
}
foreach($path in @($serial,$output,$errorLog,$launch)){
  if(Test-Path -LiteralPath $path){throw "QEMU output already exists: $path"}
}
$inputRecord=Get-Content -LiteralPath (Join-Path $transfer 'input.json') -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath (Join-Path $transfer 'SouiAcceptance.zip') -Algorithm SHA256).Hash -cne $inputRecord.zipSha256){
  throw 'Mixed ZIP does not match the frozen input record'
}
$arguments=@('-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
  '-drive',"file=$disk,format=qcow2,if=ide",
  '-drive',"file=fat:ro:$transfer,format=raw,if=ide,index=1",
  '-snapshot','-net','none','-vga','std','-display','none',
  '-vnc','127.0.0.1:99','-monitor','tcp:127.0.0.1:4445,server,nowait',
  '-serial',"file:$serial")
$process=Start-Process -FilePath $qemu -ArgumentList $arguments -PassThru -WindowStyle Hidden `
  -WorkingDirectory $root -RedirectStandardOutput $output -RedirectStandardError $errorLog
$record=[ordered]@{pid=$process.Id;startedAt=[DateTimeOffset]::UtcNow.ToString('o');arguments=$arguments;
  sourceSha256=$inputRecord.sourceSha256;zipSha256=$inputRecord.zipSha256;releaseReady=$false}
[IO.File]::WriteAllText($launch,($record | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output ("QEMU preview.5 mixed guest started: PID $($process.Id)")
