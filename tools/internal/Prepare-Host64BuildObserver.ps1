[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$ProfileRequestFile,
 [Parameter(Mandatory)][string]$DevkitRoot,
 [Parameter(Mandatory)][string]$CMakeExe,
 [Parameter(Mandatory)][string]$NodeExe,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [switch]$Prepare
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
# Planning does not configure, compile or start a native process. Preparation
# is a separate explicit operation; standard-build never compiles this helper.
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$mode=if($Prepare){'prepare'}else{'plan'}
& $NodeExe (Join-Path $PSScriptRoot 'host64-build-debug-observer.cjs') $mode $root $ProfileRequestFile $DevkitRoot $CMakeExe $OutputDirectory
if($LASTEXITCODE-ne0){throw 'Host observer preparation rejected'}
