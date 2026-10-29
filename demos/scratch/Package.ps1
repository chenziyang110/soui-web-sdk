[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture = 'x64', [string]$SourceVendor = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Build.ps1') -Architecture $Architecture -Configuration Release -ForPackage -SourceVendor $SourceVendor
if ($LASTEXITCODE -ne 0) { throw 'Scratch package build failed.' }
$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildRoot = Join-Path $sdkRoot "out/scratch-$Architecture-package"
& $cmake --build $buildRoot --config Release --target SouiScratch_export_contract --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Scratch export contract build failed.' }
& (Join-Path $buildRoot 'Release/SouiScratch_export_contract.exe')
if ($LASTEXITCODE -ne 0) { throw 'Scratch export contract failed.' }
& node (Join-Path $PSScriptRoot 'package.mjs') $Architecture
if ($LASTEXITCODE -ne 0) { throw 'Scratch package creation failed.' }
