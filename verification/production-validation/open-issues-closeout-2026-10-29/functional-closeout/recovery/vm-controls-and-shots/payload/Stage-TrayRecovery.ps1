[CmdletBinding()]
param([Parameter(Mandatory=$true,Position=0)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256)
. (Join-Path $PSScriptRoot 'TrayRecovery-Lib.ps1')
Assert-CurrentGuest
$paths=Get-CurrentPaths 'stage'
if((Test-Path -LiteralPath $paths.root)-or(Test-Path -LiteralPath $paths.stage)-or(Test-Path -LiteralPath 'C:\issue-closeout-evidence\trayrc-a1-x64')-or(Test-Path -LiteralPath 'C:\issue-closeout-evidence\trayrc-a1-x86')){throw 'Fresh exact tray recovery namespace required; preserve old roots'}
$payload=Read-CurrentManifest $paths 'D:\' $ManifestSha256
$sourceClosed=@(Get-CurrentFiles 'D:\'|ForEach-Object{[IO.Path]::GetFileName($_)}|Sort-Object)
if(($sourceClosed-join"`n")-cne(@($payload.selection.relative|Sort-Object)-join"`n")){throw 'Read-only source ISO membership differs'}
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
  $expected=@($payload.selection.relative|Sort-Object)
  $actual=@(Get-ChildItem -LiteralPath $paths.root -Force|ForEach-Object{if($_.PSIsContainer-or($_.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Flat current copy required'};$_.Name}|Sort-Object)
  if(($expected-join"`n")-cne($actual-join"`n")){throw 'Copied payload closed set differs'}
  foreach($source in $payload.selection){Assert-CurrentPin $source}
  $sourceAfter=@(Get-CurrentFiles 'D:\'|ForEach-Object{[IO.Path]::GetFileName($_)}|Sort-Object)
  if(($sourceAfter-join"`n")-cne($sourceClosed-join"`n")){throw 'Source ISO membership changed'}
  Write-NewCurrentJson (Join-Path $paths.root 'InputCopy.actual.json') @{schema='soui.current-flat-payload-copy/v1';kind='tray-recovery';stamp='a1';source='D:\';manifestSha256=$ManifestSha256;copies=@($copies.ToArray());files=9;oldRootsChanged=$false;productExecuted=$false;releaseReady=$false}
  $code=Invoke-CurrentController $paths 'stage' $ManifestSha256 ''
  if(Test-Path -LiteralPath $paths.stage){[IO.File]::Copy((Join-Path $paths.root 'InputCopy.actual.json'),(Join-Path $paths.stage 'InputCopy.actual.json'),$false)}
}catch{
  Write-NewCurrentJson (Join-Path $paths.root 'CopyOrStage.error.json') @{message=$_.Exception.Message;copies=@($copies.ToArray());productExecutedByWrapper=$false;releaseReady=$false}
  throw
}
Write-Output ('Stage only: '+$paths.stage+'; controllerExit='+$code)
exit $code
