[CmdletBinding()]
param([string]$OutputFile='', [string]$SourceRootsBinding='', [string]$PackageWorkspace='')
$ErrorActionPreference='Stop'
function Get-SouiExplicitSourcePackagePaths {
 param([string]$Workspace, [string]$Archive='')
 if(-not $Workspace -or $Workspace -notmatch '^[A-Za-z]:[\\/]' -or $Workspace -match '[;"\x00-\x1f]'){throw 'Explicit source roots require a fresh absolute external PackageWorkspace.'}
 $Workspace=[IO.Path]::GetFullPath($Workspace)
 if(Test-Path -LiteralPath $Workspace){throw 'Explicit package workspace already exists.'}
 $distribution=Join-Path $Workspace 'dist'
 if($Archive){
  $Archive=[IO.Path]::GetFullPath($Archive)
  if(-not $Archive.StartsWith($distribution+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Explicit archive must be under PackageWorkspace/dist.'}
 }
 return @{workspace=$Workspace;distribution=$distribution;stage=(Join-Path $Workspace 'source');archive=$Archive;
  archiveLog=(Join-Path $Workspace 'archive.log');testLog=(Join-Path $Workspace 'archive-test.log')}
}
function Get-SouiStagedSourcePackageVersion {
 param([string]$Stage, $StageResult)
 $version=([IO.File]::ReadAllText((Join-Path $Stage 'VERSION'))).Trim()
 if($StageResult.version -cne $version -or $version -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$'){throw 'Staged source version differs or is invalid.'}
 return $version
}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($SourceRootsBinding){
 $explicit=Get-SouiExplicitSourcePackagePaths -Workspace $PackageWorkspace -Archive $OutputFile
 $distribution=$explicit.distribution
 $OutputFile=$explicit.archive
 $stage=$explicit.stage
 $archiveLog=$explicit.archiveLog
 $testLog=$explicit.testLog
} else {
 if($PackageWorkspace){throw 'PackageWorkspace requires SourceRootsBinding.'}
 $version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
 if(-not $OutputFile){$OutputFile=Join-Path $root "dist/soui-sdk-$version-full-source.zip"}
 $OutputFile=[IO.Path]::GetFullPath($OutputFile)
 if(Test-Path -LiteralPath $OutputFile){throw 'Archive already exists; choose a new output path.'}
 if(-not $OutputFile.StartsWith((Join-Path $root 'dist')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive output must be under the distribution dist directory.'}
 $stage=Join-Path $root ('out/package/source-'+[Guid]::NewGuid().ToString('N'))
 $archiveLog=Join-Path $root 'out/package/archive.log'
 $testLog=Join-Path $root 'out/package/archive-test.log'
}
$zip=Get-Command 7z.exe -ErrorAction SilentlyContinue
$zipPath=if($zip){$zip.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
if(-not(Test-Path -LiteralPath $zipPath)){throw 'Install 7-Zip or put 7z.exe on PATH.'}
if($SourceRootsBinding){
 $stageText=& node (Join-Path $root 'tools/internal/source-archive.cjs') stage $stage --source-roots-binding $SourceRootsBinding
 if($LASTEXITCODE -ne 0){throw 'Explicit source snapshot failed; input roots and manifest pins must match.'}
 $stageResult=($stageText -join [Environment]::NewLine)|ConvertFrom-Json
 $version=Get-SouiStagedSourcePackageVersion -Stage $stage -StageResult $stageResult
 if(-not $OutputFile){$OutputFile=Join-Path $distribution "soui-sdk-$version-full-source.zip"}
} else {
 New-Item -ItemType Directory -Force -Path (Split-Path $OutputFile) | Out-Null
 & node (Join-Path $root 'tools/internal/source-archive.cjs') stage $stage
 if($LASTEXITCODE -ne 0){throw 'Source snapshot failed; add reviewed files and refresh the manifest before packaging.'}
}
if(Test-Path -LiteralPath $OutputFile){throw 'Archive already exists; choose a new output path.'}
New-Item -ItemType Directory -Force -Path (Split-Path $OutputFile) | Out-Null
Push-Location $stage
try {
 # -scsUTF-8 controls list files only; -mcu=on writes UTF-8 ZIP filename headers.
 & $zipPath a -tzip -mx=5 -mmt=2 -mcu=on $OutputFile '*' -scsUTF-8 *> $archiveLog
 if($LASTEXITCODE -ne 0){throw 'Archive creation failed; see out/package/archive.log'}
 & $zipPath t $OutputFile *> $testLog
 if($LASTEXITCODE -ne 0){throw 'Archive verification failed'}
} finally {Pop-Location}
$hash=(Get-FileHash -LiteralPath $OutputFile -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($OutputFile+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($OutputFile)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output "Source archive verified: $OutputFile"
Write-Output "SHA256: $hash"
