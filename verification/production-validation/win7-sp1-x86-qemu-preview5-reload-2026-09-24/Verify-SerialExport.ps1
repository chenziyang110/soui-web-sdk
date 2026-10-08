[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference='Stop'
if(-not $InputPath){$InputPath=Join-Path $PSScriptRoot 'serial-export.txt'}
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
  try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')} finally {$hasher.Dispose()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  [IO.File]::WriteAllBytes((Join-Path $results $name),$bytes)
}
if($seen.Count -ne 15){throw "Expected 15 exported files; got $($seen.Count)"}
$required=@('os-version.txt','runtime-zip-sha256.txt','runtime-zip-test.log','runtime-zip-test-exitcode.txt',
  'runtime-extract.log','runtime-extract-exitcode.txt','acceptance-exe-sha256.txt','engine-sha256.txt',
  'sdk-sha256.txt','reload-start.txt','reload-finish.txt','reload-events.ndjson','reload-exitcode.txt',
  'reload-stdout.log','reload-stderr.log')
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('runtime-zip-test-exitcode.txt','runtime-extract-exitcode.txt','reload-exitcode.txt')){
  if((Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim() -ne '0'){throw "Operation failed: $name"}
}
foreach($name in @('runtime-zip-test.log','runtime-extract.log')){
  if(-not (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Contains('Everything is Ok')){
    throw "Archive operation incomplete: $name"
  }
}
$expectedHashes=@{
  'runtime-zip-sha256.txt'='66BAC212641EC7D9EA4066F1CCA8C724A5CD241DF0A86CBE1ED4619A3E37D8FA'
  'acceptance-exe-sha256.txt'='38127DB6630750B9FB1F71C66995F993879482E0CD60DA0833486D5F0C2AD0F8'
  'engine-sha256.txt'='3B9071413F704958FB5708B8BB065785A6C3ED03B7FD0FB04F5952201E5FE4F7'
  'sdk-sha256.txt'='1EA0FF009B1EA6B513EE94A252BE059A9A755DF75A67043B25FF2F5394F040DE'
}
foreach($name in $expectedHashes.Keys){
  $hashes=@(Get-Content -LiteralPath (Join-Path $results $name) |
    Where-Object {$_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$'} |
    ForEach-Object {($_ -replace '\s','').ToUpperInvariant()})
  if($hashes.Count -ne 1 -or $hashes[0] -cne $expectedHashes[$name]){throw "Unexpected guest SHA-256 in $name"}
}
& node (Join-Path $PSScriptRoot 'Verify-Events.cjs') $results
if($LASTEXITCODE -ne 0){throw 'Native lifecycle event validation failed'}
Write-Output "Verified $($seen.Count) guest files by length, SHA-256, system identity, binary identity and lifecycle"
