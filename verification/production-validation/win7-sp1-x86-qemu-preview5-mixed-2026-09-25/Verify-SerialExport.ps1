[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference='Stop'
if(-not $InputPath){$InputPath=Join-Path $PSScriptRoot 'serial-export.txt'}
$inputRecord=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'input.json') -Raw | ConvertFrom-Json
$lines=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath),[Text.Encoding]::ASCII)
if($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or $lines[-1] -ne 'SOUI_SERIAL_END' -or (($lines.Length-2) % 2) -ne 0){
  throw 'Serial export framing is incomplete'
}
$results=Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$seen=@{}
for($i=1;$i -lt $lines.Length-1;$i+=2){
  $fields=$lines[$i].Split('|')
  if($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$'){
    throw "Invalid serial file header at line $($i+1)"
  }
  $name=$fields[1]
  if($seen.ContainsKey($name)){throw "Duplicate serial file: $name"}
  $seen[$name]=$true
  $bytes=[Convert]::FromBase64String($lines[$i+1])
  if($bytes.Length -ne [int]$fields[2]){throw "Length mismatch: $name"}
  $hasher=[Security.Cryptography.SHA256]::Create()
  try{$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}finally{$hasher.Dispose()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  [IO.File]::WriteAllBytes((Join-Path $results $name),$bytes)
}
$required=@('os-version.txt','runtime-zip-sha256.txt','runtime-zip-test.log','runtime-zip-test-exitcode.txt',
  'runtime-extract.log','runtime-extract-exitcode.txt','acceptance-exe-sha256.txt','engine-sha256.txt',
  'sdk-sha256.txt','mixed-start.txt','mixed-finish.txt','mixed-events.ndjson','mixed-exitcode.txt',
  'mixed-stdout.log','mixed-stderr.log')
if($seen.Count -ne $required.Count){throw "Expected $($required.Count) exported files; got $($seen.Count)"}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('runtime-zip-test-exitcode.txt','runtime-extract-exitcode.txt','mixed-exitcode.txt')){
  if((Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim() -ne '0'){throw "Operation failed: $name"}
}
foreach($name in @('runtime-zip-test.log','runtime-extract.log')){
  if(-not (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Contains('Everything is Ok')){
    throw "Archive operation incomplete: $name"
  }
}
$expectedHashes=@{
  'runtime-zip-sha256.txt'=$inputRecord.zipSha256
  'acceptance-exe-sha256.txt'=$inputRecord.exeSha256
  'engine-sha256.txt'=$inputRecord.engineSha256
  'sdk-sha256.txt'=$inputRecord.sdkSha256
}
foreach($name in $expectedHashes.Keys){
  $hashes=@(Get-Content -LiteralPath (Join-Path $results $name) |
    Where-Object {$_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$'} |
    ForEach-Object {($_ -replace '\s','').ToUpperInvariant()})
  if($hashes.Count -ne 1 -or $hashes[0] -cne $expectedHashes[$name]){throw "Unexpected guest SHA-256 in $name"}
}
& node (Join-Path $PSScriptRoot 'Verify-Events.cjs') $results
if($LASTEXITCODE -ne 0){throw 'Mixed lifecycle event validation failed'}
Write-Output "Verified $($seen.Count) guest files by length, SHA-256, system identity, binary identity and mixed lifecycle"
