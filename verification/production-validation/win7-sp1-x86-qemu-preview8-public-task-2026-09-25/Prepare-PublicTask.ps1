[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Suite,
  [Parameter(Mandatory=$true)][string]$SourceArchive,
  [Parameter(Mandatory=$true)][string]$Transfer
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$runtime=Join-Path $Suite 'runtime'
$build=Get-Content -LiteralPath (Join-Path $Suite 'build.json') -Raw|ConvertFrom-Json
if($build.schema -ne 'soui.acceptance-build/v3' -or $build.architecture -ne 'x86' -or
   $build.configuration -ne 'Release' -or $build.kernel.kind -ne 'prebuilt' -or
   $build.releaseReady -ne $false){throw 'Expected a preview.8 x86 Release public SDK acceptance build'}
if(Test-Path -LiteralPath $Transfer){throw 'Transfer target already exists'}
if(-not(Test-Path -LiteralPath $runtime -PathType Container)){throw 'Runtime directory missing'}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
$files=@(Get-ChildItem -LiteralPath $runtime -Recurse -File|ForEach-Object {
  $relative=$_.FullName.Substring($runtime.Length+1).Replace('\','/')
  if($relative -cnotmatch '^[A-Za-z0-9_.\/-]+$' -or $relative.Contains('..')){throw "Unsafe payload path: $relative"}
  [pscustomobject]@{path=$relative;bytes=$_.Length;sha256=(Hash $_.FullName).ToUpperInvariant()}
}|Sort-Object path)
if($files.Count -ne 76){throw "Expected 76 runtime files, got $($files.Count)"}
$manifest=Join-Path $root 'payload-sha256.tsv'
[IO.File]::WriteAllLines($manifest,[string[]]@($files|ForEach-Object {$_.path+'|'+$_.bytes+'|'+$_.sha256}),[Text.Encoding]::ASCII)
$zip=Join-Path $Transfer 'SouiAcceptance.zip'
New-Item -ItemType Directory -Path $Transfer|Out-Null
Push-Location $runtime
try {
  & 'C:\Program Files\7-Zip\7z.exe' a -tzip -mx=5 $zip '.\*' *> (Join-Path $root 'zip-create.log')
  if($LASTEXITCODE -ne 0){throw 'Runtime ZIP creation failed'}
} finally {Pop-Location}
& 'C:\Program Files\7-Zip\7z.exe' t $zip *> (Join-Path $root 'zip-test.log')
if($LASTEXITCODE -ne 0){throw 'Runtime ZIP test failed'}
$check=Join-Path $Transfer 'checked'
& 'C:\Program Files\7-Zip\7z.exe' x $zip "-o$check" -y *> (Join-Path $root 'zip-extract-check.log')
if($LASTEXITCODE -ne 0){throw 'Runtime ZIP extraction failed'}
foreach($file in $files){
  $actual=Join-Path $check $file.path.Replace('/',[string][char]92)
  if(-not(Test-Path -LiteralPath $actual -PathType Leaf) -or
     (Get-Item -LiteralPath $actual).Length -ne $file.bytes -or
     (Hash $actual) -ne $file.sha256.ToLowerInvariant()){throw "Runtime ZIP differs: $($file.path)"}
}
if(@(Get-ChildItem -LiteralPath $check -Recurse -File).Count -ne $files.Count){throw 'Runtime ZIP has an extra file'}
Remove-Item -LiteralPath $check -Recurse -Force
Copy-Item -LiteralPath 'C:\SOUI-SDK-production-validation\guest-preview8-isolated-source-20260925\7za.exe' -Destination (Join-Path $Transfer '7za.exe')
$names=@('run-public-task.cmd','verify-payload.ps1','export-serial.ps1','payload-sha256.tsv')
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $Transfer $name)}
$input=[ordered]@{
  schema='soui.win7-x86-preview8-public-task-input/v1'
  version='0.3.0-preview.8'
  sourceArchiveSha256=Hash $SourceArchive
  buildJsonSha256=Hash (Join-Path $Suite 'build.json')
  buildId=$build.buildId
  consumerBuildId=$build.consumerBuildId
  sourceSha256=$build.sourceSha256
  payloadSha256=$build.payload.sha256
  architecture='x86';configuration='Release';kernelKind=$build.kernel.kind
  payloadFiles=$files.Count
  payloadManifestSha256=Hash $manifest
  payloadZipSha256=Hash $zip
  payloadZipBytes=(Get-Item -LiteralPath $zip).Length
  exeSha256=Hash (Join-Path $runtime 'SouiAcceptance.exe')
  engineSha256=Hash (Join-Path $runtime 'node.dll')
  souiSha256=Hash (Join-Path $runtime 'soui.dll')
  sevenZipSha256=Hash (Join-Path $Transfer '7za.exe')
  config=[ordered]@{mode='bridge';cycles=3;warmup=0;settleMs=100;timeoutMs=60000}
  scripts=[ordered]@{}
  releaseReady=$false
}
foreach($name in $names[0..2]){$input.scripts[$name]=Hash (Join-Path $root $name)}
[IO.File]::WriteAllText((Join-Path $root 'input.json'),($input|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $root 'input.json') -Destination (Join-Path $Transfer 'input.json')
Copy-Item -LiteralPath (Join-Path $Suite 'build.json') -Destination (Join-Path $root 'build.json')
Write-Output "Runtime ZIP SHA-256: $($input.payloadZipSha256)"
Write-Output "Transfer: $Transfer"
