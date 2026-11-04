[CmdletBinding()]
param([Parameter(Mandatory=$true,Position=0)][ValidateSet('ord')][string]$Kind,
  [Parameter(Mandatory=$true,Position=1)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256,
  [Parameter(Mandatory=$true,Position=2)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,23}$')][string]$Stamp)
. (Join-Path $PSScriptRoot 'Ordinary-Lib.ps1')
Assert-CurrentGuest
$paths=Get-CurrentPaths $Kind $Stamp
if(Test-Path -LiteralPath $paths.root){throw 'Fresh current root required; preserve all old roots'}
if((Test-Path -LiteralPath $paths.stage)-or(Test-Path -LiteralPath $paths.run)){throw 'Fresh stage/run evidence names required'}
$payload=Read-CurrentManifest $paths 'D:\' $ManifestSha256
New-Item -ItemType Directory -Path $paths.root|Out-Null
$copies=[Collections.Generic.List[object]]::new();$code=1
try{
  foreach($source in $payload.selection){
    Assert-CurrentPin $source
    $target=Join-Path $paths.root $source.relative
    [IO.File]::Copy($source.path,$target,$false)
    $copied=Get-CurrentPin $target
    if($copied.bytes-ne$source.bytes-or$copied.sha256-cne$source.sha256){throw 'Current file copy differs'}
    Assert-CurrentPin $source
    $copies.Add(@{source=$source;copy=$copied})
  }
  $expected=@($payload.selection.relative|Sort-Object);$actual=@(Get-ChildItem -LiteralPath $paths.root -Force|ForEach-Object{if($_.PSIsContainer-or($_.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Flat current copy required'};$_.Name}|Sort-Object)
  if(($expected-join"`n")-cne($actual-join"`n")){throw 'Copied payload closed set differs'}
  foreach($source in $payload.selection){Assert-CurrentPin $source}
  Write-NewCurrentJson (Join-Path $paths.root 'InputCopy.actual.json') @{schema='soui.current-flat-payload-copy/v1';kind=$Kind;stamp=$Stamp;source='D:\';manifestSha256=$ManifestSha256;copies=@($copies.ToArray());files=21;oldRootsChanged=$false;productExecuted=$false;releaseReady=$false}
  $code=Invoke-CurrentController $paths 'stage' $ManifestSha256 ''
  if(Test-Path -LiteralPath $paths.stage){[IO.File]::Copy((Join-Path $paths.root 'InputCopy.actual.json'),(Join-Path $paths.stage 'InputCopy.actual.json'),$false)}
}catch{
  $failure=@{message=$_.Exception.Message;kind=$Kind;stamp=$Stamp;copies=@($copies.ToArray());productExecutedByWrapper=$false;releaseReady=$false}
  Write-NewCurrentJson (Join-Path $paths.root 'CopyOrStage.error.json') $failure
  throw
}
Write-Output ('Stage only: '+$paths.stage+'; controllerExit='+$code)
exit $code
