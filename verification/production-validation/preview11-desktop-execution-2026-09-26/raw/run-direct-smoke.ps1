param([string]$Attempt='desktop-direct-smoke-attempt1')
$ErrorActionPreference='Stop'
$repo='C:\SOUI-SDK-development\preview9-tray'
$out=Join-Path $PSScriptRoot $Attempt
if(Test-Path -LiteralPath $out){throw 'Refuse overwrite desktop attempt'}
New-Item -ItemType Directory -Path $out | Out-Null
function Require-Idle([string]$name){
  $running=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(SouiAcceptance|qemu-system-(i386|x86_64)|SouiStarter|SouiWorkbench|SouiTrayConsumer|RenewalStarter|soui_desktop_unicode_tests|soui_directory_dialog_tests)\.exe$'} | Select-Object Name,ProcessId,CreationDate,CommandLine)
  [ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$running}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out ($name+'.gate.json')) -Encoding utf8
  if($running.Count){throw 'Desktop or QEMU is occupied; do not start another load'}
}
function Run-Desktop([string]$name,[string]$exe,[string[]]$arguments,[int]$timeout){
  Require-Idle $name
  $started=[DateTime]::UtcNow.ToString('o')
  $hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
  $si=[Diagnostics.ProcessStartInfo]::new()
  $si.FileName=$exe
  $si.Arguments=($arguments|ForEach-Object { if($_.Contains('"')){throw 'Unexpected quote in supervised argument'}; '"'+$_+'"' }) -join ' '
  $si.WorkingDirectory=Split-Path $exe
  $si.UseShellExecute=$false
  $si.CreateNoWindow=$true
  $si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  $si.RedirectStandardOutput=$true
  $si.RedirectStandardError=$true
  $si.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
  $si.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
  $p=[Diagnostics.Process]::new()
  $p.StartInfo=$si
  if(-not $p.Start()){throw 'Supervised process did not start'}
  $stdout=$p.StandardOutput.ReadToEndAsync()
  $stderr=$p.StandardError.ReadToEndAsync()
  try{
    $finished=$p.WaitForExit($timeout)
    if(-not $finished){Stop-Process -Id $p.Id -Force;$p.WaitForExit()}
    $p.Refresh()
    $code=$p.ExitCode
    [IO.File]::WriteAllText((Join-Path $out ($name+'.stdout.log')),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $out ($name+'.stderr.log')),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
    $unchanged=$hash -eq (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $remaining=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(SouiAcceptance|qemu-system-(i386|x86_64)|SouiStarter|SouiWorkbench|SouiTrayConsumer|RenewalStarter|soui_desktop_unicode_tests|soui_directory_dialog_tests)\.exe$'} | Select-Object Name,ProcessId,ParentProcessId,CreationDate,CommandLine)
    [ordered]@{name=$name;program=$exe;arguments=$arguments;pid=$p.Id;startedAt=$started;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code;timedOut=(-not $finished);exeSha256=$hash;exeUnchanged=$unchanged;remainingDesktopProcesses=$remaining;releaseReady=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out ($name+'.result.json')) -Encoding utf8
    Write-Host "$name exit=$code timedOut=$(-not $finished)"
    if(-not $finished -or $code -ne 0 -or -not $unchanged -or $remaining.Count){throw "$name failed or desktop became occupied; raw sample preserved, inspect remaining PIDs before any next load"}
  }finally{$p.Dispose()}
}
Run-Desktop 'x64-app-smoke' (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-File',(Join-Path $repo 'tools/Test.ps1'),'-Architecture','x64','-Configuration','Release','-NativeOnly') 180000
Copy-Item -LiteralPath (Join-Path $repo 'out/x64/smoke-Release.json') -Destination (Join-Path $out 'x64-smoke-Release.json')
foreach($arch in @('x86','x64')){
  $project=Join-Path $PSScriptRoot 'generated-project-attempt1'
  $exe=Join-Path $project "out/$arch/bin/Release/RenewalStarter/RenewalStarter.exe"
  Run-Desktop "$arch-generated-direct-smoke" $exe @('--smoke-test') 60000
  Copy-Item -LiteralPath (Join-Path (Split-Path $exe) 'smoke.log') -Destination (Join-Path $out "$arch-generated-smoke.log")
}