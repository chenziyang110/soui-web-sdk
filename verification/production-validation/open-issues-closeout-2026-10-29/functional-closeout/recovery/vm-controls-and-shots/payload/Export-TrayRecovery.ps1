[CmdletBinding()]
param([Parameter(Mandatory=$true,Position=0)][ValidateSet('stage','x64','x86')][string]$Phase,
 [Parameter(Mandatory=$true,Position=1)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256)
. (Join-Path $PSScriptRoot 'TrayRecovery-Lib.ps1')
Assert-CurrentGuest
$paths=Get-CurrentPaths $Phase
$folder=if($Phase-ceq'stage'){$paths.stage}else{$paths.run}
$folderItem=Get-Item -LiteralPath $folder -Force
if(-not$folderItem.PSIsContainer-or($folderItem.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Plain current evidence root required'}
$payload=Read-CurrentManifest $paths $paths.root $ManifestSha256
$iso=Read-CurrentManifest $paths 'D:\' $ManifestSha256
$closed=Read-CurrentClosedLauncher $folder
if($Phase-cne'stage'){
  $ready=[IO.File]::ReadAllText((Join-Path $folder 'ready.json'),[Text.Encoding]::UTF8)|ConvertFrom-Json
  $result=[IO.File]::ReadAllText((Join-Path $folder 'finished.json'),[Text.Encoding]::UTF8)|ConvertFrom-Json
  $context=[IO.File]::ReadAllText((Join-Path $folder 'owned.ndjson.context.json'),[Text.Encoding]::UTF8)|ConvertFrom-Json
  if(-not$result.closureConfirmed-or-not$result.terminal.rootExited-or$result.terminal.activeProcesses-ne0-or$context.timeoutMs-ne100000-or$context.drainMs-ne2000-or$ready.pid-ne$result.pid-or$ready.creationFileTime-cne$result.creationFileTime-or$result.reportSha256-cne(Get-CurrentPin (Join-Path $folder 'owned.ndjson')).sha256){throw 'Actual native Owned closure/context differs; preserve pending evidence'}
  $nativeEnded=Assert-CurrentProcessEnded $ready.pid ([string]$ready.creationFileTime)
}else{$nativeEnded=$null}
foreach($name in @('ExportFiles.actual.json','SerialExport.actual.json','ExportPreflight.actual.json')){if(Test-Path -LiteralPath (Join-Path $folder $name)){throw 'One fresh export only; preserve earlier catalogs and serial markers'}}
$exporter=Get-CurrentPin 'C:\issues8\Guest-Export.ps1'
if($exporter.sha256-cne'f1cc23cfa6d76d35b2a6d8dd0779ceb332a42b51f74271bf097b5e9b44f1ce7c'){throw 'Existing unchanged serial exporter identity differs'}
Write-NewCurrentJson (Join-Path $folder 'ExportPreflight.actual.json') @{schema='soui.current-closed-export-preflight/v1';kind='tray-recovery';phase=$Phase;nativeRoot=$nativeEnded;machine=$script:CurrentMachine;observedUtc=[DateTime]::UtcNow.ToString('o');closedLauncher=$closed;exporter=$exporter;scope='Closed exact fresh evidence files only; no whole serial, private catalog or external inputs';testPassed=$null;releaseReady=$false}
$files=@(Get-CurrentFiles $folder|Sort-Object)
if($files.Count-gt512){throw 'More than512 files; preserve full tree and prepare explicit complete batches'}
$pins=@();$bytes=[long]0
foreach($file in $files){$pin=Get-CurrentPin $file;$pins+=,$pin;$bytes+=$pin.bytes;if($pin.bytes-gt16777216-or$bytes-gt8388608){throw 'Evidence exceeds8MiB; preserve originals, no truncation'}}
$catalogPath=Join-Path $folder 'ExportFiles.actual.json'
Write-NewCurrentJson $catalogPath @{schema='soui.issue-short-export-files/v1';files=$pins;totalBytes=$bytes;scope='Exact full closed current stage/run tree; exporter adds its own catalog and SerialExport marker only';testPassed=$null;releaseReady=$false}
$catalogPin=Get-CurrentPin $catalogPath
if($bytes+$catalogPin.bytes-gt8388608){throw 'Complete catalog plus original files exceeds8MiB; do not export a partial tree'}
foreach($pin in $pins){Assert-CurrentPin $pin}
$actual=@(Get-CurrentFiles $folder|Sort-Object);$expected=@(($files+@($catalogPath))|Sort-Object)
if(($actual-join"`n")-cne($expected-join"`n")){throw 'Evidence membership changed before export'}
Assert-CurrentPin $exporter
$previous=$ErrorActionPreference;$nativeExit=$null
try{$ErrorActionPreference='Continue';$global:LASTEXITCODE=$null;& (Get-CurrentPowerShell) -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $exporter.path -EvidenceDirectory $folder;$nativeExit=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
if($null-eq$nativeExit){throw 'Actual serial exporter process exit missing'}
foreach($pin in $pins){Assert-CurrentPin $pin};Assert-CurrentPin $catalogPin;Assert-CurrentPin $exporter
$actual=@(Get-CurrentFiles $folder|Sort-Object);$expected=@(($files+@($catalogPath,(Join-Path $folder 'SerialExport.actual.json')))|Sort-Object)
if(($actual-join"`n")-cne($expected-join"`n")){throw 'Evidence changed during export; preserve entire original tree'}
Write-Output ('Exporter actual exit='+$nativeExit+'; test outcome retained in original records')
exit $nativeExit
