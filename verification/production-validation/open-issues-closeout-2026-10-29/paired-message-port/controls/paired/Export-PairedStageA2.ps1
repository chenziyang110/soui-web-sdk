[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$folder='C:\issue-closeout-evidence\mp77p-stage-a2'
if([IntPtr]::Size-ne8-or$env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'){throw 'Task VM x64 only'}
$utf8=[Text.UTF8Encoding]::new($false)
function Pin([string]$File){$i=Get-Item -LiteralPath $File -Force;if($i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain evidence file required'};[ordered]@{path=$i.FullName;bytes=$i.Length;sha256=(Get-FileHash -LiteralPath $i.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Same-Pin($Expected){$a=Pin $Expected.path;if($a.bytes-ne$Expected.bytes-or$a.sha256-cne$Expected.sha256){throw 'Evidence/source pin changed'}}
function Write-New([string]$File,$Value){$b=$utf8.GetBytes(($Value|ConvertTo-Json -Depth 12)+[char]10);$s=[IO.File]::Open($File,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$s.Write($b,0,$b.Length)}finally{$s.Dispose()}}
function Files([string]$Directory){$i=Get-Item -LiteralPath $Directory -Force;if(-not$i.PSIsContainer-or($i.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain evidence directory required'};foreach($e in Get-ChildItem -LiteralPath $Directory -Force){if($e.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked member rejected'};if($e.PSIsContainer){Files $e.FullName}else{$e.FullName}}}
foreach($n in @('ExportPreflight.actual.json','ExportFiles.actual.json','SerialExport.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $n)){throw 'One fresh complete export only'}}
$finishedPin=Pin (Join-Path $folder 'PowerShell.finished.json');$finished=Get-Content -LiteralPath $finishedPin.path -Raw|ConvertFrom-Json
if($finished.exitCode-ne0-or$finished.actualNativeExitCode-ne0-or-not$finished.environmentRestored-or-not$finished.leaseReleased-or$finished.pathChangedByStage-or@($finished.restoreErrors).Count){throw 'Original stage wrapper has not returned cleanly; preserve actual failure'}
$leasePin=Pin (Join-Path $folder 'Lease.actual.json');$lease=Get-Content -LiteralPath $leasePin.path -Raw|ConvertFrom-Json
if($lease.name-cne'Local\SouiDevKit.DesktopValidation'-or-not$lease.acquired-or$lease.mode-cne'stage'-or$lease.ownerProcessId-le0-or[string]$lease.ownerCreationFileTime-notmatch'^[0-9]{17,20}$'){throw 'Actual stage parent identity missing'}
$summaryPin=Pin (Join-Path $folder 'Summary.actual.json');$summary=Get-Content -LiteralPath $summaryPin.path -Raw|ConvertFrom-Json
if($summary.schema-cne'soui.fresh-paired-guest-stage-result/v1'-or-not$summary.stagingCompleted-or$summary.executionEnabled-or$summary.productExecuted-or$summary.process.pid-le0-or$summary.process.parentPid-ne$lease.ownerProcessId){throw 'Actual metadata stage result/owner differs'}
$owner=Get-Process -Id ([int]$lease.ownerProcessId) -ErrorAction SilentlyContinue
$ownerReused=$false
if($null-ne$owner){try{$observedBirth=$owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();if([UInt64]::Parse($observedBirth)-lt[UInt64]::Parse([string]$lease.ownerCreationFileTime)){throw 'Observed PID birth predates recorded owner; inconsistent identity'};if($observedBirth-ceq[string]$lease.ownerCreationFileTime-and-not$owner.HasExited){throw 'Original PowerShell owner still alive'};$ownerReused=($observedBirth-cne[string]$lease.ownerCreationFileTime)}finally{$owner.Dispose()}}
# The metadata script did not capture Node birth. Any occupant of that PID is
# therefore refused, rather than guessing a reused PID or touching a process.
$node=Get-Process -Id ([int]$summary.process.pid) -ErrorAction SilentlyContinue
if($null-ne$node){$node.Dispose();throw 'Recorded metadata Node PID is still present; fail closed'}
$exporter=Pin 'C:\issues8\Guest-Export.ps1'
if($exporter.sha256-cne'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c'){throw 'Original serial exporter changed'}
Write-New (Join-Path $folder 'ExportPreflight.actual.json') @{schema='soui.fresh-paired-metadata-closed-export-preflight/v1';actualMachine=$env:COMPUTERNAME;powerShellFinished=$finishedPin;stageSummary=$summaryPin;lease=$leasePin;originalParentIdentityAbsent=$true;parentPidCurrentlyReused=$ownerReused;recordedNodePidAbsent=$true;nodeBirthRecorded=$false;exporter=$exporter;scope='Metadata stage only; no whole Native Job or browser-pass inference';testPassed=$null;productExecuted=$false;releaseReady=$false}
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
