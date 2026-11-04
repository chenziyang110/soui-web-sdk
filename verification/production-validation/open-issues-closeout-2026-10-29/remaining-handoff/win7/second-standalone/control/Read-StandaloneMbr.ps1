$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root='F:/vm/Windows 7 x64'
$descriptor=Join-Path $root 'Windows 7 x64.vmdk'
$out=$PSScriptRoot
$occupancyHelper='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/win7-conversion-current/r2/Read-VmOccupancy.ps1'
$plan='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/win7-conversion-current/r2/Conversion.plan.prepared.json'
$planSha='de0b8c132efa2d691579d7da43bdc5d07beb35fd030ab47fe142160d0180ca97'
$qemuIo='C:/Users/11034/.codex/task-tools/qemu-ucrt64-11.1.2-e89cf96f2d1e4685bbdac347cb9eea3d/runtime/ucrt64/bin/qemu-io.exe'
$held=[Collections.Generic.List[object]]::new();$released=0;$failure=$null;$nativeExit=$null;$before=$null;$after=$null
function Save-New([string]$Name,$Value){$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12)+[char]10);$stream=[IO.File]::Open((Join-Path $out $Name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}}
function Hash([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Plain([string]$Path){$p=[IO.Path]::GetFullPath($Path);while($p){$item=Get-Item -LiteralPath $p -Force;if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked input refused'};$parent=[IO.Path]::GetDirectoryName($p);if($p-eq$parent){break};$p=$parent}}
function Snapshot {return @($held|ForEach-Object{[pscustomobject]@{path=$_.path;bytes=$_.stream.Length;lastWriteFileTime=[IO.File]::GetLastWriteTimeUtc($_.path).ToFileTimeUtc().ToString()}})}
try{
  if((Hash $occupancyHelper)-cne'2ec5ce861879912f1cc01dad560dff770e5cc2226757b7217314c026c9dde297'-or(Hash $plan)-cne$planSha-or(Hash $qemuIo)-cne'e75571f9b249ca861d121d92d113f39c1195a1c5ae454a1b29d1a24db405690e'){throw 'Pinned read-only tools changed'}
  $occupancy=& $occupancyHelper -PlanFile $plan -PlanSha256 $planSha
  Save-New 'Occupancy.actual.json' (($occupancy-join"`n")|ConvertFrom-Json)
  Plain $root
  $locks=@(Get-ChildItem -LiteralPath $root -Force|Where-Object{$_.Name-match'(?i)\.(lck|lock)$'})
  if($locks.Count){throw 'Standalone source lock metadata present'}
  Plain $descriptor
  $stream=[IO.File]::Open($descriptor,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $held.Add([pscustomobject]@{path=$descriptor;stream=$stream})
  if($stream.Length-ne1127){throw 'Standalone descriptor length changed'}
  $hasher=[Security.Cryptography.SHA256]::Create();try{$digest=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$hasher.Dispose()}
  if($digest-cne'9e2cacdce95eb7f53c376c3e647eb9ab61e9861544768e0e120cffc08a71b8c3'){throw 'Standalone descriptor bytes changed'}
  $stream.Position=0;$bytes=New-Object byte[] 1127;if($stream.Read($bytes,0,1127)-ne1127){throw 'Descriptor read incomplete'}
  $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
  if($text-notmatch'(?m)^parentCID=ffffffff\r?$'-or$text-match'(?m)^parentFileNameHint='){throw 'Standalone parent chain is not empty'}
  $extents=@([regex]::Matches($text,'(?m)^RW\s+(\d+)\s+SPARSE\s+"([^"]+)"\r?$'))
  if($extents.Count-ne16){throw 'Exact sixteen standalone extents required'}
  $sectors=[uint64]0
  foreach($extent in $extents){
    $name=$extent.Groups[2].Value;if($name-notmatch'^Windows 7 x64-s\d{3}\.vmdk$'){throw 'Unexpected extent path'}
    $file=Join-Path $root $name;Plain $file
    $stream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $held.Add([pscustomobject]@{path=$file;stream=$stream})
    $header=New-Object byte[] 28
    if($stream.Read($header,0,28)-ne28-or[BitConverter]::ToUInt32($header,0)-ne[uint32]0x564d444b-or[BitConverter]::ToUInt64($header,12)-ne[uint64]$extent.Groups[1].Value){throw 'Standalone extent header/capacity differs'}
    $sectors+=[uint64]$extent.Groups[1].Value
  }
  if($held.Count-ne17-or$sectors-ne125829120){throw 'Exact standalone source set/capacity differs'}
  $before=Snapshot
  Save-New 'Read.invocation.actual.json' @{executable=$qemuIo;sha256=(Hash $qemuIo);args=@('-r','-f','vmdk',$descriptor,'-c','read -v 0 512');readOnly=$true;virtualOffset=0;virtualReadBytes=512;heldSourceCount=$held.Count;denySourceWriteDelete=$true;before=$before;noOtherGuestDataRequested=$true}
  $preference=$ErrorActionPreference
  try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& $qemuIo '-r' '-f' 'vmdk' $descriptor '-c' 'read -v 0 512' 1> (Join-Path $out 'qemu-io.stdout.log') 2> (Join-Path $out 'qemu-io.stderr.log');$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$preference}
  if($null-eq$nativeExit-or$nativeExit-ne0){throw 'Read-only MBR command did not exit native0'}
  $after=Snapshot
  if(($before|ConvertTo-Json -Compress)-cne($after|ConvertTo-Json -Compress)){throw 'Held standalone metadata changed'}
  if((Hash $descriptor)-cne'9e2cacdce95eb7f53c376c3e647eb9ab61e9861544768e0e120cffc08a71b8c3'-or(Hash $occupancyHelper)-cne'2ec5ce861879912f1cc01dad560dff770e5cc2226757b7217314c026c9dde297'-or(Hash $plan)-cne$planSha-or(Hash $qemuIo)-cne'e75571f9b249ca861d121d92d113f39c1195a1c5ae454a1b29d1a24db405690e'){throw 'Read-only source/control pin changed'}
}catch{$failure=$_.Exception.Message}
finally{
  foreach($entry in $held){try{$handle=$entry.stream.SafeFileHandle;$entry.stream.Dispose();if(-not$handle.IsClosed){throw 'Source read handle remains open'};$released++}catch{if(-not$failure){$failure=$_.Exception.Message}}}
  $passed=($null-eq$failure-and$nativeExit-eq0-and$held.Count-eq17-and$released-eq17-and$null-ne$before-and$null-ne$after)
  Save-New 'Read.finished.actual.json' @{schema='soui.standalone-win7-mbr-readonly/v1';passed=$passed;failure=$failure;actualNativeExitCode=$nativeExit;heldSourceCount=$held.Count;releasedSourceCount=$released;fileAccess='Read';fileShare='Read';sameHeldLengthAndLastWriteBeforeAfter=($null-ne$before-and$null-ne$after);fileIdsNotClaimedFromPowerShell=$true;before=$before;after=$after;virtualOffset=0;virtualReadBytes=512;sourceFullHashPerformed=$false;sourceWrite=$false;vmStarted=$false;conversionOrCopy=$false;personalFilesOrMftParsed=$false;sourceVmxChanged=$false;logsArePowerShellNativeRedirectionRecords=$true}
}
if($failure){Write-Error $failure;exit 2}
