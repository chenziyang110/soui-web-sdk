[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string[]]$InputPath,
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [ValidateSet('zip','7z-balanced','7z-small')][string[]]$Profiles=@('zip','7z-balanced','7z-small'),
  [ValidateRange(1,10)][int]$ExtractionRuns=3,
  [ValidateRange(1,3600)][int]$TimeoutSeconds=180,
  [string]$SevenZipPath=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $SevenZipPath){
  $archiveTool=Get-Command 7z.exe -ErrorAction SilentlyContinue
  $SevenZipPath=if($archiveTool){$archiveTool.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
}
$arguments=@((Join-Path $root 'tools/internal/compression-measurement.cjs'),'--output',
  [IO.Path]::GetFullPath($OutputRoot),'--tool',[IO.Path]::GetFullPath($SevenZipPath),
  '--extraction-runs',[string]$ExtractionRuns,'--timeout-ms',[string]($TimeoutSeconds*1000))
foreach($inputDirectory in $InputPath){$arguments+=@('--input',[IO.Path]::GetFullPath($inputDirectory))}
foreach($profile in $Profiles){$arguments+=@('--profile',$profile)}
& node @arguments
if($LASTEXITCODE -ne 0){throw 'Compression measurement failed; inspect its preserved partial report and logs.'}
