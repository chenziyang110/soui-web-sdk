[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
$zip=Get-Command 7z.exe -ErrorAction SilentlyContinue
$zipPath=if($zip){$zip.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
if(-not(Test-Path -LiteralPath $zipPath)){throw 'Install 7-Zip or put 7z.exe on PATH.'}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'out/package'),(Join-Path $root 'dist') | Out-Null
foreach($target in @('SouiStarter','SouiWorkbench')){
 $archive=Join-Path $root "dist/$target-$version-windows-$Architecture.zip"
 if(Test-Path -LiteralPath $archive){throw "Archive already exists: $archive"}
 $stage=Join-Path $root ('out/package/'+$target+'-'+[Guid]::NewGuid().ToString('N'))
 $reportText=& node $evidence stage-app $Architecture Release $target $stage
 if($LASTEXITCODE -ne 0){throw 'Application evidence or staging failed; rebuild and rerun native tests.'}
 $report=$reportText | ConvertFrom-Json
 Push-Location $stage
 try{
  & $zipPath a -tzip -mx=5 -mmt=2 $archive '*' *> (Join-Path $root "out/package/$target-$Architecture.log")
  if($LASTEXITCODE -ne 0){throw 'Application archive failed'}
  & $zipPath t $archive *> (Join-Path $root "out/package/$target-$Architecture-test.log")
  if($LASTEXITCODE -ne 0){throw 'Application archive integrity check failed'}
 }finally{Pop-Location}
 $hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
 [IO.File]::WriteAllText(($archive+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($archive)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
 $report | Add-Member -NotePropertyName archive -NotePropertyValue ([ordered]@{file=[IO.Path]::GetFileName($archive);bytes=(Get-Item -LiteralPath $archive).Length;sha256=$hash;compression='7-Zip ZIP -mx=5 -mmt=2';createdAt=(Get-Date).ToString('o')})
 [IO.File]::WriteAllText(($archive+'.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 Write-Output "Application archive verified: $archive (uncompressed $($report.payload.bytes) bytes)"
}
