[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference='Stop'
if(-not $InputPath){$InputPath=Join-Path $PSScriptRoot 'serial-export.txt'}
function Get-Sha256File([string]$Path){
  $stream=[IO.File]::OpenRead([IO.Path]::GetFullPath($Path))
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','')}
  finally {$hasher.Clear();$stream.Close()}
}
$input=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'input.json') -Raw|ConvertFrom-Json
if($input.schema -ne 'soui.win7-x86-preview8-independent-consumer-input/v1' -or
   $input.version -ne '0.3.0-preview.8' -or $input.architecture -ne 'x86' -or
   $input.configuration -ne 'Release' -or $input.payloadFiles -ne 71 -or
   $input.harnessRevision -ne 2 -or
   $input.sourceArchiveSha256 -ne '00c405582079762cf01778841ffea1a9b91392897b6b1e7178778330657241b5' -or
   $input.releaseReady -ne $false){throw 'Unexpected independent consumer identity'}
foreach($item in @(
  @('payload-sha256.tsv','payloadManifestSha256'),
  @('sdk-lock.json','sdkLockSha256'),
  @('sdk-manifest.json','sdkManifestSha256')
)){
  $actual=Get-Sha256File (Join-Path $PSScriptRoot $item[0])
  if($actual -ne $input.($item[1])){throw "Input file changed: $($item[0])"}
}
foreach($name in @('run-consumer.cmd','verify-consumer.ps1','export-serial.ps1')){
  if((Get-Sha256File (Join-Path $PSScriptRoot $name)) -ne $input.scripts.$name){
    throw "Guest script changed: $name"
  }
}
$build=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-Release.json') -Raw|ConvertFrom-Json
if($build.schema -ne 'soui.source-build/v2' -or $build.buildId -ne $input.buildId -or
   $build.source.sha256 -ne $input.sourceSha256 -or $build.architecture -ne 'x86' -or
   $build.configuration -ne 'Release' -or $build.releaseReady -ne $false){
  throw 'Independent project build identity mismatch'
}
$lines=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath),[Text.Encoding]::ASCII)
if($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or
   $lines[-1] -ne 'SOUI_SERIAL_END' -or (($lines.Length-2)%2) -ne 0){
  throw 'Serial export framing is incomplete'
}
$results=Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force -Path $results|Out-Null
$seen=@{}
for($i=1;$i -lt $lines.Length-1;$i+=2){
  $fields=$lines[$i].Split('|')
  if($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or
     $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$'){throw "Invalid serial file header at line $($i+1)"}
  $name=$fields[1]
  if($seen.ContainsKey($name)){throw "Duplicate serial file: $name"}
  $seen[$name]=$true
  $bytes=[Convert]::FromBase64String($lines[$i+1])
  if($bytes.Length -ne [int]$fields[2]){throw "Length mismatch: $name"}
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
  finally {$hasher.Dispose()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  [IO.File]::WriteAllBytes((Join-Path $results $name),$bytes)
}
$required=@(
  '7za-sha256.txt','app-exe-sha256.txt','app-exitcode.txt','app-smoke.log',
  'engine-sha256.txt','extract-exitcode.txt','extract.log','os-version.txt',
  'payload-verify-exitcode.txt','payload-verify.log','raw-powershell-exitcode.txt','soui-sha256.txt',
  'zip-sha256.txt','zip-test-exitcode.txt','zip-test.log'
)
if($seen.Count -ne $required.Count){throw "Expected $($required.Count) serial files; got $($seen.Count)"}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('zip-test-exitcode.txt','extract-exitcode.txt','raw-powershell-exitcode.txt','payload-verify-exitcode.txt','app-exitcode.txt')){
  if((Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim() -ne '0'){
    throw "Guest operation failed: $name"
  }
}
$hashFiles=@{
  '7za-sha256.txt'=$input.sevenZipSha256
  'zip-sha256.txt'=$input.payloadZipSha256
  'app-exe-sha256.txt'=$input.exeSha256
  'engine-sha256.txt'=$input.engineSha256
  'soui-sha256.txt'=$input.souiSha256
}
foreach($name in $hashFiles.Keys){
  $hashes=@(Get-Content -LiteralPath (Join-Path $results $name)|
    Where-Object {$_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$'}|
    ForEach-Object {($_ -replace '\s','').ToUpperInvariant()})
  if($hashes.Count -ne 1 -or $hashes[0] -cne $hashFiles[$name].ToUpperInvariant()){
    throw "Unexpected guest SHA-256 in $name"
  }
}
foreach($name in @('zip-test.log','extract.log')){
  if(-not (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Contains('Everything is Ok')){
    throw "Archive operation incomplete: $name"
  }
}
$payloadLog=Get-Content -LiteralPath (Join-Path $results 'payload-verify.log') -Raw
foreach($expected in @('Payload files=71,hashes=matched','Legal files=5,hashes=matched')){
  if(-not $payloadLog.Contains($expected)){throw "Payload verification missing: $expected"}
}
$events=@(Get-Content -LiteralPath (Join-Path $results 'app-smoke.log'))
if($events[0] -ne 'process-start' -or $events -notcontains 'host-started' -or
   $events -notcontains 'application-ready' -or $events[-1] -ne 'host-close-complete'){
  throw 'Independent consumer startup or close is incomplete'
}
Write-Output "Verified preview.8 independent consumer: $($seen.Count) serial files, Win7 SP1 x86 identity, full payload and normal app startup/close"
