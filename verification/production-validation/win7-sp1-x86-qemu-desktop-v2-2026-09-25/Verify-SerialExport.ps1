[CmdletBinding()]
param([string]$InputPath='D:\SOUI-SDK-QEMU-validation\desktop-v2-serial-20260925.txt')
$ErrorActionPreference='Stop'
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
$required=@('os-version.txt','test-exe-sha256.txt','desktop-stdout.log',
  'desktop-stderr.log','desktop-exitcode.txt')
if($seen.Count -ne $required.Count){throw "Expected $($required.Count) guest files; got $($seen.Count)"}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing guest file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
$hashes=@(Get-Content -LiteralPath (Join-Path $results 'test-exe-sha256.txt') |
  Where-Object {$_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$'} |
  ForEach-Object {($_ -replace '\s','').ToUpperInvariant()})
if($hashes.Count -ne 1 -or $hashes[0] -cne $inputRecord.testExeSha256){
  throw 'Guest executable identity differs from the frozen input'
}
if((Get-Content -LiteralPath (Join-Path $results 'desktop-exitcode.txt') -Raw).Trim() -ne '0'){
  throw 'Guest desktop service test failed'
}
$stdout=Get-Content -LiteralPath (Join-Path $results 'desktop-stdout.log') -Raw
if(-not $stdout.Contains('desktop unicode conversion passed')){throw 'Guest success marker is missing'}
$record=[ordered]@{schema='soui.qemu-desktop-guest/v2';sourceCommit=$inputRecord.sourceCommit;
  sourceManifestSha256=$inputRecord.sourceManifestSha256;testExeSha256=$inputRecord.testExeSha256;
  guest='Windows 7 SP1 x86 QEMU';serialFiles=$seen.Count;desktopRoundTripPassed=$true;
  releaseReady=$false}
[IO.File]::WriteAllText((Join-Path $results 'verification.json'),($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output ($record|ConvertTo-Json -Depth 5)
