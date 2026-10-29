[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$sdkRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $sdkRoot 'tools/internal/Find-Toolchain.ps1')
. (Join-Path $sdkRoot 'tools/internal/CMake-Configure.ps1')
. (Join-Path $sdkRoot 'tools/internal/Msvc-CompilerLifecycle.ps1')
$toolchain=Get-SouiToolchain
$cmake=(Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$package=& (Join-Path $sdkRoot 'tools/internal/Restore-WebView2.ps1')
$output=Join-Path $sdkRoot "out/scratch-raw-webview2-$Architecture"
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $sdkRoot `
  -SourceDirectory $PSScriptRoot -BuildDirectory $output -Architecture $Architecture)
$nodeReuse = Get-Item -LiteralPath 'Env:MSBUILDDISABLENODEREUSE' -ErrorAction SilentlyContinue
$compilerPhase = $null
$primaryFailure = $null
try {
  $env:MSBUILDDISABLENODEREUSE = '1'
  $compilerPhase = Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $Architecture -EvidenceRoot $output -Phase configure
  & $cmake @configure "-DSOUI_WEBVIEW2_SDK_ROOT=$package"
  Complete-SouiMsvcCompilerPhase $compilerPhase $LASTEXITCODE 'Raw WebView2 baseline configuration failed.'
  $compilerPhase = Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $Architecture -EvidenceRoot $output -Phase build
  & $cmake --build $output --config $Configuration --target ScratchRawWebView2Baseline --parallel 2
  Complete-SouiMsvcCompilerPhase $compilerPhase $LASTEXITCODE 'Raw WebView2 baseline build failed.'
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
Write-Output (Join-Path $output "bin/$Configuration/ScratchRawWebView2Baseline.exe")
