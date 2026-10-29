[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$sdkRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $sdkRoot 'tools/internal/Find-Toolchain.ps1')
. (Join-Path $sdkRoot 'tools/internal/CMake-Configure.ps1')
$null=Get-SouiToolchain
$cmake=(Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$package=& (Join-Path $sdkRoot 'tools/internal/Restore-WebView2.ps1')
$output=Join-Path $sdkRoot "out/scratch-raw-webview2-$Architecture"
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $sdkRoot `
  -SourceDirectory $PSScriptRoot -BuildDirectory $output -Architecture $Architecture)
& $cmake @configure "-DSOUI_WEBVIEW2_SDK_ROOT=$package"
if($LASTEXITCODE -ne 0){throw 'Raw WebView2 baseline configuration failed.'}
& $cmake --build $output --config $Configuration --target ScratchRawWebView2Baseline --parallel 2
if($LASTEXITCODE -ne 0){throw 'Raw WebView2 baseline build failed.'}
Write-Output (Join-Path $output "bin/$Configuration/ScratchRawWebView2Baseline.exe")
