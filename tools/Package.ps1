[CmdletBinding()]
param([string]$OutputFile='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
if(-not $OutputFile){$OutputFile=Join-Path $root "dist/soui-sdk-$version-full-source.zip"}
$OutputFile=[IO.Path]::GetFullPath($OutputFile)
if(Test-Path -LiteralPath $OutputFile){throw 'Archive already exists; choose a new output path.'}
if(-not $OutputFile.StartsWith((Join-Path $root 'dist')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive output must be under the distribution dist directory.'}
$zip=Get-Command 7z.exe -ErrorAction SilentlyContinue
$zipPath=if($zip){$zip.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
if(-not(Test-Path -LiteralPath $zipPath)){throw 'Install 7-Zip or put 7z.exe on PATH.'}
New-Item -ItemType Directory -Force -Path (Split-Path $OutputFile) | Out-Null
& node (Join-Path $root 'tools/internal/source-archive.cjs')
if($LASTEXITCODE -ne 0){throw 'Source manifest creation failed'}
Push-Location $root
try {
 & $zipPath a -tzip -mx=5 -mmt=2 $OutputFile '@out/package/files.txt' -scsUTF-8 *> (Join-Path $root 'out/package/archive.log')
 if($LASTEXITCODE -ne 0){throw 'Archive creation failed; see out/package/archive.log'}
 & $zipPath t $OutputFile *> (Join-Path $root 'out/package/archive-test.log')
 if($LASTEXITCODE -ne 0){throw 'Archive verification failed'}
} finally {Pop-Location}
$hash=(Get-FileHash -LiteralPath $OutputFile -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($OutputFile+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($OutputFile)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output "Source archive verified: $OutputFile"
Write-Output "SHA256: $hash"
