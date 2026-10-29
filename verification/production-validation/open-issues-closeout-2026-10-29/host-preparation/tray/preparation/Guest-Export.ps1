[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
# Prepared only. Serial export of fresh closed evidence; no product/input/VM control.
$folder=[IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\','/')
if(-not $folder.StartsWith('C:\issue-closeout-evidence\',[StringComparison]::OrdinalIgnoreCase)){
  throw 'Use this run''s fresh issue evidence directory.'
}
$catalogPath=Join-Path $folder 'ExportFiles.actual.json'
$catalogRaw=[IO.File]::ReadAllBytes($catalogPath)
$catalog=[Text.Encoding]::UTF8.GetString($catalogRaw)|ConvertFrom-Json
if($catalog.schema-ne'soui.issue-short-export-files/v1' -or $catalog.files.Count-gt512){throw 'Unexpected or excessive export catalog.'}
$payload=[ordered]@{'ExportFiles.actual.json'=[Convert]::ToBase64String($catalogRaw)}
$expected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$sha=[Security.Cryptography.SHA256]::Create()
$total=[long]$catalogRaw.Length
try{
  for($i=0;$i-lt$catalog.files.Count;$i++){
    $row=$catalog.files[$i];$file=[IO.Path]::GetFullPath([string]$row.path)
    if(-not $file.StartsWith($folder+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Export file is outside this run''s evidence root.'}
    if(-not $expected.Add($file)){throw 'Duplicate export path.'}
    if((Get-Item -LiteralPath $file).Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked export files are not allowed.'}
    $bytes=[IO.File]::ReadAllBytes($file)
    $digest=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()
    if($bytes.Length-ne$row.bytes -or $digest-cne$row.sha256){throw 'Actual report bytes changed after catalog creation.'}
    $total+=$bytes.Length
    if($bytes.Length-gt16777216 -or $total-gt8388608){throw 'Bounded export exceeded; retain originals and prepare explicit complete batches, never truncate.'}
    $payload[('file{0:d4}'-f$i)]=[Convert]::ToBase64String($bytes)
  }
}finally{$sha.Dispose()}
$envelope=[Text.Encoding]::UTF8.GetBytes(($payload|ConvertTo-Json -Compress))
$tag='ISSUESHORT'+[Guid]::NewGuid().ToString('N').ToUpperInvariant()
$exportInfo=[ordered]@{tag=$tag;catalogSha256=(Get-FileHash -LiteralPath $catalogPath -Algorithm SHA256).Hash.ToLowerInvariant();
  rawBytes=$total;files=$catalog.files.Count;algorithm='original base64 JSON file envelope; fresh exact catalog';
  sourceClosedRequired=$true;testPassed=$null}|ConvertTo-Json -Compress
$stream=[IO.File]::Open((Join-Path $folder 'SerialExport.actual.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$raw=[Text.Encoding]::UTF8.GetBytes($exportInfo);$stream.Write($raw,0,$raw.Length)}finally{$stream.Dispose()}
$serial=[IO.Ports.SerialPort]::new('COM1',115200)
$serial.WriteTimeout=15000
try{
  $serial.Open();$serial.WriteLine($tag+'BEGIN')
  $serial.WriteLine([Convert]::ToBase64String($envelope));$serial.WriteLine($tag+'END')
}finally{if($serial.IsOpen){$serial.Close()};$serial.Dispose()}
Write-Output ($tag+' files='+$catalog.files.Count+' rawBytes='+$total)
