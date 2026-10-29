[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture = 'x64', [string]$SourceVendor = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Build.ps1') -Architecture $Architecture -Configuration Release -ForPackage -SourceVendor $SourceVendor
if ($LASTEXITCODE -ne 0) { throw 'Scratch package build failed.' }
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildRoot = Join-Path $sdkRoot "out/scratch-$Architecture-package"
& (Join-Path $buildRoot 'Release/SouiScratch_export_contract.exe')
if ($LASTEXITCODE -ne 0) { throw 'Scratch export contract failed.' }
$packageArguments = @((Join-Path $PSScriptRoot 'package.mjs'), $Architecture)
if ($SourceVendor) { $packageArguments += @('--source-vendor', [IO.Path]::GetFullPath($SourceVendor)) }
& node @packageArguments
if ($LASTEXITCODE -ne 0) { throw 'Scratch package creation failed.' }
