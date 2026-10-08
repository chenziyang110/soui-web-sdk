[CmdletBinding()]
param([string]$InputPath='D:\SOUI-SDK-QEMU-validation\public-task-serial-20260925.txt')
$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$inputRecord=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'input.json') -Raw | ConvertFrom-Json
$lines=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath),[Text.Encoding]::ASCII)
if($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or $lines[-1] -ne 'SOUI_SERIAL_END' -or
    (($lines.Length-2) % 2) -ne 0){throw 'Serial export framing is incomplete'}
$results=Join-Path $PSScriptRoot 'results'
if(Test-Path -LiteralPath $results){throw "Guest result directory already exists: $results"}
New-Item -ItemType Directory -Path $results | Out-Null
$seen=@{}
for($index=1;$index -lt $lines.Length-1;$index+=2){
  $fields=$lines[$index].Split('|')
  if($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$'){
    throw "Invalid serial file header at line $($index+1)"
  }
  $name=$fields[1]
  if($seen.ContainsKey($name)){throw "Duplicate serial file: $name"}
  $seen[$name]=$true
  $bytes=[Convert]::FromBase64String($lines[$index+1])
  if($bytes.Length -ne [int]$fields[2]){throw "Length mismatch: $name"}
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
  finally {$hasher.Dispose()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  [IO.File]::WriteAllBytes((Join-Path $results $name),$bytes)
}
$required=@('os-version.txt','7za-sha256.txt','runtime-zip-sha256.txt',
  'runtime-zip-test.log','runtime-zip-test-exitcode.txt','runtime-extract.log','runtime-extract-exitcode.txt',
  'acceptance-exe-sha256.txt','engine-sha256.txt','sdk-sha256.txt',
  'bridge-events.ndjson','bridge-exitcode.txt','bridge-stdout.log','bridge-stderr.log')
if($seen.Count -ne $required.Count){throw "Expected $($required.Count) guest files; got $($seen.Count)"}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing guest file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('runtime-zip-test-exitcode.txt','runtime-extract-exitcode.txt','bridge-exitcode.txt')){
  if((Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim() -ne '0'){throw "Guest operation failed: $name"}
}
foreach($name in @('runtime-zip-test.log','runtime-extract.log')){
  if(-not (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Contains('Everything is Ok')){
    throw "Guest archive operation incomplete: $name"
  }
}
$expectedHashes=@{
  '7za-sha256.txt'=$inputRecord.sevenZipSha256
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
$eventResult=& node (Join-Path $PSScriptRoot 'Verify-Events.cjs') $results
if($LASTEXITCODE -ne 0){throw 'Guest Bridge event validation failed'}
$record=[ordered]@{schema='soui.qemu-public-task-guest/v1';sourceSha256=$inputRecord.sourceSha256;
  zipSha256=$inputRecord.zipSha256;guest='Windows 7 SP1 x86 QEMU';events=($eventResult|ConvertFrom-Json);
  serialFiles=$seen.Count;releaseReady=$false}
[IO.File]::WriteAllText((Join-Path $results 'verification.json'),($record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output ($record|ConvertTo-Json -Depth 6)
