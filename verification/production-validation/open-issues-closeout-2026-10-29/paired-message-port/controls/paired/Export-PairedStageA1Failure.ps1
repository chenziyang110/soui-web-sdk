[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$folder='C:\issue-closeout-evidence\mp77p-stage-a1'
if([IntPtr]::Size-ne8-or$env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'){throw 'Task VM x64 only'}
$utf8=[Text.UTF8Encoding]::new($false)
function Pin([string]$File){$i=Get-Item -LiteralPath $File -Force;if($i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain evidence file required'};[ordered]@{path=$i.FullName;bytes=$i.Length;sha256=(Get-FileHash -LiteralPath $i.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Same-Pin($Expected){$a=Pin $Expected.path;if($a.bytes-ne$Expected.bytes-or$a.sha256-cne$Expected.sha256){throw 'Evidence/source pin changed'}}
function Write-New([string]$File,$Value){$b=$utf8.GetBytes(($Value|ConvertTo-Json -Depth 12)+[char]10);$s=[IO.File]::Open($File,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$s.Write($b,0,$b.Length)}finally{$s.Dispose()}}
function Files([string]$Directory){$i=Get-Item -LiteralPath $Directory -Force;if(-not$i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain evidence directory required'};foreach($e in Get-ChildItem -LiteralPath $Directory -Force){if($e.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked member rejected'};if($e.PSIsContainer){Files $e.FullName}else{$e.FullName}}}
foreach($n in @('ExportPreflight.actual.json','ExportFiles.actual.json','SerialExport.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $n)){throw 'One fresh complete export only'}}
$finishedPin=Pin (Join-Path $folder 'PowerShell.finished.json');$finished=Get-Content -LiteralPath $finishedPin.path -Raw|ConvertFrom-Json
if($finished.exitCode-ne1-or$finished.actualNativeExitCode-ne1-or-not$finished.environmentRestored-or-not$finished.leaseReleased-or$finished.pathChangedByStage-or@($finished.restoreErrors).Count){throw 'Exact failed metadata wrapper1/1 with restoration and lease release required'}
$leasePin=Pin (Join-Path $folder 'Lease.actual.json');$lease=Get-Content -LiteralPath $leasePin.path -Raw|ConvertFrom-Json
if($lease.name-cne'Local\SouiDevKit.DesktopValidation'-or-not$lease.acquired-or$lease.mode-cne'stage'-or$lease.ownerProcessId-le0-or[string]$lease.ownerCreationFileTime-notmatch'^[0-9]{17,20}$'){throw 'Actual stage parent identity missing'}
foreach($name in @('Start.actual.json','Summary.actual.json','Failure.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $name)){throw 'This exporter is only for a failed pre-Start metadata invocation'}}
$rawBefore=@(Files $folder|ForEach-Object{[IO.Path]::GetFileName($_)}|Sort-Object)
if(($rawBefore-join"`n")-cne(@('Lease.actual.json','PowerShell.finished.json','stderr.log','stdout.log')-join"`n")){throw 'Exact complete pre-Start failure tree required'}
$stderrPin=Pin (Join-Path $folder 'stderr.log');if($stderrPin.bytes-le0){throw 'Original actual failure stderr must be preserved'}
$owner=Get-Process -Id ([int]$lease.ownerProcessId) -ErrorAction SilentlyContinue
$ownerReused=$false
if($null-ne$owner){try{$observedBirth=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();if([UInt64]::Parse($observedBirth)-lt[UInt64]::Parse([string]$lease.ownerCreationFileTime)){throw 'Observed PID birth predates recorded owner; inconsistent identity'};if($observedBirth-ceq[string]$lease.ownerCreationFileTime-and-not$owner.HasExited){throw 'Original PowerShell owner still alive'};$ownerReused=($observedBirth-cne[string]$lease.ownerCreationFileTime)}finally{$owner.Dispose()}}
# No Start/summary was written, so Node PID/birth were not captured. The exact
# source calls Node synchronously and records its actual exit1 before finally.
# Do not guess a PID, claim process absence, or infer a natural Native Job pass.
$exporter=Pin 'C:\issues8\Guest-Export.ps1'
if($exporter.sha256-cne'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c'){throw 'Original serial exporter changed'}
Write-New (Join-Path $folder 'ExportPreflight.actual.json') @{schema='soui.failed-prestart-paired-metadata-export-preflight/v1';actualMachine=$env:COMPUTERNAME;powerShellFinished=$finishedPin;actualFailureStderr=$stderrPin;lease=$leasePin;originalParentIdentityAbsent=$true;parentPidCurrentlyReused=$ownerReused;recordedActualSynchronousNodeExitCode=1;nodePidRecorded=$false;nodeBirthRecorded=$false;nodePidAbsenceObserved=$false;noStartOrSuiteWriteTransitionReached=$true;sourceControlFlowBasis='Frozen a1 Paired-GuestStage source349a0ff... writes Start before any suite/control output; source contains no child launch before this transition';sourceIdentityBasis='Root reviewed unchanged26-file carrier/plan and actual assertion at CRT preflight; no stage media file is rehashed after CD swap';originalStageSourceSha256='349a0ffbeb84d4f9fb294c8784984b897241fd39f3a6c9bdbf67d3160122b51d';originalStagePlanSha256='d903b2ba8c481761e1ac1ad3e186a9952dbe49db3376cd59b1c56828db876f60';exporter=$exporter;scope='Exact failed metadata preflight evidence only; no Native Job/process-absence/browser-pass inference';testPassed=$false;productExecuted=$false;releaseReady=$false}
$names=@(Files $folder|Sort-Object);if($names.Count-lt1-or$names.Count-gt512){throw 'Bounded complete file count required'}
$pins=@();$bytes=[long]0
foreach($f in $names){$p=Pin $f;$bytes+=$p.bytes;if($p.bytes-gt16777216-or$bytes-gt8388608){throw 'Complete evidence exceeds8MiB; preserve originals without truncation'};$pins+=,$p}
Write-New (Join-Path $folder 'ExportFiles.actual.json') @{schema='soui.issue-short-export-files/v1';files=$pins;totalBytes=$bytes;scope='Exact complete closed metadata stage tree; no product/Job pass claim';testPassed=$null;releaseReady=$false}
$catalog=Pin (Join-Path $folder 'ExportFiles.actual.json');if($bytes+$catalog.bytes-gt8388608){throw 'Evidence plus catalog exceeds8MiB'}
foreach($p in $pins){Same-Pin $p};Same-Pin $catalog;Same-Pin $exporter
$expected=@(($names+@($catalog.path))|Sort-Object);if((@(Files $folder|Sort-Object)-join"`n")-cne($expected-join"`n")){throw 'Closed export tree changed'}
$nativeExit=$null;$previousPreference=$ErrorActionPreference
try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $exporter.path -EvidenceDirectory $folder;$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$previousPreference}
if($null-eq$nativeExit){throw 'Actual serial exporter exit missing'}
foreach($p in $pins){Same-Pin $p};Same-Pin $catalog;Same-Pin $exporter
$expected=@(($names+@($catalog.path,(Join-Path $folder 'SerialExport.actual.json')))|Sort-Object);if((@(Files $folder|Sort-Object)-join"`n")-cne($expected-join"`n")){throw 'Evidence changed during serial export'}
Write-Output ('Metadata exporter actual exit='+$nativeExit+'; no product or whole-Job pass claim')
exit $nativeExit
