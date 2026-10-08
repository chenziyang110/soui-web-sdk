param([string]$Attempt='preview11-win7-apps-attempt3')
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot $Attempt
if(Test-Path -LiteralPath $out){throw 'Refuse overwrite guest attempt'}
$prep=Join-Path $PSScriptRoot 'preview11-win7-preparation-v3-20260926'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk='D:\SOUI-SDK-QEMU-validation\system.qcow2'
$status=Get-Content -LiteralPath 'D:\SOUI-SDK-QEMU-validation\mixed-soak-20260924\status.json' -Raw | ConvertFrom-Json
if($status.status -notin @('passed','failed')){throw 'Host long run not terminal'}
$busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(Soui.*|qemu-system-.*|RenewalStarter|InstalledRenewalProbe|soui_.*tests)\.exe$'} | Select-Object Name,ProcessId,CommandLine)
if($busy.Count){throw 'Other desktop or QEMU load is running'}
New-Item -ItemType Directory -Path $out | Out-Null
[ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$busy;hostStatus=$status}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'gate.json') -Encoding utf8
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $out 'launch-script.ps1')
& node (Join-Path $prep 'Verify-Frozen.cjs') *> (Join-Path $out 'frozen-check-before.log')
if($LASTEXITCODE -ne 0){throw 'Frozen candidate check failed'}
$ports=@(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Select-Object -ExpandProperty LocalPort)
$monitor=10660; while($ports -contains $monitor){$monitor++}
$display=60; while($ports -contains (5900+$display)){$display++}
$diskBefore=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash.ToLowerInvariant()
$arguments=@('-name','soui-preview11-win7-apps','-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
 '-drive',"file=$disk,format=qcow2,if=ide",'-drive',"file=fat:ro:$prep\volume,format=raw,if=ide,index=1",
 '-snapshot','-net','none','-vga','std','-display','none','-vnc',"127.0.0.1:$display",'-monitor',"tcp:127.0.0.1:$monitor,server,nowait",
 '-serial',('file:'+(Join-Path $out 'serial-export.txt')))
$si=[Diagnostics.ProcessStartInfo]::new(); $si.FileName=$qemu
$si.Arguments=($arguments|ForEach-Object {'"'+$_+'"'}) -join ' '
$si.WorkingDirectory=$out; $si.UseShellExecute=$false; $si.CreateNoWindow=$true
$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden; $si.RedirectStandardOutput=$true; $si.RedirectStandardError=$true
$p=[Diagnostics.Process]::new(); $p.StartInfo=$si
$started=[DateTime]::UtcNow
if(-not $p.Start()){throw 'QEMU did not start'}
$stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
[ordered]@{pid=$p.Id;program=$qemu;arguments=$arguments;startedAt=$started.ToString('o');monitorPort=$monitor;vncPort=(5900+$display);diskSha256Before=$diskBefore;freezeSha256=(Get-FileHash -LiteralPath (Join-Path $prep 'freeze.json')).Hash.ToLowerInvariant();releaseReady=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'launch.json') -Encoding utf8
Write-Output "QEMU PID $($p.Id); monitor $monitor; VNC $(5900+$display); attempt $out"
$timedOut=$false
try {
 while(-not $p.WaitForExit(1000)){
  if(([DateTime]::UtcNow-$started).TotalSeconds -gt 720){$timedOut=$true; $p.Kill(); $p.WaitForExit(); break}
 }
 $code=$p.ExitCode
 [IO.File]::WriteAllText((Join-Path $out 'qemu.stdout.log'),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $out 'qemu.stderr.log'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 $diskAfter=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash.ToLowerInvariant()
 & node (Join-Path $prep 'Verify-Frozen.cjs') *> (Join-Path $out 'frozen-check-after.log')
 $frozenExit=$LASTEXITCODE
 [ordered]@{pid=$p.Id;startedAt=$started.ToString('o');finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code;timedOut=$timedOut;diskSha256Before=$diskBefore;diskSha256After=$diskAfter;diskUnchanged=($diskBefore -eq $diskAfter);frozenCheckExitCode=$frozenExit;releaseReady=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'qemu-result.json') -Encoding utf8
 if($timedOut -or $code -ne 0 -or $diskBefore -ne $diskAfter -or $frozenExit -ne 0){exit 1}
}finally{$p.Dispose()}
