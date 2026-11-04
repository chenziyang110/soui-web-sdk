#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Inputs,[Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$InputsSha256)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Need($value,$why){if(-not$value){throw $why}}
function Native([string]$name){return [IO.Path]::GetFullPath($name)}
function Small($record){
 Need ($record.bytes-ge0-and$record.bytes-le16777216) 'Bounded metadata required'
 $entry=Get-Item -LiteralPath $record.path -Force
 Need (-not$entry.PSIsContainer-and(($entry.Attributes-band[IO.FileAttributes]::ReparsePoint)-eq0)) 'Plain metadata required'
 $stream=[IO.File]::Open($record.path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{
  Need ($stream.Length-eq$record.bytes) 'Metadata length changed'
  $bytes=[byte[]]::new([int]$record.bytes);$done=0
  while($done-lt$bytes.Length){$count=$stream.Read($bytes,$done,$bytes.Length-$done);Need ($count-gt0) 'Metadata short read';$done+=$count}
  Need ($stream.ReadByte()-eq-1-and[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()-ceq$record.sha256) 'Metadata hash/EOF mismatch'
  return ,$bytes
 }finally{$stream.Dispose()}
}
function NewJson($name,$value){
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value|ConvertTo-Json -Depth 18)+[char]10)
 $stream=[IO.File]::Open($name,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{$stream.Write($bytes);$stream.Flush($true)}finally{$stream.Dispose()}
}
Need ([Threading.Thread]::CurrentThread.GetApartmentState()-eq[Threading.ApartmentState]::STA-and[Environment]::Is64BitProcess-and$PSVersionTable.PSVersion.ToString()-ceq'7.6.5') 'Fixed x64 PowerShell7.6.5 STA required'
$inputsInfo=Get-Item -LiteralPath $Inputs
$frozen=[Text.Encoding]::UTF8.GetString((Small ([pscustomobject]@{path=$Inputs;bytes=$inputsInfo.Length;sha256=$InputsSha256})))|ConvertFrom-Json
Need ($frozen.schema-ceq'soui.issue77-data-iso-writer-inputs/v1') 'Reviewed writer input schema required'
$plan=[Text.Encoding]::UTF8.GetString((Small $frozen.sourcePlan))|ConvertFrom-Json
Need ($plan.schema-ceq'soui.open-issues-control-data-iso-source/v1'-and$plan.files.Count-eq6-and$plan.fileSystems-eq4-and$plan.maxIsoBytes-eq1400000000-and$plan.initialWholeJobTimeoutMs-eq120000-and$plan.drainMs-eq2000) 'Original closed source/limits required'
Need ($frozen.actualFileSystems-eq4-and$frozen.udfRevision-eq0x201) 'Explicit strict UDF2.01 representation required for the unchanged deep tree'
$base=Native ([IO.Path]::GetDirectoryName($frozen.sourcePlan.path))
Need ((Native $plan.sourceRoot)-ceq(Join-Path $base 'iso-tree')-and(Native $plan.output)-ceq(Join-Path $base 'SouiOpenIssues.iso')-and(Native $plan.scratch)-ceq(Join-Path $base 'imapi-scratch')) 'Output stays in the fresh carrier directory'
$attempt=Native $frozen.attemptRoot
Need ($attempt.StartsWith($base+'\iso-writer-r3-',[StringComparison]::OrdinalIgnoreCase)) 'Fresh bounded attempt root required'
$scratch=Join-Path $attempt 'imapi-scratch'
Need (-not(Test-Path -LiteralPath $plan.output)-and-not(Test-Path -LiteralPath $scratch)-and-not(Test-Path -LiteralPath (Join-Path $attempt 'IsoWriter.result.json'))) 'Preserve prior ISO outputs'
Need ($frozen.originalHelper.bytes-eq11926-and$frozen.originalHelper.sha256-ceq'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72'-and$frozen.helper.sha256-ceq'00789c652e6e2c3639ee2896c05e56023974fc6ef8d334b8c4d5098d3003417a') 'Exact one-line long-path derivative and original helper required'
$helper=Small $frozen.helper
$null=Small $frozen.originalHelper;$null=Small $frozen.writer;$null=Small $frozen.trustedOriginalWriter;$null=Small $frozen.comServer
Add-Type -TypeDefinition ([Text.Encoding]::UTF8.GetString($helper)) -Language CSharp
$leases=[Collections.Generic.List[object]]::new();$members=[Collections.Generic.List[object]]::new();$cleanup=[Collections.Generic.List[string]]::new()
$fsi=$null;$rootItem=$null;$result=$null;$image=$null;$primary=$null;$success=$null
function Closed{
 $actualFiles=@(Get-ChildItem -LiteralPath $plan.sourceRoot -File -Recurse -Force|ForEach-Object{$_.FullName.Substring((Native $plan.sourceRoot).Length+1).Replace('\','/')})
 $actualDirs=@(Get-ChildItem -LiteralPath $plan.sourceRoot -Directory -Recurse -Force|ForEach-Object{Need (($_.Attributes-band[IO.FileAttributes]::ReparsePoint)-eq0) 'No directory reparse';$_.FullName.Substring((Native $plan.sourceRoot).Length+1).Replace('\','/')})
 Need ($actualFiles.Count-eq$plan.files.Count-and@(Compare-Object @($plan.files.path|Sort-Object -CaseSensitive) @($actualFiles|Sort-Object -CaseSensitive) -CaseSensitive).Count-eq0-and@(Compare-Object @($plan.directories|Sort-Object -CaseSensitive) @($actualDirs|Sort-Object -CaseSensitive) -CaseSensitive).Count-eq0) 'ISO source file/directory closed set changed'
}
try{
 $leases.Add([SouiDataIso4ddce37c.ReadLease]::new((Native $plan.sourceRoot),$true))
 foreach($directory in $plan.directories){$leases.Add([SouiDataIso4ddce37c.ReadLease]::new((Native (Join-Path $plan.sourceRoot $directory)),$true))}
 foreach($record in @($frozen.helper,$frozen.originalHelper,$frozen.writer,$frozen.trustedOriginalWriter,$frozen.sourcePlan)){
  $lease=[SouiDataIso4ddce37c.ReadLease]::new((Native $record.path),$false);$leases.Add($lease);$null=$lease.Hash($record.bytes,$record.sha256)
 }
 $inputLease=[SouiDataIso4ddce37c.ReadLease]::new((Native $Inputs),$false);$leases.Add($inputLease);$null=$inputLease.Hash($inputsInfo.Length,$InputsSha256)
 Closed
 foreach($record in $plan.files){
  Need ($record.bytes-ge0-and$record.bytes-le353102192) 'Original bounded member size required'
  $lease=[SouiDataIso4ddce37c.ReadLease]::new((Native (Join-Path $plan.sourceRoot $record.path)),$false)
  $leases.Add($lease);$members.Add([pscustomobject]@{row=$record;lease=$lease;before=$lease.Hash($record.bytes,$record.sha256)})
 }
 NewJson (Join-Path $attempt 'IsoWriter.pre.json') ([ordered]@{inputsSha256=$InputsSha256;files=@($members|ForEach-Object{$_.before});closedFiles=$plan.files.Count;closedDirectories=$plan.directories.Count})
 $clsid=(Get-Item -LiteralPath 'Registry::HKEY_CLASSES_ROOT\IMAPI2FS.MsftFileSystemImage\CLSID').GetValue('')
 Need ($clsid-ceq'{2C941FC5-975B-59BE-A960-9A2A262853A5}'-and(Get-Item -LiteralPath ('Registry::HKEY_CLASSES_ROOT\CLSID\'+$clsid+'\InprocServer32')).GetValue('')-ieq'C:\Windows\System32\imapi2fs.dll') 'Original trusted system COM server required'
 [SouiDataIso4ddce37c.IsoIO]::NewDirectory((Native $scratch))
 $fsi=New-Object -ComObject IMAPI2FS.MsftFileSystemImage
 $fsi.ChooseImageDefaultsForMediaType(6);$fsi.FileSystemsToCreate=4;$fsi.UDFRevision=0x201;$fsi.StrictFileSystemCompliance=$true
 $fsi.VolumeName='SOUI_CTRL9';$fsi.WorkingDirectory=Native $scratch;$fsi.SessionStartBlock=0
 $rootItem=$fsi.Root;$rootItem.AddTree(('\\?\'+(Native $plan.sourceRoot)),$false)
 $result=$fsi.CreateResultImage();$image=$result.ImageStream
 $iso=[SouiDataIso4ddce37c.IsoIO]::CopyImage($image,(Native $plan.output),$plan.maxIsoBytes)
 Closed
 $post=@($members|ForEach-Object{[ordered]@{before=$_.before;after=$_.lease.Hash($_.row.bytes,$_.row.sha256)}})
 $null=Small $frozen.comServer
 NewJson (Join-Path $attempt 'IsoWriter.post.json') ([ordered]@{allExact=$true;files=$post;closedFiles=$plan.files.Count;closedDirectories=$plan.directories.Count})
 $success=[ordered]@{schema='soui.open-issues-control-data-iso-writer-actual/v1';iso=$iso;sourcePlan=$frozen.sourcePlan;inputsSha256=$InputsSha256;
  sourceClosedFiles=$plan.files.Count;sourceClosedDirectories=$plan.directories.Count;sourceHeldBeforeAfterRawHashes=$true;
  boundedHelper=$frozen.helper;originalHelper=$frozen.originalHelper;extendedPathsOnly=$true;sourcePlanFileSystems=4;fileSystems=4;udfRevision=0x201;strictCompliance=$true;actualImageStreamAndFileEof=$true;
  fullGuestContentHashVerificationStillRequired=$true;externalWholeJobTimeoutMs=120000;externalDrainMs=2000;
  dataOnly=$true;productExecuted=$false;vmTouched=$false;releaseReady=$false}
}catch{$primary=$_}finally{
 try{foreach($releaseError in @([SouiDataIso4ddce37c.IsoIO]::ReleaseOwners($image,$result,$rootItem,$fsi))){if($releaseError){$cleanup.Add([string]$releaseError)}}}catch{$cleanup.Add($_.Exception.GetType().FullName)}
 $image=$null;$result=$null;$rootItem=$null;$fsi=$null
 for($i=$leases.Count-1;$i-ge0;$i--){try{$leases[$i].Dispose()}catch{$cleanup.Add($_.Exception.GetType().FullName)}}
}
if($primary-or$cleanup.Count){NewJson (Join-Path $attempt 'IsoWriter.failure.json') ([ordered]@{error=if($primary){$primary.Exception.Message}else{'CleanupFailure'};cleanupErrors=$cleanup.ToArray();partialPreserved=$true;retried=$false;dataOnly=$true;productExecuted=$false;vmTouched=$false});throw 'Data-only ISO creation failed; preserve original attempt'}
$success.cleanupErrors=$cleanup.ToArray()
NewJson (Join-Path $attempt 'IsoWriter.result.json') $success
$success|ConvertTo-Json -Depth 8 -Compress
