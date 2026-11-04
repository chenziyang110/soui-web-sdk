$ErrorActionPreference='Stop'
$taskRoot='F:/work/SOUI_WEB/out/open-issues-closeout-20261029'
$skill='C:/Users/11034/.codex/skills/qemu-novnc-lab'
$vmRun='C:/Users/11034/.codex/task-vms/soui-devtools-qemu-stable-e89cf96f2d1e4685bbdac347cb9eea3d/private-fe2e6175cf5a4a78b23d418330506ee3/postsettings-cpu-nehalem-3f486e90/runs/166925c3-9fd3-4751-9a17-c45d880af0a6'
if((Get-FileHash -LiteralPath ($skill+'/scripts/lab.py') -Algorithm SHA256).Hash.ToLowerInvariant() -cne 'ed4216f706238f2ec8277da190e6929f1688d4a60be5b1817b683f9a71846d50'){throw 'Reviewed recovery implementation drift'}
if((Get-FileHash -LiteralPath ($skill+'/scripts/pool.py') -Algorithm SHA256).Hash.ToLowerInvariant() -cne '1bfa2b9ec7effecc3657ccc5d7a5dcbac99c045b74902b9c9013e440810cff2a'){throw 'Reviewed pool implementation drift'}
if(-not $env:SOUI_TASK_VM_TOKEN){throw 'Current held VM lease token is required'}
$evidence=Join-Path $taskRoot ('vm-recovery-'+[Guid]::NewGuid().ToString())
[void](New-Item -ItemType Directory -Path $evidence)
$pins=@{};$rows=@()
foreach($name in @('lab.json','started.json','active-session.json','start.lock')){
  $file=Join-Path $vmRun $name
  $before=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
  Copy-Item -LiteralPath $file -Destination (Join-Path $evidence $name)
  $after=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
  $copy=(Get-FileHash -LiteralPath (Join-Path $evidence $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  if($before-cne$after-or$copy-cne$before){throw 'VM marker changed during snapshot'}
  $pins[$name]=$before;$rows+=@{name=$name;bytes=(Get-Item -LiteralPath $file).Length;sha256=$before}
}
$started=Get-Content -LiteralPath (Join-Path $evidence 'started.json') -Raw|ConvertFrom-Json
$active=Get-Content -LiteralPath (Join-Path $evidence 'active-session.json') -Raw|ConvertFrom-Json
if($started.session-cne'bbc12535-6afa-4abb-a997-509bb3ebed32'-or$active.id-cne$started.session){throw 'Current reviewed session differs'}
$review=@{schema='soui.qemu-offline-recovery-root-review/v1';reviewer='root';implementationAuthor='issues_ui_fixture_audit';createdUtc=[DateTime]::UtcNow.ToString('o');approved=$true;sourceLabSha256='ed4216f706238f2ec8277da190e6929f1688d4a60be5b1817b683f9a71846d50';originalSession=$started.session;originalMarkers=$rows;originalExitCode=$null;priorOutcome='UNKNOWN';cleanShutdownVerified=$false;reviewScope='Independent source and focused-test evidence review; actual deny-write/delete disk fence and current marker/session checks required by entry; native IPv4/IPv6 positive no-listener proof for Windows loopback timeouts; unrelated visible QEMU does not prove performance isolation';productExecuted=$false}
[IO.File]::WriteAllText((Join-Path $evidence 'RootReview.actual.json'),($review|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$recoveryArguments=@(($skill+'/scripts/pool.py'),'offline-recover','--name','soui-devtools-windows','--token',$env:SOUI_TASK_VM_TOKEN,
  '--reason','Current task VM supervisor lost without a terminal receipt; recover the existing disk using held native no-writer fences and preserve unknown prior outcome',
  '--expected-session',$started.session,'--expected-lab-sha256',$pins['lab.json'],'--expected-started-sha256',$pins['started.json'],
  '--expected-active-session-sha256',$pins['active-session.json'],'--expected-start-lock-sha256',$pins['start.lock'])
& python @recoveryArguments 1> (Join-Path $evidence 'recover.stdout.json') 2> (Join-Path $evidence 'recover.stderr.log')
$exit=$LASTEXITCODE
[IO.File]::WriteAllText((Join-Path $evidence 'Exit.actual.json'),(@{exitCode=$exit;productExecuted=$false;originalExitCode=$null;priorOutcome='UNKNOWN';automaticRetry=$false}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Write-Output $evidence
Get-Content -LiteralPath (Join-Path $evidence 'recover.stdout.json')
Get-Content -LiteralPath (Join-Path $evidence 'recover.stderr.log')
if($exit-ne0){throw 'Offline recovery refused; preserve this actual failure and inspect without retry'}
