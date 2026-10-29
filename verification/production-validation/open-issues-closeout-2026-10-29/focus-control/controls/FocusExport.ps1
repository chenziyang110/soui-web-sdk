[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
if($env:COMPUTERNAME-ine'SOUI-7AEMMK5Q0T'-or-not[Environment]::Is64BitProcess-or[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')-ine'D:'){throw 'Reviewed named x64 task VM and D: control ISO only'}
$folder='C:\issue-closeout-evidence\focusc-a1-run'
function FocusPin([string]$File){$item=Get-Item -LiteralPath $File -Force;if($item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain focus evidence file required'};return @{path=$item.FullName;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}}
function CheckFocusPin($Pin){$actual=FocusPin $Pin.path;if($actual.bytes-ne$Pin.bytes-or$actual.sha256-cne$Pin.sha256){throw ('Changed focus evidence: '+$Pin.path)}}
function FocusFiles([string]$Directory){$item=Get-Item -LiteralPath $Directory -Force;if(-not$item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain focus directory required'};foreach($child in Get-ChildItem -LiteralPath $item.FullName -Force){if($child.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked focus evidence forbidden'};if($child.PSIsContainer){FocusFiles $child.FullName}else{$child.FullName}}}
function ReadFocus([string]$Name){return [IO.File]::ReadAllText((Join-Path $folder $Name),[Text.Encoding]::UTF8)|ConvertFrom-Json}
function NewFocusJson([string]$File,$Value){$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 20)+[Environment]::NewLine);$stream=[IO.File]::Open($File,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}}
function AssertFocusGone([int]$ProcessIdValue,[string]$Birth){
  if($ProcessIdValue-le0){throw 'Positive actual PID required'}
  $process=$null
  try{$process=[Diagnostics.Process]::GetProcessById($ProcessIdValue)}catch [ArgumentException]{return}
  try{
    if(-not$Birth){throw 'Controller PID still exists; no birth was collected, so PID reuse cannot be assumed'}
    if($Birth-cnotmatch'^[0-9]{10,20}$'){throw 'Exact root birth required'}
    $actual=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
    if($actual-ceq$Birth-or[long]$actual-lt[long]$Birth){throw 'Original focus root identity still exists or birth is inconsistent'}
  }finally{$process.Dispose()}
}
$summary=ReadFocus 'Summary.actual.json';$invocation=ReadFocus 'Invocation.actual.json';$result=ReadFocus 'finished.json';$ready=ReadFocus 'ready.json';$context=ReadFocus 'owned.ndjson.context.json'
if($summary.schema-cne'soui.native-focus-owned-result/v1'-or$summary.machine-cne'SOUI-7AEMMK5Q0T'-or$invocation.timeoutMs-ne90000-or$invocation.drainMs-ne2000-or$invocation.innerDeadlineMs-ne60000){throw 'Frozen focus supervision identity differs'}
if($result.signal-or$result.spawnError-or$result.reportError-or-not$result.closureConfirmed-or-not$result.terminal.rootExited-or$result.terminal.activeProcesses-ne0){throw 'Native focus Job closure is not confirmed; no export while owned processes can write'}
if($ready.pid-le0-or$ready.pid-gt[int]::MaxValue-or$ready.creationFileTime-cnotmatch'^[0-9]{10,20}$'-or$ready.pid-ne$result.pid-or$ready.creationFileTime-cne$result.creationFileTime-or$context.timeoutMs-ne90000-or$context.drainMs-ne2000-or$result.reportSha256-cne(FocusPin (Join-Path $folder 'owned.ndjson')).sha256){throw 'Actual owned context/identity/raw differs'}
AssertFocusGone ([int]$ready.pid) ([string]$ready.creationFileTime)
AssertFocusGone ([int]$summary.controllerPid) ''
if($summary.controllerPid-ne$invocation.controllerPid){throw 'Controller identity differs'}
# Nonzero/invalid/forced outcomes are preserved. Closure permits transport and
# never upgrades a control result or an SDK qualification.
foreach($pin in $invocation.sourcePins){CheckFocusPin $pin}
$self=$invocation.sourcePins|Where-Object{[IO.Path]::GetFullPath($_.path)-ieq[IO.Path]::GetFullPath($PSCommandPath)}
if(@($self).Count-ne1){throw 'The exact reviewed focus export script was not pinned before the control'}
foreach($name in @('ExportFiles.actual.json','SerialExport.actual.json','ExportPreflight.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $name)){throw 'One export only; preserve previous exact catalog/marker'}}
$exporter=FocusPin 'C:\issues8\Guest-Export.ps1'
if($exporter.sha256-cne'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c'){throw 'Original unchanged serial exporter differs'}
NewFocusJson (Join-Path $folder 'ExportPreflight.actual.json') @{schema='soui.native-focus-closed-export/v1';rootPid=$ready.pid;rootCreationFileTime=$ready.creationFileTime;rootIdentityAbsent=$true;controllerPid=$summary.controllerPid;controllerPidAbsent=$true;controllerBirthNotCollected=$true;observedUtc=[DateTime]::UtcNow.ToString('o');exporter=$exporter;nativeRootExitCode=$result.exitCode;testPassed=$null;releaseReady=$false}
$files=@(FocusFiles $folder|Sort-Object);if($files.Count-lt1-or$files.Count-gt512){throw 'Bounded complete focus file count required'}
$pins=@();$total=[long]0
foreach($file in $files){$pin=FocusPin $file;$pins+=,$pin;$total+=$pin.bytes;if($pin.bytes-gt16777216-or$total-gt8388608){throw 'Preserve oversized full evidence; no truncation'}}
$catalog=Join-Path $folder 'ExportFiles.actual.json'
NewFocusJson $catalog @{schema='soui.issue-short-export-files/v1';files=$pins;totalBytes=$total;scope='Exact closed outer focus diagnostic and only its five named copied inner logs; no whole serial/private roots';testPassed=$null;releaseReady=$false}
$catalogPin=FocusPin $catalog;if($total+$catalogPin.bytes-gt8388608){throw 'Complete export exceeds8MiB'}
foreach($pin in $pins){CheckFocusPin $pin}
$actual=@(FocusFiles $folder|Sort-Object);$expected=@(($files+@($catalog))|Sort-Object);if(($actual-join"`n")-cne($expected-join"`n")){throw 'Focus evidence membership changed'}
$nativeExit=$null;$previous=$ErrorActionPreference
try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $exporter.path -EvidenceDirectory $folder;$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
if($null-eq$nativeExit){throw 'Actual serial-export exit missing'}
foreach($pin in $pins){CheckFocusPin $pin};CheckFocusPin $catalogPin;CheckFocusPin $exporter
$actual=@(FocusFiles $folder|Sort-Object);$expected=@(($files+@($catalog,(Join-Path $folder 'SerialExport.actual.json')))|Sort-Object);if(($actual-join"`n")-cne($expected-join"`n")){throw 'Focus evidence changed during export'}
Write-Output ('Actual focus exporter exit='+$nativeExit+'; control/native failures remain unchanged')
exit $nativeExit
