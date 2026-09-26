[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Payload,
  [Parameter(Mandatory=$true)][string]$Zip,
  [Parameter(Mandatory=$true)][string]$Transfer
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
if(Test-Path -LiteralPath $Transfer){throw 'Transfer target already exists'}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
if((Hash $Zip) -ne 'ec69c61f1955e1b1d00dc1710811aa9baad4b12a7d5d9ae6508d49f580ff8d25'){
  throw 'Frozen preview.8 Workbench ZIP differs'
}
$files=@(Get-ChildItem -LiteralPath $Payload -Recurse -File|ForEach-Object {
  $relative=$_.FullName.Substring($Payload.Length+1).Replace('\','/')
  if($relative.StartsWith('/') -or $relative.Contains('..') -or $relative.Contains('|')){throw "Unsafe payload path: $relative"}
  [pscustomobject]@{path=$relative;bytes=$_.Length;sha256=(Hash $_.FullName).ToUpperInvariant()}
}|Sort-Object path)
if($files.Count -ne 475){throw "Expected 475 files, got $($files.Count)"}
$manifest=Join-Path $root 'payload-sha256.tsv'
[IO.File]::WriteAllLines($manifest,[string[]]@($files|ForEach-Object {$_.path+'|'+$_.bytes+'|'+$_.sha256}),[Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $Transfer|Out-Null
Copy-Item -LiteralPath $Zip -Destination (Join-Path $Transfer 'SouiWorkbench.zip')
Copy-Item -LiteralPath 'C:\SOUI-SDK-production-validation\guest-preview8-transfer-20260925\7za.exe' -Destination (Join-Path $Transfer '7za.exe')
foreach($name in @('run-demo-ui.cmd','verify-payload.ps1','export-serial.ps1','payload-sha256.tsv')){
  Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $Transfer $name)
}
$input=[ordered]@{
  schema='soui.win7-x86-preview8-demo-ui-input/v1';version='0.3.0-preview.8'
  sourceArchiveSha256='ef4c036e190d111273b8183bccbc931141a5f7fe3e8aafdf5b1989cc32903dc6'
  sourceSha256='ff9558accf330a1f40500991356b237df896bbbeb579533252550b5ea2b5e72e'
  architecture='x86';configuration='Release';payloadFiles=$files.Count
  payloadManifestSha256=Hash $manifest
  payloadZipSha256=Hash $Zip
  payloadZipBytes=(Get-Item -LiteralPath $Zip).Length
  exeSha256=Hash (Join-Path $Payload 'SouiWorkbench.exe')
  engineSha256=Hash (Join-Path $Payload 'node.dll')
  souiSha256=Hash (Join-Path $Payload 'soui.dll')
  sevenZipSha256=Hash (Join-Path $Transfer '7za.exe')
  initialPage='data';react=$true;renderer='cpu';width=760;height=520
  scripts=[ordered]@{};releaseReady=$false
}
foreach($name in @('run-demo-ui.cmd','verify-payload.ps1','export-serial.ps1')){$input.scripts[$name]=Hash (Join-Path $root $name)}
[IO.File]::WriteAllText((Join-Path $root 'input.json'),($input|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $root 'input.json') -Destination (Join-Path $Transfer 'input.json')
Write-Output "Transfer=$Transfer, payload files=$($files.Count), ZIP SHA-256=$($input.payloadZipSha256)"
