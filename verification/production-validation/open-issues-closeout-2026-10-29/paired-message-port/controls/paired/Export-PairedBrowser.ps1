[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('preflight','run')][string]$Phase)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
if([IntPtr]::Size-ne8-or$env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'){throw 'Task VM x64 only'}
$folder='C:\issue-closeout-evidence\mp77-'+$Phase+'-pa2'
$utf8=[Text.UTF8Encoding]::new($false)
function Pin([string]$File){$i=Get-Item -LiteralPath $File -Force;if($i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain file required'};[ordered]@{path=$i.FullName;bytes=$i.Length;sha256=(Get-FileHash -LiteralPath $i.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Same($Expected){$a=Pin $Expected.path;if($a.bytes-ne$Expected.bytes-or$a.sha256-cne$Expected.sha256){throw 'Frozen evidence bytes changed'}}
function Write-New([string]$File,$Value){$b=$utf8.GetBytes(($Value|ConvertTo-Json -Depth 16)+[char]10);$s=[IO.File]::Open($File,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$s.Write($b,0,$b.Length)}finally{$s.Dispose()}}
function Files([string]$Directory){$i=Get-Item -LiteralPath $Directory -Force;if(-not$i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain directory required'};foreach($e in Get-ChildItem -LiteralPath $Directory -Force){if($e.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked member rejected'};if($e.PSIsContainer){Files $e.FullName}else{$e.FullName}}}
function Same-Path([string]$A,[string]$B){[IO.Path]::GetFullPath($A)-ieq[IO.Path]::GetFullPath($B)}
foreach($n in @('ExportPreflight.actual.json','ExportFiles.actual.json','SerialExport.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $n)){throw 'One fresh complete export only'}}
$psPin=Pin (Join-Path $folder 'PowerShell.finished.json');$ps=Get-Content -LiteralPath $psPin.path -Raw|ConvertFrom-Json
if($ps.exitCode-ne$ps.actualNativeExitCode-or$ps.actualNativeExitCode-notin@(0,1)-or-not$ps.environmentRestored-or-not$ps.pathRestored-or-not$ps.cwdRestored-or-not$ps.leaseReleased-or@($ps.restoreErrors).Count-or$ps.originalControllersModified-or$ps.originalTimelimitsChanged){throw 'Exact completed source-awaited browser wrapper required; failure codes remain failures'}
$leasePin=Pin (Join-Path $folder 'Lease.actual.json');$lease=Get-Content -LiteralPath $leasePin.path -Raw|ConvertFrom-Json
if($lease.name-cne'Local\SouiDevKit.DesktopValidation'-or-not$lease.acquired-or$lease.mode-cne$Phase-or$lease.ownerProcessId-le0-or[string]$lease.ownerCreationFileTime-notmatch'^[0-9]{17,20}$'){throw 'Actual wrapper identity missing'}
$invocationPin=Pin (Join-Path $folder 'Invocation.actual.json');$invocation=Get-Content -LiteralPath $invocationPin.path -Raw|ConvertFrom-Json
if($invocation.wrapperPid-ne$lease.ownerProcessId-or$invocation.wrapperCreationFileTime-cne$lease.ownerCreationFileTime-or$invocation.mode-cne$Phase-or-not(Same-Path $invocation.actualNodeCwd 'F:\work\soui-issue96')){throw 'Actual invocation identity or source CWD differs'}
$manifestPin=Pin $invocation.manifest.path;if($manifestPin.bytes-ne$invocation.manifest.bytes-or$manifestPin.sha256-cne$invocation.manifest.sha256){throw 'Original browser media must remain mounted and unchanged'}
$manifest=Get-Content -LiteralPath $manifestPin.path -Raw|ConvertFrom-Json
foreach($closedRoot in $manifest.closedReaderRoots){if(Same-Path $closedRoot ([IO.Path]::GetPathRoot($PSCommandPath))){throw 'This extra exporter must not be inside a closed media root'}}
$inputPin=Pin $manifest.executionInput.path;if($inputPin.bytes-ne$manifest.executionInput.bytes-or$inputPin.sha256-cne$manifest.executionInput.sha256){throw 'Actual browser input changed'}
$cfg=Get-Content -LiteralPath $inputPin.path -Raw|ConvertFrom-Json
$owner=Get-Process -Id ([int]$lease.ownerProcessId) -ErrorAction SilentlyContinue;$reused=$false
if($null-ne$owner){try{$birth=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();if([UInt64]::Parse($birth)-lt[UInt64]::Parse([string]$lease.ownerCreationFileTime)){throw 'Observed PID birth predates original owner'};if($birth-ceq[string]$lease.ownerCreationFileTime-and-not$owner.HasExited){throw 'Original wrapper still alive'};$reused=($birth-cne[string]$lease.ownerCreationFileTime)}finally{$owner.Dispose()}}
$outerClosures=@()
if($Phase-eq'run'){
 $summaryPin=Pin (Join-Path $folder 'Summary.actual.json');$summary=Get-Content -LiteralPath $summaryPin.path -Raw|ConvertFrom-Json
 if($summary.schema-cne'soui.message-port-guest-four-arm-result/v1'-or$summary.sourceVersion-cne'0.5.1-preview.1'-or$summary.currentSdk060Qualified-or$summary.qualificationPassed-or$summary.releaseReady){throw 'Actual finite four-arm summary scope differs'}
 $order=@('x64-baseline','x64-candidate','x86-baseline','x86-candidate');$attempted=@($summary.attemptedArms)
 if($attempted.Count-gt4-or($attempted-join"`n")-cne(($order|Select-Object -First $attempted.Count)-join"`n")){throw 'Actual attempted arm prefix differs'}
 foreach($key in $attempted){
  $dir=Join-Path $folder $key;$finishedPin=Pin (Join-Path $dir 'finished.json');$r=Get-Content -LiteralPath $finishedPin.path -Raw|ConvertFrom-Json
  $rawPin=Pin (Join-Path $dir 'owned.ndjson');$ctxPin=Pin (Join-Path $dir 'owned.ndjson.context.json');$ctx=Get-Content -LiteralPath $ctxPin.path -Raw|ConvertFrom-Json
  $readyPin=Pin (Join-Path $dir 'ready.json');$ready=Get-Content -LiteralPath $readyPin.path -Raw|ConvertFrom-Json
  $rows=@(Get-Content -LiteralPath $rawPin.path -Encoding UTF8|Where-Object{$_-ne''}|ForEach-Object{$_|ConvertFrom-Json});if($rows.Count-lt2){throw 'Actual outer owned stream incomplete'};$first=$rows[0];$last=$rows[-1]
  if(-not$r.closureConfirmed-or$null-ne$r.reportError-or$null-ne$r.spawnError-or$r.fallbackUsed-or-not$r.terminal.rootExited-or-not$r.terminal.closureConfirmed-or$r.terminal.activeProcesses-ne0-or$r.terminal.event-cne'terminal'){throw 'Attempted outer Owned Job closure not proved; do not export live output'}
  if($r.pid-le0-or[string]$r.creationFileTime-notmatch'^[0-9]{17,20}$'-or-not(Same-Path $r.report $rawPin.path)-or$r.reportSha256-cne$rawPin.sha256-or$first.event-cne'start'-or$first.pid-ne$r.pid-or$first.creationFileTime-cne$r.creationFileTime-or$ready.pid-ne$r.pid-or$ready.creationFileTime-cne$r.creationFileTime-or($last|ConvertTo-Json -Compress -Depth 12)-cne($r.terminal|ConvertTo-Json -Compress -Depth 12)){throw 'Actual outer raw/ready/terminal identity binding differs'}
  if($ctx.schema-cne'soui.owned-process-context/v1'-or$ctx.timeoutMs-ne420000-or$ctx.drainMs-ne2000-or$ctx.runnerSha256-cne'1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f'-or$ctx.executableSha256-cne'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'-or$r.runnerSha256-cne$ctx.runnerSha256-or$r.executableSha256-cne$ctx.executableSha256-or-not(Same-Path $ctx.cwd 'F:\work\soui-issue96')-or@($ctx.args).Count-ne5-or$ctx.args[1]-cne'ROOT_CURRENT_MESSAGE_PORT_AB_V1'-or$ctx.args[4]-cne$key){throw 'Original outer context/image/deadline changed'}
  $expectedArgs=@([string]$cfg.controller.path,'ROOT_CURRENT_MESSAGE_PORT_AB_V1',[string]$manifest.executionInput.path,[string]$manifest.executionInput.sha256,$key)
  if(($ctx.args-join"`n")-cne($expectedArgs-join"`n")-or-not(Same-Path $ctx.executable $cfg.requiredExecutionNode.path)){throw 'Actual outer frozen controller argv differs'}
  $outerClosures+=@{key=$key;finished=$finishedPin;raw=$rawPin;context=$ctxPin;ready=$readyPin;actualExitCode=$r.exitCode;actualSupervisorExitCode=$r.supervisorExitCode;passedAsRecorded=$r.passed;closureConfirmed=$true;forcedTerminationAsRecorded=$r.terminal.terminatedOwnedJob;limitTerminatedAsRecorded=$r.terminal.limitTerminatedProcesses}
 }
}else{$summaryPin=$null}
$exporter=Pin 'C:\issues8\Guest-Export.ps1';if($exporter.sha256-cne'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c'){throw 'Original serial exporter changed'}
Write-New (Join-Path $folder 'ExportPreflight.actual.json') @{schema='soui.paired-browser-closed-export-preflight/v1';phase=$Phase;actualMachine=$env:COMPUTERNAME;powerShellFinished=$psPin;lease=$leasePin;invocation=$invocationPin;originalParentIdentityAbsent=$true;parentPidCurrentlyReused=$reused;actualSourceAwaitedGuestNodeExitCode=$ps.actualNativeExitCode;guestNodePidRecorded=$false;guestNodePidAbsenceObserved=$false;topGuestNodeClosureBasis='Exact reviewed PowerShell invokes the unchanged guest Node synchronously and captures its actual native exit before finally; no standalone PID absence or whole-Job pass is inferred';attemptedOuterJobClosures=$outerClosures;originalSummary=$summaryPin;allActualFailuresRetained=$true;exporter=$exporter;testPassed=$null;releaseReady=$false}
$names=@(Files $folder|Sort-Object);if($names.Count-lt1-or$names.Count-gt512){throw 'Complete bounded file count required'};$pins=@();$bytes=[long]0
foreach($f in $names){$p=Pin $f;$bytes+=$p.bytes;if($p.bytes-gt16777216-or$bytes-gt8388608){throw 'Complete closed evidence exceeds8MiB; preserve originals without truncation'};$pins+=,$p}
Write-New (Join-Path $folder 'ExportFiles.actual.json') @{schema='soui.issue-short-export-files/v1';files=$pins;totalBytes=$bytes;scope='Exact complete closed paired browser evidence; original outcomes preserved, no exporter product verdict';testPassed=$null;releaseReady=$false}
$catalog=Pin (Join-Path $folder 'ExportFiles.actual.json');if($bytes+$catalog.bytes-gt8388608){throw 'Evidence plus catalog exceeds8MiB'}
foreach($p in $pins){Same $p};Same $catalog;Same $exporter
Same $manifestPin;Same $inputPin
$expected=@(($names+@($catalog.path))|Sort-Object);if((@(Files $folder|Sort-Object)-join"`n")-cne($expected-join"`n")){throw 'Closed source set changed'}
$nativeExit=$null;$prior=$ErrorActionPreference
try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $exporter.path -EvidenceDirectory $folder;$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
if($null-eq$nativeExit){throw 'Actual serial exporter exit absent'}
foreach($p in $pins){Same $p};Same $catalog;Same $exporter
Same $manifestPin;Same $inputPin
$expected=@(($names+@($catalog.path,(Join-Path $folder 'SerialExport.actual.json')))|Sort-Object);if((@(Files $folder|Sort-Object)-join"`n")-cne($expected-join"`n")){throw 'Evidence changed during export'}
Write-Output ('Browser evidence exporter actual exit='+$nativeExit+'; original test outcome retained');exit $nativeExit
