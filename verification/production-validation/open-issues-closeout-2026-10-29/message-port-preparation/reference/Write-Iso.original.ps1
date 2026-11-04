#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Plan,[Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$PlanSha256)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Need($v,$m){if(-not $v){throw $m}}
function NativePath([string]$p){return [IO.Path]::GetFullPath($p)}
function Small([string]$file,[long]$bytes,[string]$sha){Need ($bytes-ge 0 -and $bytes-le 16777216) 'Bounded metadata required';$q=Get-Item -LiteralPath $file -Force;Need (-not $q.PSIsContainer -and (($q.Attributes-band [IO.FileAttributes]::ReparsePoint)-eq 0)) 'Plain metadata required';$f=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);try{Need ($f.Length-eq $bytes) 'Metadata length changed';$b=[byte[]]::new([int]$bytes);$n=0;while($n-lt $b.Length){$r=$f.Read($b,$n,$b.Length-$n);Need ($r-gt 0) 'Metadata short read';$n+=$r};Need ($f.ReadByte()-eq -1 -and [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b)).ToLowerInvariant()-ceq $sha) 'Metadata hash/EOF mismatch';return ,$b}finally{$f.Dispose()}}
function NewJson([string]$file,$v){$b=[Text.UTF8Encoding]::new($false).GetBytes(($v|ConvertTo-Json -Depth 48)+[char]10);$f=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$f.Write($b);$f.Flush($true)}finally{$f.Dispose()}}
$here=NativePath $PSScriptRoot
Need ((NativePath $Plan)-ceq (Join-Path $here 'plan.json') -and [Threading.Thread]::CurrentThread.GetApartmentState()-eq [Threading.ApartmentState]::STA -and [Environment]::Is64BitProcess -and $PSVersionTable.PSVersion.ToString()-ceq '7.6.5') 'Fixed x64 STA original PowerShell host required'
$planInfo=Get-Item -LiteralPath $Plan
$p=[Text.UTF8Encoding]::new($false,$true).GetString((Small $Plan $planInfo.Length $PlanSha256))|ConvertFrom-Json
Need ($p.schema-ceq 'soui.release06-tooldeps-data-plan/v1' -and $p.sourceRoot-ceq 'C:/s06tooldepsmedia/iso-tree' -and $p.output-ceq 'C:/s06tooldepsmedia/SouiRelease06ToolDeps.iso' -and $p.scratch-ceq 'C:/s06tooldepsmedia/imapi-scratch' -and $p.maxIsoBytes-eq 1400000000 -and $p.timeoutMs-eq 120000 -and $p.drainMs-eq 2000 -and @($p.files).Count-eq 11 -and $p.fileSystems-eq 3) 'Exact reviewed runtime-only plan required'
Need (-not (Test-Path -LiteralPath $p.output) -and -not (Test-Path -LiteralPath $p.scratch) -and -not (Test-Path -LiteralPath (Join-Path $here 'writer-result.json'))) 'Fresh helper2 outputs required'
Need ($p.helper.bytes-eq 11926 -and $p.helper.sha256-ceq 'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72') 'Original complete11926-byte helper required'
$helper=Small $p.helper.path $p.helper.bytes $p.helper.sha256
Add-Type -TypeDefinition ([Text.UTF8Encoding]::new($false,$true).GetString($helper)) -Language CSharp
$leases=[Collections.Generic.List[object]]::new();$members=[Collections.Generic.List[object]]::new();$cleanup=[Collections.Generic.List[string]]::new()
$fsi=$null;$rootItem=$null;$result=$null;$image=$null;$primary=$null;$success=$null
function Closed {
 $files=@(Get-ChildItem -LiteralPath $p.sourceRoot -File -Recurse -Force|ForEach-Object{$_.FullName.Substring((NativePath $p.sourceRoot).Length+1).Replace('\','/')})
 $dirs=@(Get-ChildItem -LiteralPath $p.sourceRoot -Directory -Recurse -Force|ForEach-Object{Need (($_.Attributes-band [IO.FileAttributes]::ReparsePoint)-eq 0) 'Directory reparse refused';$_.FullName.Substring((NativePath $p.sourceRoot).Length+1).Replace('\','/')})
 Need ($files.Count-eq 11 -and @(Compare-Object @($p.files|ForEach-Object{$_.path}|Sort-Object -CaseSensitive) @($files|Sort-Object -CaseSensitive) -CaseSensitive).Count-eq 0 -and @(Compare-Object @($p.directories|Sort-Object -CaseSensitive) @($dirs|Sort-Object -CaseSensitive) -CaseSensitive).Count-eq 0) 'Actual11 source file/directory closure differs'
}
try{
 $leases.Add([SouiDataIso4ddce37c.ReadLease]::new((NativePath $p.sourceRoot),$true))
 foreach($d in $p.directories){$leases.Add([SouiDataIso4ddce37c.ReadLease]::new((NativePath (Join-Path $p.sourceRoot $d)),$true))}
 foreach($r in @($p.helper,$p.writer,$p.sourceStage,$p.sourceFiles)){$lease=[SouiDataIso4ddce37c.ReadLease]::new((NativePath $r.path),$false);$leases.Add($lease);$null=$lease.Hash($r.bytes,$r.sha256)}
 $planLease=[SouiDataIso4ddce37c.ReadLease]::new((NativePath $Plan),$false);$leases.Add($planLease);$null=$planLease.Hash($planInfo.Length,$PlanSha256)
 Closed
 foreach($r in $p.files){Need ($r.bytes-ge 0 -and $r.bytes-le 353102192) 'Original member byte limit required';$lease=[SouiDataIso4ddce37c.ReadLease]::new((NativePath (Join-Path $p.sourceRoot $r.path)),$false);$leases.Add($lease);$members.Add([pscustomobject]@{row=$r;lease=$lease;before=$lease.Hash($r.bytes,$r.sha256)})}
 NewJson (Join-Path $here 'PRE.json') ([ordered]@{files=@($members|ForEach-Object{$_.before});closedFiles=11;closedDirectories=@($p.directories).Count;planSha256=$PlanSha256})
 $null=Small 'C:/Windows/System32/imapi2fs.dll' 839680 '6b00e77979673c6d8bc4ff24a11dc49adc0a839f7a4d80986ef3711f587dbeaf'
 $clsid=(Get-Item -LiteralPath 'Registry::HKEY_CLASSES_ROOT\IMAPI2FS.MsftFileSystemImage\CLSID').GetValue('')
 Need ($clsid-ceq '{2C941FC5-975B-59BE-A960-9A2A262853A5}' -and (Get-Item -LiteralPath ('Registry::HKEY_CLASSES_ROOT\CLSID\'+$clsid+'\InprocServer32')).GetValue('')-ieq 'C:\Windows\System32\imapi2fs.dll') 'Original trusted IMAPI CLSID/server required'
 [SouiDataIso4ddce37c.IsoIO]::NewDirectory((NativePath $p.scratch))
 $fsi=New-Object -ComObject IMAPI2FS.MsftFileSystemImage
 $fsi.ChooseImageDefaultsForMediaType(6);$fsi.FileSystemsToCreate=3;$fsi.StrictFileSystemCompliance=$true;$fsi.VolumeName='SOUI_R06DEPS4';$fsi.WorkingDirectory=NativePath $p.scratch;$fsi.SessionStartBlock=0
 $rootItem=$fsi.Root;$rootItem.AddTree((NativePath $p.sourceRoot),$false);$result=$fsi.CreateResultImage();$image=$result.ImageStream
 $iso=[SouiDataIso4ddce37c.IsoIO]::CopyImage($image,(NativePath $p.output),1400000000)
 Closed
 $post=@($members|ForEach-Object{[ordered]@{before=$_.before;after=$_.lease.Hash($_.row.bytes,$_.row.sha256)}})
 $null=Small 'C:/Windows/System32/imapi2fs.dll' 839680 '6b00e77979673c6d8bc4ff24a11dc49adc0a839f7a4d80986ef3711f587dbeaf'
 NewJson (Join-Path $here 'POST.json') ([ordered]@{files=$post;allExact=$true;closedFiles=11;closedDirectories=@($p.directories).Count})
 $success=[ordered]@{schema='soui.startup-data-zip-iso-writer/v1';iso=$iso;sourceClosedFiles=11;sourceClosedDirectories=@($p.directories).Count;sourceAndCopiesRawBeforeAfter=$true;actualStreamAndFileEofFromOriginalCopyImage=$true;originalHelper=[ordered]@{bytes=11926;sha256=$p.helper.sha256};fileSystems=3;externalWholeJobBudgetMs=120000;externalDrainMs=2000;dataOnly=$true;productExecuted=$false;runtimeExecuted=$false;vmTouched=$false;releaseReady=$false}
}catch{$primary=$_}finally{
 # Original helper deduplicates the four live scalar RCWs before releasing.
 try{foreach($err in @([SouiDataIso4ddce37c.IsoIO]::ReleaseOwners($image,$result,$rootItem,$fsi))){if($err){$cleanup.Add([string]$err)}}}catch{$cleanup.Add($_.Exception.GetType().FullName)}
 $image=$null;$result=$null;$rootItem=$null;$fsi=$null
 for($i=$leases.Count-1;$i-ge 0;$i--){try{$leases[$i].Dispose()}catch{$cleanup.Add($_.Exception.GetType().FullName)}}
}
if($primary -or $cleanup.Count){NewJson (Join-Path $here 'writer-failure.json') ([ordered]@{schema='soui.startup-data-zip-iso-failure/v1';error=if($primary){$primary.Exception.Message}else{'CleanupFailure'};cleanupErrors=$cleanup.ToArray();partialPreserved=$true;retried=$false;runtimeExecuted=$false;productExecuted=$false;vmTouched=$false});throw 'Modern data-only ISO writer failed; preserve original attempt'}
$success.cleanupErrors=$cleanup.ToArray()
NewJson (Join-Path $here 'writer-result.json') $success
$success|ConvertTo-Json -Depth 8 -Compress
