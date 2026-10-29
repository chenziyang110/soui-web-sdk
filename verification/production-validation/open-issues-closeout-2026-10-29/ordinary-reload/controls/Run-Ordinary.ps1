[CmdletBinding()]
param([Parameter(Mandatory=$true,Position=0)][ValidateSet('ord')][string]$Kind,
  [Parameter(Mandatory=$true,Position=1)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,23}$')][string]$Stamp,
  [Parameter(Mandatory=$true,Position=2)][ValidatePattern('^[a-f0-9]{64}$')][string]$ManifestSha256,
  [Parameter(Mandatory=$true,Position=3)][ValidatePattern('^[a-f0-9]{64}$')][string]$StageSha256,
  [Parameter(Mandatory=$true,Position=4)][ValidateSet('ROOT_REVIEWED_CURRENT_STAGE_V1')][string]$RootGate)
. (Join-Path $PSScriptRoot 'Ordinary-Lib.ps1')
Assert-CurrentGuest
$paths=Get-CurrentPaths $Kind $Stamp
$payload=Read-CurrentManifest $paths $paths.root $ManifestSha256
if(Test-Path -LiteralPath $paths.run){throw 'Preserve prior current run'}
$closed=Read-CurrentClosedLauncher $paths.stage
if($closed.finishedValue.exitCode-ne0-or-not$closed.finishedValue.leaseReleased-or-not$closed.finishedValue.environmentRestored-or@($closed.finishedValue.restoreErrors).Count-ne0){throw 'Reviewed stage launcher did not finish cleanly'}
$stageFile=Join-Path $paths.stage 'Stage.actual.json';$stagePin=Get-CurrentPin $stageFile
if($stagePin.sha256-cne$StageSha256){throw 'Exact independently reviewed actual stage SHA required'}
$stage=[IO.File]::ReadAllText($stageFile,[Text.Encoding]::UTF8)|ConvertFrom-Json
if(-not$stage.passed-or$stage.productExecuted-or$stage.manifest.sha256-cne$ManifestSha256-or$stage.machine-ine$script:CurrentMachine){throw 'Stage identity or scope differs'}
# This explicit separate invocation is Root's review marker. It maps to the
# existing kind-specific gate; no automatic stage-to-run path is present.
$code=Invoke-CurrentController $paths 'run' $ManifestSha256 $StageSha256
Write-Output ('Run evidence: '+$paths.run+'; controllerExit='+$code)
exit $code
