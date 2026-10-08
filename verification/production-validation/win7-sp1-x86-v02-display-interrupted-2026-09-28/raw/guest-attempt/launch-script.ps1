param([string]$Attempt='guest-display-explore-attempt1')
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot $Attempt
if(Test-Path -LiteralPath $out){throw 'Refuse overwrite guest attempt'}
$prep=Join-Path $PSScriptRoot 'display-explore-attempt1'
$qemu='C:\Program Files\qemu\qemu-system-i386.exe'
$disk='D:\SOUI-SDK-QEMU-validation\system.qcow2'
$status=Get-Content -LiteralPath 'D:\SOUI-SDK-QEMU-validation\mixed-soak-20260924\status.json' -Raw | ConvertFrom-Json
if($status.status -notin @('passed','failed')){throw 'Host long run not terminal'}
$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(Soui.*|qemu-.*|RenewalStarter|InstalledRenewalProbe|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'} | Select-Object Name,ProcessId,CommandLine)
if($busy.Count){throw 'Other desktop or QEMU load is running'}
New-Item -ItemType Directory -Path $out | Out-Null
[ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$busy;hostStatus=$status}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'gate.json') -Encoding utf8
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $out 'launch-script.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Monitor-DisplayExplore.cjs') -Destination (Join-Path $out 'monitor-script.cjs')
& node (Join-Path $PSScriptRoot 'Verify-DisplayExplore.cjs') *> (Join-Path $out 'frozen-check-before.log')
if($LASTEXITCODE -ne 0){throw 'Frozen candidate check failed'}
$ports=@(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Select-Object -ExpandProperty LocalPort)
$monitor=10660; while($ports -contains $monitor){$monitor++}
$display=60; while($ports -contains (5900+$display)){$display++}
$diskBefore=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash.ToLowerInvariant()
if($diskBefore -ne 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4'){throw 'Base disk differs from licensed frozen guest'}
if((Get-FileHash -LiteralPath (Join-Path $prep 'freeze.json')).Hash.ToLowerInvariant() -ne '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b'){throw 'Current guest freeze changed'}
$arguments=@('-name','soui-v02-current-win7-display-explore','-machine','pc,accel=tcg','-cpu','core2duo','-smp','2','-m','2048',
 '-drive',"file=$disk,format=qcow2,if=ide",'-drive',"file=fat:ro:$prep\volume,format=raw,if=ide,index=1",
 '-snapshot','-net','none','-vga','std','-display','none','-vnc',"127.0.0.1:$display",'-monitor',"tcp:127.0.0.1:$monitor,server,nowait",
 '-serial',('file:'+(Join-Path $out 'serial-export.txt')))
$si=[Diagnostics.ProcessStartInfo]::new(); $si.FileName=$qemu
$si.Arguments=($arguments|ForEach-Object {'"'+$_+'"'}) -join ' '
$si.EnvironmentVariables['TEMP']=$out;$si.EnvironmentVariables['TMP']=$out;$si.WorkingDirectory=$out; $si.UseShellExecute=$false; $si.CreateNoWindow=$true
$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden; $si.RedirectStandardOutput=$true; $si.RedirectStandardError=$true
$p=[Diagnostics.Process]::new(); $p.StartInfo=$si
$started=[DateTime]::UtcNow
$launchGate=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'} | Select-Object ProcessId,Name,CreationDate)
[ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$launchGate}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'gate-before-launch.json') -Encoding utf8
if($launchGate.Count){throw 'Desktop load appeared before QEMU launch; no VM started'}
if(-not $p.Start()){throw 'QEMU did not start'}
$stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
[ordered]@{pid=$p.Id;program=$qemu;arguments=$arguments;startedAt=$started.ToString('o');monitorPort=$monitor;vncPort=(5900+$display);diskSha256Before=$diskBefore;freezeSha256=(Get-FileHash -LiteralPath (Join-Path $prep 'freeze.json')).Hash.ToLowerInvariant();monitorScriptSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Monitor-DisplayExplore.cjs')).Hash.ToLowerInvariant();releaseReady=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'launch.json') -Encoding utf8
Write-Output "QEMU PID $($p.Id); monitor $monitor; VNC $(5900+$display); attempt $out"
$timedOut=$false;$externalLoadObserved=$false;$gateFailure=$null
$loadSamples=[Collections.Generic.List[object]]::new();$nextGate=[DateTime]::UtcNow
try {
 while(-not $p.WaitForExit(1000)){
  if([DateTime]::UtcNow -ge $nextGate){
    $nextGate=[DateTime]::UtcNow.AddSeconds(5);$observed=@()
    try {$observed=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.ProcessId -ne $p.Id -and $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'} | Select-Object ProcessId,Name,CreationDate)}catch{$gateFailure=$_.Exception.Message}
    $loadSamples.Add([ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=$observed;error=$gateFailure})
    if($observed.Count -or $gateFailure){$externalLoadObserved=$true;$p.Kill();$p.WaitForExit();break}
  }
  if(([DateTime]::UtcNow-$started).TotalSeconds -gt 720){$timedOut=$true; $p.Kill(); $p.WaitForExit(); break}
 }
 $loadSamples.ToArray()|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'gates-during.json') -Encoding utf8
 $code=$p.ExitCode
 [IO.File]::WriteAllText((Join-Path $out 'qemu.stdout.log'),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $out 'qemu.stderr.log'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 $diskAfter=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash.ToLowerInvariant()
 & node (Join-Path $PSScriptRoot 'Verify-DisplayExplore.cjs') *> (Join-Path $out 'frozen-check-after.log')
 $frozenExit=$LASTEXITCODE
 [ordered]@{pid=$p.Id;startedAt=$started.ToString('o');finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code;timedOut=$timedOut;externalLoadObserved=$externalLoadObserved;gateFailure=$gateFailure;diskSha256Before=$diskBefore;diskSha256After=$diskAfter;diskUnchanged=($diskBefore -eq $diskAfter);frozenCheckExitCode=$frozenExit;releaseReady=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'qemu-result.json') -Encoding utf8
 if($externalLoadObserved -or $gateFailure -or $timedOut -or $code -ne 0 -or $diskBefore -ne $diskAfter -or $frozenExit -ne 0){exit 1}
}finally{$p.Dispose()}
