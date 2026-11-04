$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root='C:\issue-closeout-evidence\mp77p-stage-a2'
$node='F:\work\SOUI_WEB\out\open-issues-closeout-20261029\message-port\tools\node.exe'
$plan=Join-Path $PSScriptRoot 'Paired.transport.json'
$script=Join-Path $PSScriptRoot 'Paired-GuestStage.cjs'
if($env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'-or[IntPtr]::Size-ne8){throw 'Task VM x64 only'}
if(Test-Path -LiteralPath $root){throw 'Fresh stage evidence required'}
if((Get-FileHash -LiteralPath $plan -Algorithm SHA256).Hash.ToLowerInvariant()-cne'0000000000000000000000000000000000000000000000000000000000000000'){throw 'Reviewed plan differs'}
if((Get-FileHash -LiteralPath $script -Algorithm SHA256).Hash.ToLowerInvariant()-cne'1111111111111111111111111111111111111111111111111111111111111111'){throw 'Reviewed metadata stager differs'}
if((Get-Item -LiteralPath $node).Length-ne91380224-or(Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'){throw 'Exact guest Node differs'}
$names=@('NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER')
$saved=@{};$errors=[Collections.Generic.List[string]]::new();$exitCode=1;$nativeExit=$null;$held=$false;$released=$false
$mutex=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
try{
 try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true;throw 'Abandoned validation mutex'}
 if(-not$held){throw 'Validation mutex occupied'}
 New-Item -ItemType Directory -Path $root|Out-Null
 $owner=[Diagnostics.Process]::GetCurrentProcess()
 $lease=@{name='Local\SouiDevKit.DesktopValidation';acquired=$true;mode='stage';ownerProcessId=$PID;ownerCreationFileTime=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
 [IO.File]::WriteAllText((Join-Path $root 'Lease.actual.json'),($lease|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
 foreach($name in $names){$v=[Environment]::GetEnvironmentVariable($name,'Process');$saved[$name]=@{present=($null-ne$v);value=$v};[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 $priorPreference=$ErrorActionPreference
 try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& $node $script 'ROOT_REVIEWED_PAIRED_GUEST_STAGE_V1' $plan '0000000000000000000000000000000000000000000000000000000000000000' 1>(Join-Path $root 'stdout.log') 2>(Join-Path $root 'stderr.log');$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$priorPreference}
 if($null-eq$nativeExit){throw 'Native metadata stage exit absent'};$exitCode=[int]$nativeExit
}catch{[Console]::Error.WriteLine($_.Exception.Message);$exitCode=1}
finally{
 foreach($name in $saved.Keys){try{[Environment]::SetEnvironmentVariable($name,$saved[$name].value,'Process');$now=[Environment]::GetEnvironmentVariable($name,'Process');if(($null-ne$now)-ne$saved[$name].present-or($saved[$name].present-and$now-cne$saved[$name].value)){throw 'Restore mismatch'}}catch{$errors.Add($name)}}
 if($held){try{$mutex.ReleaseMutex();$released=$true}catch{$errors.Add('mutex')}};$mutex.Dispose()
 if(Test-Path -LiteralPath $root){[IO.File]::WriteAllText((Join-Path $root 'PowerShell.finished.json'),(@{exitCode=$exitCode;actualNativeExitCode=$nativeExit;restoreErrors=@($errors.ToArray());environmentRestored=($errors.Count-eq0);pathChangedByStage=$false;leaseReleased=$released;productExecuted=$false;releaseReady=$false}|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))}
}
if($errors.Count){exit 1};exit $exitCode
