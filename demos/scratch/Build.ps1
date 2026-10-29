[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture = 'x64',
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [switch]$SkipWeb,
  [switch]$ForPackage
)
$ErrorActionPreference = 'Stop'
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$version = ([IO.File]::ReadAllText((Join-Path $sdkRoot 'VERSION'))).Trim()
if ($version -notmatch '^0\.6\.0($|-)') { throw 'This demo requires SOUI SDK 0.6.0.' }
$output = Join-Path $sdkRoot "out/scratch-$Architecture"
if ($ForPackage) { $output += '-package' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
if (!$SkipWeb) {
  & npm.cmd --prefix (Join-Path $PSScriptRoot 'web') ci --no-audit --no-fund
  if ($LASTEXITCODE -ne 0) { throw 'Scratch Web dependency installation failed.' }
  & npm.cmd --prefix (Join-Path $PSScriptRoot 'web') run build
  if ($LASTEXITCODE -ne 0) { throw 'Scratch Web build failed.' }
}
if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'web/dist/index.html') -PathType Leaf)) {
  throw 'Scratch Web output is missing; run Build.ps1 without SkipWeb.'
}
. (Join-Path $sdkRoot 'tools/internal/Find-Toolchain.ps1')
. (Join-Path $sdkRoot 'tools/internal/CMake-Configure.ps1')
$null = Get-SouiToolchain
$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$webview2Package = & (Join-Path $sdkRoot 'tools/internal/Restore-WebView2.ps1')
$configure = @(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $sdkRoot `
  -SourceDirectory $PSScriptRoot -BuildDirectory $output -Architecture $Architecture)
& $cmake @configure "-DSOUI_WEBVIEW2_SDK_ROOT=$webview2Package"
if ($LASTEXITCODE -ne 0) { throw 'Scratch CMake configuration failed.' }
& $cmake --build $output --config $Configuration --target SouiScratch_resources --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Scratch native build/deployment failed.' }
Write-Output (Join-Path $output "bin/$Configuration/SouiScratch/SouiScratch.exe")
