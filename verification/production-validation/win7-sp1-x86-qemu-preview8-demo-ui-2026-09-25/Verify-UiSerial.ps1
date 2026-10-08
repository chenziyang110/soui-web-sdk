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
if($input.schema -ne 'soui.win7-x86-preview8-demo-ui-input/v1' -or
   $input.version -ne '0.3.0-preview.8' -or $input.architecture -ne 'x86' -or
   $input.configuration -ne 'Release' -or $input.payloadFiles -ne 475 -or
   $input.initialPage -ne 'data' -or $input.react -ne $true -or
   $input.sourceArchiveSha256 -ne 'ef4c036e190d111273b8183bccbc931141a5f7fe3e8aafdf5b1989cc32903dc6' -or
   $input.payloadZipSha256 -ne 'ec69c61f1955e1b1d00dc1710811aa9baad4b12a7d5d9ae6508d49f580ff8d25' -or
   $input.releaseReady -ne $false){throw 'Unexpected Demo UI input identity'}
if((HashFile (Join-Path $root 'payload-sha256.tsv')) -ne $input.payloadManifestSha256){
  throw 'Payload manifest changed'
}
foreach($name in @('run-demo-ui.cmd','verify-payload.ps1','export-serial.ps1')){
  if((HashFile (Join-Path $root $name)) -ne $input.scripts.$name){throw "Guest script changed: $name"}
}
$lines=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath),[Text.Encoding]::ASCII)
if($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or
   $lines[-1] -ne 'SOUI_SERIAL_END' -or (($lines.Length-2)%2) -ne 0){
  throw 'Serial export framing is incomplete'
}
# Decode into memory so rejected input can never overwrite the archived results.
$decoded=@{}
function ResultText([string]$Name){[Text.Encoding]::ASCII.GetString($decoded[$Name])}
for($i=1;$i -lt $lines.Length-1;$i+=2){
  $fields=$lines[$i].Split('|')
  if($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or
     $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$'){throw "Invalid serial frame at line $($i+1)"}
  $name=$fields[1]
  if($decoded.ContainsKey($name)){throw "Duplicate serial file: $name"}
  $bytes=[Convert]::FromBase64String($lines[$i+1])
  if($bytes.Length -ne [int]$fields[2]){throw "Length mismatch: $name"}
  $hasher=[Security.Cryptography.SHA256]::Create()
  try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
  finally {$hasher.Clear()}
  if($hash -cne $fields[3]){throw "SHA-256 mismatch: $name"}
  $decoded[$name]=$bytes
}
$required=@('7za-sha256.txt','os-version.txt','zip-sha256.txt','zip-test.log',
  'zip-test-exitcode.txt','extract.log','extract-exitcode.txt',
  'raw-powershell-exitcode.txt','payload-verify.log','payload-verify-exitcode.txt',
  'app-exe-sha256.txt','engine-sha256.txt','soui-sha256.txt',
  'app-exitcode.txt','app-host.log')
if($decoded.Count -ne $required.Count){throw "Expected $($required.Count) serial files; got $($decoded.Count)"}
foreach($name in $required){if(-not $decoded.ContainsKey($name)){throw "Missing serial file: $name"}}
$osText=(ResultText 'os-version.txt').Replace([string][char]0,'')
foreach($expected in @('Version=6.1.7601','ServicePackMajorVersion=1','OSArchitecture=32-bit')){
  if(-not $osText.Contains($expected)){throw "Guest identity missing: $expected"}
}
foreach($name in @('zip-test-exitcode.txt','extract-exitcode.txt',
    'raw-powershell-exitcode.txt','payload-verify-exitcode.txt','app-exitcode.txt')){
  if((ResultText $name).Trim() -ne '0'){
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
  $hashes=@((ResultText $name) -split '\r?\n'|
    Where-Object {$_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$'}|
    ForEach-Object {($_ -replace '\s','').ToUpperInvariant()})
  if($hashes.Count -ne 1 -or $hashes[0] -cne $hashFiles[$name].ToUpperInvariant()){
    throw "Unexpected guest SHA-256 in $name"
  }
}
foreach($name in @('zip-test.log','extract.log')){
  if(-not (ResultText $name).Contains('Everything is Ok')){
    throw "Archive operation incomplete: $name"
  }
}
$payloadLog=ResultText 'payload-verify.log'
foreach($marker in @('Payload files=475,hashes=matched','Legal files=5,hashes=matched')){
  if(-not $payloadLog.Contains($marker)){throw "Guest payload marker missing: $marker"}
}
$appSteps=@((ResultText 'app-host.log') -split '\r?\n' | Where-Object {$_})
$previous=-1
foreach($step in @('window-constructed','host-started','adapter-unmount-start',
    'adapter-bridge-shutdown','adapter-events-drained','adapter-webview-destroyed')){
  $index=[Array]::IndexOf($appSteps,$step)
  if($index -le $previous -or [Array]::LastIndexOf($appSteps,$step) -ne $index){
    throw "Workbench lifecycle step missing, repeated or out of order: $step"
  }
  $previous=$index
}
Write-Output "Verified preview.8 Workbench host evidence on Win7 SP1 x86: $($decoded.Count) serial files, full payload, ordered shutdown, exit 0; GUI states require screenshot review"
