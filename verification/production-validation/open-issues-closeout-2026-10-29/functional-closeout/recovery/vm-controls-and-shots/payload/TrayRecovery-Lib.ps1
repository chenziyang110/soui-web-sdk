# Output-only preparation. Dot-source only from the task VM's read-only D: control ISO.
$ErrorActionPreference='Stop'
$script:CurrentMachine='SOUI-7AEMMK5Q0T'
function Assert-CurrentGuest {
  if($env:COMPUTERNAME-ine$script:CurrentMachine-or-not[Environment]::Is64BitProcess){throw 'Named x64 task VM required; host execution forbidden'}
  if([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')-ine'D:'){throw 'Use the reviewed read-only D: control ISO'}
}
function Write-NewCurrentJson([string]$Path,$Value) {
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 30)+[Environment]::NewLine)
  $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Get-CurrentPin([string]$Path) {
  $item=Get-Item -LiteralPath $Path -Force
  if($item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw ('Plain file required: '+$Path)}
  $bytes=[IO.File]::ReadAllBytes($item.FullName)
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$digest=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  $after=Get-Item -LiteralPath $item.FullName -Force
  if($item.Length-ne$after.Length-or$item.Length-ne$bytes.Length-or$item.LastWriteTimeUtc.Ticks-ne$after.LastWriteTimeUtc.Ticks-or$item.CreationTimeUtc.Ticks-ne$after.CreationTimeUtc.Ticks){throw 'File changed while pinned'}
  return [pscustomobject]@{path=$item.FullName;bytes=$bytes.Length;sha256=$digest}
}
function Assert-CurrentPin($Pin) {
  $actual=Get-CurrentPin ([string]$Pin.path)
  if($actual.bytes-ne$Pin.bytes-or$actual.sha256-cne$Pin.sha256){throw ('Reviewed bytes differ: '+$Pin.path)}
}
function Get-CurrentFiles([string]$Directory) {
  $item=Get-Item -LiteralPath $Directory -Force
  if(-not$item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain evidence directory required'}
  foreach($child in Get-ChildItem -LiteralPath $item.FullName -Force){
    if($child.Attributes-band[IO.FileAttributes]::ReparsePoint){throw ('Linked evidence forbidden: '+$child.FullName)}
    if($child.PSIsContainer){Get-CurrentFiles $child.FullName}else{$child.FullName}
  }
}
function Get-CurrentPaths([string]$Architecture='stage') {
  if($Architecture-cnotmatch'^(stage|x64|x86)$'){throw 'One exact stage/x64/x86 command required'}
  return [pscustomobject]@{kind='tray-recovery';stamp='a1';architecture=$Architecture;root='C:\trayrc-a1';
    manifestName='TrayRecovery.manifest.json';schema='soui.current-tray-recovery-light-payload/v1';
    controller='Guest-TrayRecovery.ps1';nativeRootGate='ROOT_REVIEWED_TRAY_RECOVERY_STAGE_V1';
    stage='C:\issue-closeout-evidence\trayrc-a1-stage';
    run=('C:\issue-closeout-evidence\trayrc-a1-'+$Architecture)}
}
function Read-CurrentManifest($Paths,[string]$Root,[string]$Sha256) {
  if($Sha256-cnotmatch'^[a-f0-9]{64}$'){throw 'Exact reviewed manifest SHA256 required'}
  $file=Join-Path $Root $Paths.manifestName;$pin=Get-CurrentPin $file
  if($pin.sha256-cne$Sha256){throw 'Current manifest identity differs'}
  $plan=[IO.File]::ReadAllText($file,[Text.Encoding]::UTF8)|ConvertFrom-Json
  if($plan.schema-cne$Paths.schema-or$plan.version-cne'0.6.0'-or$plan.preparationHost-ieq$env:COMPUTERNAME-or@($plan.payloadFiles).Count-ne8){throw 'Exact tray recovery nine-file control payload required'}
  $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $selection=[Collections.Generic.List[object]]::new()
  foreach($entry in $plan.payloadFiles){
    if([string]$entry.relative-cnotmatch'^[A-Za-z0-9][A-Za-z0-9_.-]+$'-or-not$names.Add([string]$entry.relative)-or$entry.sha256-cnotmatch'^[a-f0-9]{64}$'){throw 'Unique flat payload names and hashes required'}
    $source=Join-Path $Root $entry.relative;$actual=Get-CurrentPin $source
    if($actual.bytes-ne$entry.bytes-or$actual.sha256-cne$entry.sha256){throw ('Current payload differs: '+$entry.relative)}
    $selection.Add([pscustomobject]@{relative=[string]$entry.relative;path=$actual.path;bytes=$actual.bytes;sha256=$actual.sha256})
  }
  if(-not$names.Add($Paths.manifestName)){throw 'Manifest cannot be a payload member'}
  $selection.Add([pscustomobject]@{relative=$Paths.manifestName;path=$pin.path;bytes=$pin.bytes;sha256=$pin.sha256})
  if(-not$names.Contains($Paths.controller)){throw 'Original reviewed controller missing'}
  return [pscustomobject]@{file=$file;pin=$pin;plan=$plan;selection=@($selection.ToArray())}
}
function Get-CurrentPowerShell {
  $file=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw 'Guest Windows PowerShell unavailable'}
  return $file
}
function Invoke-CurrentController($Paths,[string]$Mode,[string]$ManifestSha256,[string]$StageSha256) {
  $evidence=if($Mode-ceq'stage'){$Paths.stage}else{$Paths.run}
  if(Test-Path -LiteralPath $evidence){throw 'Preserve prior current evidence'}
  $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $Paths.root $Paths.controller),
    '-Mode',$Mode,'-Architecture',$Paths.architecture,'-Manifest',(Join-Path $Paths.root $Paths.manifestName),'-ManifestSha256',$ManifestSha256,
    '-ExpectedMachine',$script:CurrentMachine,'-EvidenceDirectory',$evidence)
  if($Mode-ceq'run'){$arguments+=@('-RootGate',$Paths.nativeRootGate,'-StageFile',(Join-Path $Paths.stage 'Stage.actual.json'),'-StageSha256',$StageSha256)}
  $process=[Diagnostics.Process]::GetCurrentProcess()
  $invocation=[ordered]@{schema='soui.current-guest-wrapper-invocation/v1';mode=$Mode;kind=$Paths.kind;
    wrapperPid=$PID;wrapperCreationFileTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();
    executable=(Get-CurrentPowerShell);arguments=$arguments;manifestSha256=$ManifestSha256;stageSha256=$StageSha256;
    controller=(Get-CurrentPin (Join-Path $Paths.root $Paths.controller));deadlineChanged=$false;automaticStageToRun=$false;productExecutedByWrapper=$false;releaseReady=$false}
  $nativeExit=$null;$failure=$null
  try{
    $previous=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& $invocation.executable @arguments|Out-Host;$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
    if($null-eq$nativeExit){throw 'Actual controller process exit missing'}
  }catch{$failure=$_.Exception.Message}
  if(Test-Path -LiteralPath $evidence){
    Write-NewCurrentJson (Join-Path $evidence 'Wrapper.Invocation.actual.json') $invocation
    Write-NewCurrentJson (Join-Path $evidence 'Wrapper.finished.json') @{nativeExitCode=$nativeExit;failure=$failure;wrapperPid=$PID;wrapperCreationFileTime=$invocation.wrapperCreationFileTime;controllerReturned=($null-ne$nativeExit);productExecutedByWrapper=$false;releaseReady=$false}
  }
  if($failure){throw $failure}
  if($null-eq$nativeExit){throw 'Controller process did not return an exit code'}
  return [int]$nativeExit
}
function Assert-CurrentProcessEnded($ProcessIdValue,[string]$CreationFileTime) {
  $numericId=[long]$ProcessIdValue
  if($numericId-le0-or$numericId-gt[int]::MaxValue-or$CreationFileTime-cnotmatch'^[0-9]{10,20}$'){throw 'Actual PID and birth required'}
  # Use the same exact Process.StartTime FILETIME as the original lease.
  # WMI/DMTF creation times can truncate sub-microsecond ticks and must not
  # turn a still-live matching process into a supposed reused PID.
  $actual=$null
  try{$actual=[Diagnostics.Process]::GetProcessById([int]$numericId)}catch [ArgumentException]{
    return [pscustomobject]@{pid=$numericId;creationFileTime=$CreationFileTime;identityAbsent=$true;pidReused=$false}
  }
  try{
    $birth=$actual.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
    if($birth-ceq$CreationFileTime){throw 'Evidence owner process is still running'}
    if([long]$birth-lt[long]$CreationFileTime){throw 'Observed process birth precedes the recorded identity'}
    return [pscustomobject]@{pid=$numericId;creationFileTime=$CreationFileTime;identityAbsent=$true;pidReused=$true;observedDifferentBirth=$birth}
  }finally{$actual.Dispose()}
}
function Read-CurrentClosedLauncher([string]$Evidence) {
  $leaseFile=Join-Path $Evidence 'Lease.actual.json';$finishedFile=Join-Path $Evidence 'PowerShell.finished.json'
  $lease=[IO.File]::ReadAllText($leaseFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
  $finished=[IO.File]::ReadAllText($finishedFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
  if(-not$lease.acquired-or$lease.name-cne'Local\SouiDevKit.DesktopValidation'-or$null-eq$finished.exitCode){throw 'Actual controller lease/finished records required'}
  $ended=Assert-CurrentProcessEnded $lease.ownerProcessId ([string]$lease.ownerCreationFileTime)
  $wrapperFile=Join-Path $Evidence 'Wrapper.finished.json'
  if(-not(Test-Path -LiteralPath $wrapperFile -PathType Leaf)){throw 'Current wrapper finished record required'}
  $wrapper=[IO.File]::ReadAllText($wrapperFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
  $wrapperEnded=Assert-CurrentProcessEnded $wrapper.wrapperPid ([string]$wrapper.wrapperCreationFileTime)
  $inputsFile=Join-Path $Evidence 'Inputs.actual.json'
  $inputs=[IO.File]::ReadAllText($inputsFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
  $controllerId=[long]$inputs.controllerPid
  if($controllerId-le0-or$controllerId-gt[int]::MaxValue){throw 'Recorded actual top Node controller PID required'}
  if($inputs.controllerParentPid-ne$lease.ownerProcessId){throw 'Recorded Node parent differs from the actual PowerShell lease owner'}
  $nodeProcess=$null
  try{$nodeProcess=[Diagnostics.Process]::GetProcessById([int]$controllerId)}catch [ArgumentException]{}
  if($null-ne$nodeProcess){$nodeProcess.Dispose();throw 'Recorded Node PID still exists; no birth was collected and PID reuse cannot be assumed'}
  $nodeEnded=@{record=(Get-CurrentPin $inputsFile);controllerPid=$controllerId;pidAbsentObserved=$true;birthNotCollected=$true;pidReuseAssumed=$false}
  return [pscustomobject]@{nodeController=$nodeEnded;lease=(Get-CurrentPin $leaseFile);finished=(Get-CurrentPin $finishedFile);finishedValue=$finished;
    launcherIdentity=$ended;wrapperFinished=(Get-CurrentPin $wrapperFile);wrapperIdentity=$wrapperEnded}
}
