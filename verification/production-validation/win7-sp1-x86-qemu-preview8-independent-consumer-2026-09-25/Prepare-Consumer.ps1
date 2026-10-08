[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$payload=Join-Path $root 'payload\Preview8Win7'
$source='C:\SOUI-SDK-production-validation\preview8-final-source-extracted-v2-20260925'
$project='C:\SOUI-SDK-production-validation\preview8-final-win7-project-20260925'
$transfer='C:\SOUI-SDK-production-validation\guest-preview8-consumer-transfer-20260925'
$archive='C:\SOUI-SDK-production-validation\soui-sdk-0.3.0-preview.8-full-source-final.zip'
if(Test-Path -LiteralPath $transfer){throw 'Transfer target already exists'}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne
    '00C405582079762CF01778841FFEA1A9B91392897B6B1E7178778330657241B5'){
  throw 'Final source archive changed'
}
$build=Get-Content -LiteralPath (Join-Path $project 'out\x86\build-Release.json') -Raw|ConvertFrom-Json
if($build.architecture -ne 'x86' -or $build.configuration -ne 'Release'){
  throw 'Expected an x86 Release independent project build'
}
Copy-Item -LiteralPath 'C:\SOUI-SDK-production-validation\guest-preview8-isolated-source-20260925\7za.exe' -Destination (Join-Path $root '7za.exe')
Copy-Item -LiteralPath 'C:\SOUI-SDK-production-validation\guest-preview8-isolated-source-20260925\export-serial.ps1' -Destination (Join-Path $root 'export-serial.ps1')
$files=@(Get-ChildItem -LiteralPath $payload -Recurse -File|ForEach-Object {
  $relative=$_.FullName.Substring($payload.Length+1).Replace('\','/')
  if($relative -notmatch '^[A-Za-z0-9_.\/-]+$' -or $relative.Contains('..')){
    throw "Unsupported transfer path: $relative"
  }
  [pscustomobject]@{path=$relative;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}|Sort-Object path)
if($files.Count -ne 71){throw "Expected 71 payload files, got $($files.Count)"}
$manifest=Join-Path $root 'payload-sha256.tsv'
$lines=@($files|ForEach-Object {$_.path+'|'+$_.bytes+'|'+$_.sha256})
[IO.File]::WriteAllLines($manifest,[string[]]$lines,[Text.Encoding]::ASCII)
$zip=Join-Path $root 'Preview8Win7.zip'
Push-Location $payload
try {
  & 'C:\Program Files\7-Zip\7z.exe' a -tzip -mx=5 $zip '.\*' *> (Join-Path $root 'zip-create.log')
  if($LASTEXITCODE -ne 0){throw 'Consumer ZIP creation failed'}
} finally {Pop-Location}
& 'C:\Program Files\7-Zip\7z.exe' t $zip *> (Join-Path $root 'zip-test.log')
if($LASTEXITCODE -ne 0){throw 'Consumer ZIP test failed'}
$check=Join-Path $root 'check-Preview8Win7'
if(Test-Path -LiteralPath $check){throw 'Consumer ZIP check directory exists'}
& 'C:\Program Files\7-Zip\7z.exe' x $zip "-o$check" -y *> (Join-Path $root 'zip-extract-check.log')
if($LASTEXITCODE -ne 0){throw 'Consumer ZIP extraction failed'}
foreach($file in $files){
  $actual=Join-Path $check ($file.path.Replace('/',[string][char]92))
  if(-not(Test-Path -LiteralPath $actual -PathType Leaf) -or
      (Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash -ne $file.sha256){
    throw "Consumer ZIP content differs: $($file.path)"
  }
}
if(@(Get-ChildItem -LiteralPath $check -Recurse -File).Count -ne $files.Count){
  throw 'Consumer ZIP has an unexpected file'
}
$input=[ordered]@{
  schema='soui.win7-x86-preview8-independent-consumer-input/v1'
  version='0.3.0-preview.8'
  sourceArchiveSha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  sdkManifestSha256=(Get-FileHash -LiteralPath (Join-Path $source 'dist\sdk\0.3.0-preview.8\sdk-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
  sdkLockSha256=(Get-FileHash -LiteralPath (Join-Path $project 'sdk-lock.json') -Algorithm SHA256).Hash.ToLowerInvariant()
  buildId=$build.buildId
  sourceSha256=$build.source.sha256
  architecture='x86'
  configuration='Release'
  payloadFiles=$files.Count
  payloadManifestSha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
  payloadZipSha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
  payloadZipBytes=(Get-Item -LiteralPath $zip).Length
  exeSha256=(Get-FileHash -LiteralPath (Join-Path $payload 'Preview8Win7.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
  engineSha256=(Get-FileHash -LiteralPath (Join-Path $payload 'node.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
  souiSha256=(Get-FileHash -LiteralPath (Join-Path $payload 'soui.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
  sevenZipSha256=(Get-FileHash -LiteralPath (Join-Path $root '7za.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
  scripts=[ordered]@{}
  releaseReady=$false
}
foreach($name in @('run-consumer.cmd','verify-consumer.ps1','export-serial.ps1')){
  $input.scripts[$name]=(Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
[IO.File]::WriteAllText((Join-Path $root 'input.json'),($input|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $transfer|Out-Null
foreach($name in @('7za.exe','Preview8Win7.zip','payload-sha256.tsv','run-consumer.cmd','verify-consumer.ps1','export-serial.ps1','input.json')){
  Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $transfer $name)
}
Write-Output "Consumer guest transfer: $transfer"
Write-Output "Consumer ZIP SHA-256: $($input.payloadZipSha256)"
