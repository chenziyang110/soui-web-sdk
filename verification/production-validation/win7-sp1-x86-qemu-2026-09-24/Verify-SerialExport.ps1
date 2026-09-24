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
if($seen.Count -ne 17){throw "Expected 17 exported files; got $($seen.Count)"}
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
Write-Output "Verified $($seen.Count) serial files by length and SHA-256"
