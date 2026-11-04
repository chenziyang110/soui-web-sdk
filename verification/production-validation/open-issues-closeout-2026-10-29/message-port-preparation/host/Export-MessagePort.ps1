[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$folder=[IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\','/')
if($folder-notmatch'^C:\\issue-closeout-evidence\\mp77-[a-z0-9-]+$'){throw 'Exact task evidence root required'}
if(-not(Test-Path -LiteralPath (Join-Path $folder 'PowerShell.finished.json'))){throw 'Producer completion record required before export'}
$catalogPath=Join-Path $folder 'ExportFiles.actual.json'
if(Test-Path -LiteralPath $catalogPath){throw 'Preserve existing export; no implicit retry'}
$files=@(Get-ChildItem -LiteralPath $folder -File -Recurse -Force|Sort-Object FullName -CaseSensitive)
if($files.Count-gt512){throw 'More than512 files requires an explicit export plan; originals retained'}
$rows=@();$total=[long]0
foreach($file in $files){
 if($file.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked evidence refused'}
 $relative=$file.FullName.Substring($folder.Length+1).Replace('\','/')
 $row=[ordered]@{path=$relative;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
 $rows+=$row;$total+=$file.Length
}
$catalog=[ordered]@{schema='soui.message-port-export-catalog/v1';files=$rows;totalBytes=$total;producerClosedRecord='PowerShell.finished.json';rawBytesPreserved=$true;exporterExitObserved=$false}
$catalogRaw=[Text.UTF8Encoding]::new($false).GetBytes(($catalog|ConvertTo-Json -Depth 8)+[char]10)
$create=[IO.File]::Open($catalogPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{$create.Write($catalogRaw,0,$catalogRaw.Length)}finally{$create.Dispose()}
$catalogSha=(Get-FileHash -LiteralPath $catalogPath -Algorithm SHA256).Hash.ToLowerInvariant()
$exportId=[Guid]::NewGuid().ToString('N').ToUpperInvariant()
$chunkLimit=1048576 # Explicit complete chunks, each below the original8MiB raw ceiling.
$exportRows=@([pscustomobject]@{path='ExportFiles.actual.json';bytes=$catalogRaw.Length;sha256=$catalogSha})+@($rows|ForEach-Object{[pscustomobject]$_})
$serial=[IO.Ports.SerialPort]::new('COM1',115200);$serial.WriteTimeout=15000
$ordinal=0;$chunks=@();$hash=[Security.Cryptography.SHA256]::Create()
function Frame($value){
 $json=[Text.UTF8Encoding]::new($false).GetBytes(($value|ConvertTo-Json -Depth 8 -Compress))
 if($json.Length-gt8388608){throw 'Bounded serial frame exceeded; no truncation'}
 $tag='MP77'+$exportId+'C'+$script:ordinal.ToString('D6')
 $serial.WriteLine($tag+'BEGIN');$serial.WriteLine([Convert]::ToBase64String($json));$serial.WriteLine($tag+'END')
 $script:ordinal++
}
try{
 $serial.Open()
 for($fileId=0;$fileId-lt$exportRows.Count;$fileId++){
  $row=$exportRows[$fileId];$file=Join-Path $folder $row.path
  if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()-cne$row.sha256){throw 'Closed source changed before export'}
  $sourceStream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try{
   $offset=[long]0
   do{
    $wanted=[int][Math]::Min([long]$chunkLimit,[long]$row.bytes-$offset);$buffer=[byte[]]::new($wanted);$count=0
    while($count-lt$wanted){$n=$sourceStream.Read($buffer,$count,$wanted-$count);if($n-le0){throw 'Unexpected EOF'};$count+=$n}
    $chunkSha=([BitConverter]::ToString($hash.ComputeHash($buffer))).Replace('-','').ToLowerInvariant()
    Frame ([ordered]@{schema='soui.message-port-serial-chunk/v1';exportId=$exportId;fileId=$fileId;path=$row.path;fileBytes=[long]$row.bytes;fileSha256=$row.sha256;offset=$offset;chunkBytes=$wanted;chunkSha256=$chunkSha;dataBase64=[Convert]::ToBase64String($buffer)})
    $chunks+=@([ordered]@{fileId=$fileId;offset=$offset;bytes=$wanted;sha256=$chunkSha});$offset+=$wanted
   }while($offset-lt$row.bytes)
   if($sourceStream.ReadByte()-ne-1){throw 'Source grew during export'}
  }finally{$sourceStream.Dispose()}
  if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()-cne$row.sha256){throw 'Closed source changed after export'}
 }
 Frame ([ordered]@{schema='soui.message-port-serial-completed/v1';exportId=$exportId;files=$exportRows.Count;chunks=$chunks.Count;catalogSha256=$catalogSha;totalOriginalBytes=$total+$catalogRaw.Length;rawChunkLimit=$chunkLimit;sourceBytesBeforeAfterEqual=$true;testPassed=$null})
}finally{if($serial.IsOpen){$serial.Close()};$serial.Dispose();$hash.Dispose()}
$receipt=[ordered]@{exportId=$exportId;catalogSha256=$catalogSha;files=$exportRows.Count;chunks=$chunks;totalOriginalBytes=$total+$catalogRaw.Length;maxRawChunkBytes=$chunkLimit;originalRawByteLimit=8388608;testPassed=$null;exporterExitObserved=$false}
[IO.File]::WriteAllText((Join-Path $folder 'SerialExport.actual.json'),($receipt|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ($exportId+' complete files='+$exportRows.Count+' rawBytes='+($total+$catalogRaw.Length))
