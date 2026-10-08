[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference='Stop'
if(-not $InputPath){$InputPath=Join-Path $PSScriptRoot 'serial-export.txt'}
$inputFile=[IO.Path]::GetFullPath($InputPath)
$lines=[IO.File]::ReadAllLines($inputFile,[Text.Encoding]::ASCII)
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
if($seen.Count -ne 26){throw "Expected 26 exported files; got $($seen.Count)"}
$required=@('7za-sha256.txt','acceptance-exe-sha256.txt','engine-sha256.txt','sdk-sha256.txt',
  'runtime-zip-sha256.txt','runtime-zip-test.log','runtime-zip-test-exitcode.txt',
  'runtime-extract.log','runtime-extract-exitcode.txt','os-version.txt')
foreach($mode in @('bridge','reload','recreate','multiwindow')){
  $required+=@("$mode-events.ndjson","$mode-exitcode.txt","$mode-stderr.log","$mode-stdout.log")
}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($mode in @('bridge','reload','recreate','multiwindow')){
  $exitCode=(Get-Content -LiteralPath (Join-Path $results "$mode-exitcode.txt") -Raw).Trim()
  if($exitCode -ne '0'){throw "$mode exit code: $exitCode"}
  $events=@(Get-Content -LiteralPath (Join-Path $results "$mode-events.ndjson") | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
  if($events.Count -lt 2 -or $events[0].event -ne 'run.begin' -or $events[0].mode -ne $mode -or $events[0].cycles -ne 3 -or $events[-1].event -ne 'run.complete'){
    throw "$mode event stream is incomplete"
  }
  $cycles=@($events | Where-Object { $_.event -eq 'sample' -and $_.phase -eq 'cycle' })
  if($cycles.Count -ne 3){throw "$mode has $($cycles.Count) completed cycle samples, expected 3"}
  if(@($events | Where-Object { $_.event -match 'fail|error' }).Count -gt 0){throw "$mode reports a failure event"}
  Write-Output "$mode`: exit=0, cycles=3, elapsedMs=$($events[-1].elapsedMs), events=$($events.Count)"
}
foreach($name in @('runtime-zip-test-exitcode.txt','runtime-extract-exitcode.txt')){
  if((Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim() -ne '0'){throw "Archive operation failed: $name"}
}
foreach($name in @('runtime-zip-test.log','runtime-extract.log')){
  if(-not (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Contains('Everything is Ok')){
    throw "Archive operation incomplete: $name"
  }
}
$expectedHashes=@{
  '7za-sha256.txt'='EA308C76A2F927B160A143D94072B0DCE232E04B751F0C6432A94E05164E716D'
  'runtime-zip-sha256.txt'='4209B68A3E12209990803D4968BC291C45F0D9D07A8A12DDD8F41F71CFF116C4'
  'acceptance-exe-sha256.txt'='3C24F024F2CA96589B648F51FBD11B65AC191C5451B88A4EB8CB7BAC783A69AC'
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
Write-Output "Verified $($seen.Count) serial files by length and SHA-256"
