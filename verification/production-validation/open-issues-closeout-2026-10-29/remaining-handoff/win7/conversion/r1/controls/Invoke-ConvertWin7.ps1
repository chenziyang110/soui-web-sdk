[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$PlanFile,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$PlanSha256,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [Parameter(Mandatory=$true)][ValidateSet('ROOT_REVIEWED_ONE_WIN7_BASE_CONVERSION_V1')][string]$RootGate
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$held=[Collections.Generic.List[object]]::new()
$releaseErrors=[Collections.Generic.List[string]]::new()
$failure=$null;$nativeExit=$null;$exitCode=2;$metadataUnchanged=$false;$released=0;$plan=$null;$qcow=$null
function Save-New([string]$Name,$Value){
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12)+[char]10)
  $stream=[IO.File]::Open((Join-Path $EvidenceDirectory $Name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Assert-Plain([string]$File){
  $current=[IO.Path]::GetFullPath($File)
  while($current){
    $item=Get-Item -LiteralPath $current -Force
    if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked conversion source/control forbidden'}
    $parent=[IO.Path]::GetDirectoryName($current);if($parent-eq$current){break};$current=$parent
  }
}
function File-Hash([string]$File){return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Held-Metadata($Entry){
  $row=$Entry.row;$actualLength=$Entry.stream.Length
  $write=[IO.File]::GetLastWriteTimeUtc($row.path).ToFileTimeUtc().ToString()
  if($actualLength-ne[long]$row.bytes-or$write-cne$row.lastWriteFileTime){throw ('Held source metadata changed: '+$row.path)}
  $observed=[ordered]@{path=$row.path;kind=$row.kind;actualLength=$actualLength;actualLastWriteFileTime=$write;shareMode='Read';denyWriteAndDelete=$true}
  if($row.kind-eq'descriptor'){
    $Entry.stream.Position=0;$hasher=[Security.Cryptography.SHA256]::Create()
    try{$digest=[BitConverter]::ToString($hasher.ComputeHash($Entry.stream)).Replace('-','').ToLowerInvariant()}finally{$hasher.Dispose()}
    if($digest-cne$row.sha256){throw 'Held descriptor bytes changed'}
    $Entry.stream.Position=0;$bytes=New-Object byte[] ([int]$actualLength)
    if($Entry.stream.Read($bytes,0,$bytes.Length)-ne$bytes.Length){throw 'Incomplete held descriptor'}
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
    if($text-notmatch'(?m)^CID=([0-9a-f]{8})\r?$'-or$Matches[1]-cne$row.CID){throw 'Descriptor CID differs'}
    if($text-notmatch'(?m)^parentCID=([0-9a-f]{8})\r?$'-or$Matches[1]-cne$row.parentCID){throw 'Descriptor parent CID differs'}
    $names=@([regex]::Matches($text,'(?m)^RW\s+\d+\s+SPARSE\s+"([^"]+)"\r?$')|ForEach-Object{$_.Groups[1].Value})
    if(($names-join"`n")-cne($row.extentNames-join"`n")){throw 'Descriptor extent roster differs'}
    $hint=[regex]::Match($text,'(?m)^parentFileNameHint="([^"]+)"\r?$')
    if($row.parentFileNameHint){if(-not$hint.Success-or$hint.Groups[1].Value-cne$row.parentFileNameHint){throw 'Descriptor parent filename differs'}}
    elseif($hint.Success){throw 'Unexpected base backing filename'}
    $observed.sha256=$digest
  }else{
    $Entry.stream.Position=0;$header=New-Object byte[] 28
    if($Entry.stream.Read($header,0,28)-ne28-or[BitConverter]::ToUInt32($header,0)-ne[uint32]0x564d444b-or[BitConverter]::ToUInt64($header,12)-ne[uint64]$row.sectors){throw 'Sparse extent header/capacity differs'}
    $observed.headerMagicAndCapacityMatched=$true
  }
  $Entry.stream.Position=0
  return [pscustomobject]$observed
}
function Read-Big64([byte[]]$Bytes,[int]$Offset){$part=New-Object byte[] 8;[Array]::Copy($Bytes,$Offset,$part,0,8);[Array]::Reverse($part);return [BitConverter]::ToUInt64($part,0)}
function Read-Big32([byte[]]$Bytes,[int]$Offset){$part=New-Object byte[] 4;[Array]::Copy($Bytes,$Offset,$part,0,4);[Array]::Reverse($part);return [BitConverter]::ToUInt32($part,0)}
try{
  if(-not[Environment]::Is64BitProcess-or$RootGate-cne'ROOT_REVIEWED_ONE_WIN7_BASE_CONVERSION_V1'){throw 'Reviewed x64 Windows PowerShell root required'}
  Assert-Plain $PlanFile
  if((File-Hash $PlanFile)-cne$PlanSha256){throw 'Reviewed conversion plan changed'}
  $plan=[IO.File]::ReadAllText($PlanFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
  if($plan.schema-cne'soui.one-win7-immutable-base-conversion-plan/v1'-or$plan.executionApproved-or$env:COMPUTERNAME-ine$plan.hostName){throw 'Exact unexecuted host plan required'}
  if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EvidenceDirectory))-ine[IO.Path]::GetFullPath($PSScriptRoot)-or-not[IO.Directory]::Exists($EvidenceDirectory)){throw 'Fresh controller-owned evidence directory required'}
  if($plan.sources.Count-ne102-or@($plan.sources|Where-Object{$_.kind-eq'descriptor'}).Count-ne6-or@($plan.sources|Where-Object{$_.kind-eq'extent'}).Count-ne96){throw 'Exact6/96 source roster required'}
  foreach($tool in @($plan.tools.qemuImg,$plan.tools.powershell)){
    Assert-Plain $tool.path;$item=Get-Item -LiteralPath $tool.path -Force
    if($item.Length-ne$tool.bytes-or(File-Hash $tool.path)-cne$tool.sha256){throw 'Pinned conversion tool changed'}
  }
  $names=@('vmware-vmx.exe','vmware.exe','qemu-system-x86_64.exe','qemu-system-i386.exe','VirtualBoxVM.exe','VBoxHeadless.exe')
  $processes=@(Get-CimInstance Win32_Process|Where-Object{$_.Name-in$names}|ForEach-Object{
    $line=[string]$_.CommandLine
    [pscustomobject]@{pid=$_.ProcessId;name=$_.Name;commandLineAvailable=($line.Length-gt0);referencesExactFvmRoot=$line.Replace('\','/').ToLowerInvariant().Contains('f:/vm/')}
  })
  $locks=@(Get-ChildItem -LiteralPath 'F:\vm' -Force|Where-Object{$_.Name-match'(?i)\.(lck|lock)$'}|Select-Object -ExpandProperty Name)
  Save-New 'Occupancy.actual.json' @{observedUtc=[DateTime]::UtcNow.ToString('o');vmProcesses=$processes;sourceLocks=$locks;rawCommandLinesOmitted=$true}
  if(@($processes|Where-Object{-not$_.commandLineAvailable-or$_.referencesExactFvmRoot}).Count-or$locks.Count){throw 'Source VM occupancy unknown/active or lock metadata present'}
  foreach($row in $plan.sources){
    if(-not$row.path.StartsWith('F:/vm/',[StringComparison]::OrdinalIgnoreCase)-or[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($row.path))-ine'F:\vm'){throw 'Source path outside exact selected system-disk directory'}
    Assert-Plain $row.path
    # FileShare.Read rejects writers/deletion for the complete conversion lifetime.
    $stream=[IO.File]::Open($row.path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $held.Add([pscustomobject]@{row=$row;stream=$stream})
    [void](Held-Metadata $held[$held.Count-1])
  }
  $before=@($held|ForEach-Object{Held-Metadata $_})
  Save-New 'Held.before.actual.json' @{heldSourceCount=$held.Count;denyWriteDelete=$true;actualFileIdsRecordedByNodeHeldHandles=$true;fileIdsNotClaimedFromPowerShell=$true;sources=$before}
  $free=[IO.DriveInfo]::new('C:\').AvailableFreeSpace
  if($free-lt[long]$plan.requiredFreeBytes){throw 'C: lacks conservative64GiB plus20GiB headroom'}
  if($plan.destinationRoot-cne'C:/SOUI-SDK-QEMU-Win7'-or$plan.destination-cne'C:/SOUI-SDK-QEMU-Win7/base-win7-sp1-x64.qcow2'-or(Test-Path -LiteralPath $plan.destinationRoot)){throw 'Fresh exact conversion directory required; never overwrite or retry'}
  [void](New-Item -ItemType Directory -Path $plan.destinationRoot -ErrorAction Stop)
  [IO.File]::WriteAllText((Join-Path $plan.destinationRoot 'ownership.json'),(@{schema='soui.one-win7-immutable-base-owner/v1';planSha256=$PlanSha256;evidenceDirectory=$EvidenceDirectory;rootPid=$PID;sourceLeaf=$plan.sourceLeaf;createdUtc=[DateTime]::UtcNow.ToString('o');noVmStarted=$true;releaseReady=$false}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
  $nativeArgs=@('convert','-f','vmdk','-O','qcow2',$plan.sourceLeaf,$plan.destination)
  if(($nativeArgs-join"`n")-cne($plan.qemuArguments-join"`n")){throw 'Exact conversion argv differs'}
  Save-New 'Conversion.invocation.actual.json' @{executable=$plan.tools.qemuImg;args=$nativeArgs;heldSourceCount=$held.Count;rootPid=$PID;actualCFreeBytes=$free;timeoutMs=600000;drainMs=2000;forceShareUsed=$false;vmStarted=$false;fullImageHashPerformed=$false}
  $nativePreference=$ErrorActionPreference
  try{
    $ErrorActionPreference='Continue';$global:LASTEXITCODE=$null
    & $plan.tools.qemuImg.path @nativeArgs 1> (Join-Path $EvidenceDirectory 'qemu.stdout.log') 2> (Join-Path $EvidenceDirectory 'qemu.stderr.log')
    $nativeExit=$LASTEXITCODE
  }finally{$ErrorActionPreference=$nativePreference}
  if($null-eq$nativeExit){throw 'Conversion did not produce an actual native exit code'}
  if($nativeExit-ne0){throw ('Actual qemu-img conversion exit='+$nativeExit)}
  $output=[IO.File]::Open($plan.destination,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try{
    $header=New-Object byte[] 72;if($output.Read($header,0,72)-ne72){throw 'Incomplete qcow2 header'}
    $version=Read-Big32 $header 4;$virtualBytes=Read-Big64 $header 24
    if($header[0]-ne0x51-or$header[1]-ne0x46-or$header[2]-ne0x49-or$header[3]-ne0xfb-or$version-notin@(2,3)-or(Read-Big64 $header 8)-ne0-or(Read-Big32 $header 16)-ne0-or$virtualBytes-ne[uint64]$plan.sourceVirtualBytes){throw 'Expected standalone qcow2 header not observed'}
    $qcow=@{path=$plan.destination;physicalLength=$output.Length;version=$version;virtualBytes=$virtualBytes;backingFileAbsent=$true;headerBytesRead=72;fullImageHashPerformed=$false}
  }finally{$output.Dispose()}
  [IO.File]::SetAttributes($plan.destination,([IO.File]::GetAttributes($plan.destination)-bor[IO.FileAttributes]::ReadOnly))
  $qcow.readOnlyBase=$true;$exitCode=0
}catch{$failure=$_.Exception.Message;$exitCode=2}
finally{
  try{
    $after=@($held|ForEach-Object{Held-Metadata $_});$metadataUnchanged=($held.Count-eq102)
    Save-New 'Held.after.actual.json' @{heldSourceCount=$held.Count;sourceMetadataAndDescriptorPinsUnchanged=$metadataUnchanged;stillDenyWriteDelete=$true;sources=$after}
  }catch{$metadataUnchanged=$false;if(-not$failure){$failure=$_.Exception.Message};$exitCode=2}
  foreach($entry in $held){try{$safeHandle=$entry.stream.SafeFileHandle;$entry.stream.Dispose();if(-not$safeHandle.IsClosed){throw 'Source stream handle still open'};$released++}catch{$releaseErrors.Add($entry.row.path);$exitCode=2}}
  if($releaseErrors.Count){$exitCode=2}
  Save-New 'Root.finished.actual.json' @{schema='soui.one-win7-conversion-root-result/v1';rootPid=$PID;actualNativeExitCode=$nativeExit;rootExitCode=$exitCode;heldSourceCount=$held.Count;sourceMetadataAndDescriptorPinsUnchanged=$metadataUnchanged;releasedSourceCount=$released;releaseFailures=@($releaseErrors.ToArray());failure=$failure;qcow2=$qcow;sourceWriteDeleteDeniedWhileHeld=$true;nativeLogsArePowerShellRedirectionRecords=$true;retryOrOverwriteOrDeletePerformed=$false;vmStarted=$false;productOrCompilerExecuted=$false;fullImageHashPerformed=$false;releaseReady=$false;completedUtc=[DateTime]::UtcNow.ToString('o')}
}
exit $exitCode
