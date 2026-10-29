[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture = 'x64',
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [string]$SourceVendor = '',
  [switch]$SkipWeb,
  [switch]$ForPackage
)
$ErrorActionPreference = 'Stop'
if ($SkipWeb -and $SourceVendor) { throw 'SourceVendor requires a fresh Web build; do not reuse another vendor output.' }
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$version = ([IO.File]::ReadAllText((Join-Path $sdkRoot 'VERSION'))).Trim()
if ($version -notmatch '^0\.6\.0($|-)') { throw 'This demo requires SOUI SDK 0.6.0.' }
$output = Join-Path $sdkRoot "out/scratch-$Architecture"
if ($ForPackage) { $output += '-package' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
if (!$SkipWeb) {
  & npm.cmd --prefix (Join-Path $PSScriptRoot 'web') ci --no-audit --no-fund
  if ($LASTEXITCODE -ne 0) { throw 'Scratch Web dependency installation failed.' }
  $webBuildArguments = @('--prefix', (Join-Path $PSScriptRoot 'web'), 'run', 'build')
  if ($SourceVendor) { $webBuildArguments += @('--', '--source-vendor', [IO.Path]::GetFullPath($SourceVendor)) }
  & npm.cmd @webBuildArguments
  if ($LASTEXITCODE -ne 0) { throw 'Scratch Web build failed.' }
}
if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'web/dist/index.html') -PathType Leaf)) {
  throw 'Scratch Web output is missing; run Build.ps1 without SkipWeb.'
}
. (Join-Path $sdkRoot 'tools/internal/Find-Toolchain.ps1')
. (Join-Path $sdkRoot 'tools/internal/CMake-Configure.ps1')
. (Join-Path $sdkRoot 'tools/internal/Msvc-CompilerLifecycle.ps1')
$toolchain = Get-SouiToolchain
$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$webview2Package = & (Join-Path $sdkRoot 'tools/internal/Restore-WebView2.ps1')
$configure = @(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $sdkRoot `
  -SourceDirectory $PSScriptRoot -BuildDirectory $output -Architecture $Architecture)
$nodeReuse = Get-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -ErrorAction SilentlyContinue
$compilerPhase = $null
$primaryFailure = $null
try {
  $env:MSBUILDDISABLENODEREUSE = '1'
  $compilerPhase = Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $Architecture -EvidenceRoot $output -Phase configure
  & $cmake @configure "-DSOUI_WEBVIEW2_SDK_ROOT=$webview2Package"
  Complete-SouiMsvcCompilerPhase $compilerPhase $LASTEXITCODE 'Scratch CMake configuration failed.'
  $targets = @('SouiScratch_resources')
  if ($ForPackage) { $targets += 'SouiScratch_export_contract' }
  $compilerPhase = Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $Architecture -EvidenceRoot $output -Phase build
  & $cmake --build $output --config $Configuration --target @targets --parallel 2
  Complete-SouiMsvcCompilerPhase $compilerPhase $LASTEXITCODE 'Scratch native build/deployment failed.'
} catch {
  $primaryFailure = $_
  throw
} finally {
  $cleanupFailures = [Collections.Generic.List[string]]::new()
  try { Close-SouiMsvcCompilerPhase $compilerPhase } catch { $cleanupFailures.Add($_.Exception.Message) }
  try {
    if ($null -ne $nodeReuse) { Set-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -Value $nodeReuse.Value -ErrorAction Stop }
    else { Remove-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -ErrorAction SilentlyContinue }
    $restored = Get-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -ErrorAction SilentlyContinue
    if (($null -ne $restored) -ne ($null -ne $nodeReuse) -or
        ($null -ne $nodeReuse -and $restored.Value -cne $nodeReuse.Value)) { throw 'Build environment restore differs.' }
  } catch { $cleanupFailures.Add($_.Exception.Message) }
  if ($cleanupFailures.Count) {
    if ($primaryFailure) { $primaryFailure.Exception.Data['cleanupFailures'] = $cleanupFailures.ToArray(); Write-Warning ($cleanupFailures -join '; ') }
    else { throw ($cleanupFailures -join '; ') }
  }
}
Write-Output (Join-Path $output "bin/$Configuration/SouiScratch/SouiScratch.exe")
