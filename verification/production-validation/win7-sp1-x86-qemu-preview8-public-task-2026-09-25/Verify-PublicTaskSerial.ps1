[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
if(-not $InputPath){$InputPath=Join-Path $root 'serial-export.txt'}
function HashFile([string]$Path){
  $stream=[IO.File]::OpenRead([IO.Path]::GetFullPath($Path))
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','')}
  finally {$hasher.Clear();$stream.Close()}
}
$input=Get-Content -LiteralPath (Join-Path $root 'input.json') -Raw|ConvertFrom-Json
if($input.schema -ne 'soui.win7-x86-preview8-public-task-input/v1' -or
   $input.version -ne '0.3.0-preview.8' -or $input.architecture -ne 'x86' -or
   $input.configuration -ne 'Release' -or $input.kernelKind -ne 'prebuilt' -or
   $input.payloadFiles -ne 76 -or $input.releaseReady -ne $false -or
   $input.sourceArchiveSha256 -ne '3291fb056a009fd9be48ff6e5212db96d94133a300a6b23eb495f065917f9788'){
  throw 'Unexpected acceptance input identity'
}
$build=Get-Content -LiteralPath (Join-Path $root 'build.json') -Raw|ConvertFrom-Json
if((HashFile (Join-Path $root 'build.json')) -ne $input.buildJsonSha256 -or
   $build.schema -ne 'soui.acceptance-build/v3' -or
   $build.buildId -ne $input.buildId -or $build.consumerBuildId -ne $input.consumerBuildId -or
   $build.sourceSha256 -ne $input.sourceSha256 -or
   $build.payload.sha256 -ne $input.payloadSha256 -or
   $build.architecture -ne 'x86' -or $build.configuration -ne 'Release'){
  throw 'Acceptance build identity mismatch'
}
if((HashFile (Join-Path $root 'payload-sha256.tsv')) -ne $input.payloadManifestSha256){
  throw 'Payload manifest changed'
}
foreach($name in @('run-public-task.cmd','verify-payload.ps1','export-serial.ps1')){
  if((HashFile (Join-Path $root $name)) -ne $input.scripts.$name){throw "Guest script changed: $name"}
}
$lines=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath),[Text.Encoding]::ASCII)
if($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or
   $lines[-1] -ne 'SOUI_SERIAL_END' -or (($lines.Length-2)%2) -ne 0){
  throw 'Serial export framing is incomplete'
}
$results=Join-Path $root 'results'
New-Item -ItemType Directory -Force -Path $results|Out-Null
$seen=@{}
for($i=1;$i -lt $lines.Length-1;$i+=2){
  $fields=$lines[$i].Split('|')
  if($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or
     $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$'){throw "Invalid serial frame at line $($i+1)"}
  $name=$fields[1]
  if($seen.ContainsKey($name)){throw "Duplicate serial file: $name"}
  $seen[$name]=$true
  $bytes=[Convert]::FromBase64String($lines[$i+1])
  if($bytes.Length -ne [int]$fields[2]){throw "Length mismatch: $name"}
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
  finally {$hasher.Clear()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  [IO.File]::WriteAllBytes((Join-Path $results $name),$bytes)
}
$required=@('7za-sha256.txt','os-version.txt','zip-sha256.txt','zip-test.log',
  'zip-test-exitcode.txt','extract.log','extract-exitcode.txt',
  'raw-powershell-exitcode.txt','payload-verify.log','payload-verify-exitcode.txt',
  'app-exe-sha256.txt','engine-sha256.txt','soui-sha256.txt',
  'bridge-events.ndjson','bridge-stdout.log','bridge-stderr.log','bridge-exitcode.txt')
if($seen.Count -ne $required.Count){throw "Expected $($required.Count) serial files; got $($seen.Count)"}
foreach($name in $required){if(-not $seen.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('zip-test-exitcode.txt','extract-exitcode.txt',
    'raw-powershell-exitcode.txt','payload-verify-exitcode.txt','bridge-exitcode.txt')){
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
if(-not (Get-Content -LiteralPath (Join-Path $results 'payload-verify.log') -Raw).Contains('Payload files=76,hashes=matched')){
  throw 'Full payload verification missing'
}
& node (Join-Path $root 'Verify-Events.cjs') $results
if($LASTEXITCODE -ne 0){throw 'Lifecycle validation failed'}
Write-Output "Verified preview.8 public task on Win7 SP1 x86: $($seen.Count) serial files, full payload, bridge cycles=3"
