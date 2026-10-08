$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$transfer=Join-Path $root 'guest-preview6-app-transfer-20260925-v3'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk=Join-Path $root 'system.qcow2'
$serial=Join-Path $root 'preview6-apps-serial-20260925.txt'
$stdout=Join-Path $root 'preview6-apps-qemu-out-20260925.log'
$stderr=Join-Path $root 'preview6-apps-qemu-err-20260925.log'
$launch=Join-Path $root 'preview6-apps-qemu-launch-20260925.json'
foreach($path in @($qemu,$disk,(Join-Path $transfer 'SouiStarter.zip'),
    (Join-Path $transfer 'SouiWorkbench.zip'),(Join-Path $transfer '7za.exe'),
    (Join-Path $transfer 'input.json'),(Join-Path $transfer 'run-preview6.cmd'),
    (Join-Path $transfer 'verify-apps.ps1'),(Join-Path $transfer 'export-serial.ps1'))){
  if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing QEMU input: $path"}
}
if(-not(Test-Path -LiteralPath (Join-Path $root 'mixed-soak-20260924\status.json') -PathType Leaf)){
  throw 'The host eight-hour validation has no terminal result yet'
}
if(Get-Process -Name SouiAcceptance,qemu-system-i386,qemu-system-x86_64 -ErrorAction SilentlyContinue){
  throw 'Another host acceptance or QEMU process is still running'
}
foreach($path in @($serial,$stdout,$stderr,$launch)){
  if(Test-Path -LiteralPath $path){throw "QEMU output already exists: $path"}
}
$record=Get-Content -LiteralPath (Join-Path $transfer 'input.json') -Raw | ConvertFrom-Json
if($record.schema -ne 'soui.win7-x86-preview6-app-input/v1' -or $record.version -ne '0.3.0-preview.6' -or
    $record.architecture -ne 'x86' -or $record.configuration -ne 'Release' -or $record.releaseReady -ne $false){
  throw 'Unexpected preview.6 input identity'
}
$committed=Join-Path $PSScriptRoot '..\input.json'
if((Get-FileHash -LiteralPath $committed -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath (Join-Path $transfer 'input.json') -Algorithm SHA256).Hash){
  throw 'Guest input differs from the committed record'
}
foreach($name in @('run-preview6.cmd','verify-apps.ps1','export-serial.ps1')){
  $expected=$record.scripts.$name
  if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash -cne $expected -or
      (Get-FileHash -LiteralPath (Join-Path $transfer $name) -Algorithm SHA256).Hash -cne $expected){
    throw "Guest script differs from the frozen input: $name"
  }
}
foreach($name in @('SouiStarter','SouiWorkbench')){
  $file=Join-Path $transfer "$name.zip"
  $entry=$record.packages.$name
  if((Get-Item -LiteralPath $file).Length -ne $entry.zipBytes -or
      (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -cne $entry.zipSha256){
    throw "Guest ZIP differs from the frozen input: $name"
  }
}
if((Get-FileHash -LiteralPath (Join-Path $transfer '7za.exe') -Algorithm SHA256).Hash -cne $record.sevenZipSha256){
  throw 'Guest archive tool differs from the frozen input'
}
$arguments=@('-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
  '-drive',"file=$disk,format=qcow2,if=ide",
  '-drive',"file=fat:ro:$transfer,format=raw,if=ide,index=1",
  '-snapshot','-net','none','-vga','std','-display','none',
  '-vnc','127.0.0.1:99','-monitor','tcp:127.0.0.1:4452,server,nowait',
  '-serial',"file:$serial")
$process=Start-Process -FilePath $qemu -ArgumentList $arguments -PassThru -WindowStyle Hidden `
  -WorkingDirectory $root -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$launchRecord=[ordered]@{pid=$process.Id;startedAt=[DateTimeOffset]::UtcNow.ToString('o');
  arguments=$arguments;sourceSha256=$record.sourceSha256;buildId=$record.buildId;
  starterZipSha256=$record.packages.SouiStarter.zipSha256;
  workbenchZipSha256=$record.packages.SouiWorkbench.zipSha256;releaseReady=$false}
[IO.File]::WriteAllText($launch,($launchRecord | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output "QEMU preview.6 app guest launched: PID $($process.Id). Verify Win7 desktop, then run run-preview6-apps-keys.ps1."
