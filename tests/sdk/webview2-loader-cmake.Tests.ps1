[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$WebView2SdkRoot,
  [Parameter(Mandatory)][string]$OutputRoot,
  [ValidateSet('x86','x64')][string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$WebView2SdkRoot = [IO.Path]::GetFullPath($WebView2SdkRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Use a fresh output directory; retain prior results.' }
[IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
function Run([string[]]$Arguments) {
  & cmake @Arguments
  if ($LASTEXITCODE -ne 0) { throw ('CMake failed: '+($Arguments -join ' ')) }
}
function Put([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false)) }
$platform = if ($Architecture -eq 'x86') { 'Win32' } else { 'x64' }
$source = Join-Path $OutputRoot 'configure-fixture'
[IO.Directory]::CreateDirectory($source) | Out-Null
# Configure the production renderer CMake, without substituting its dependency,
# link or install rules. This test does not claim a native SDK build.
$cmake = @'
cmake_minimum_required(VERSION 3.24)
project(LoaderConfigureFixture LANGUAGES CXX)
set(SOUI_DISTRIBUTION_ROOT "@ROOT@")
add_library(soui_sdk INTERFACE)
add_library(Soui::Sdk ALIAS soui_sdk)
install(TARGETS soui_sdk EXPORT SouiSdkTargets)
set(SOUI_BUILD_WEBVIEW2 ON)
set(SOUI_BUILD_AUTO_HOST ON)
add_subdirectory("@ROOT@/sdk/renderers/webview2" renderer)
include(CMakePackageConfigHelpers)
configure_package_config_file("@ROOT@/sdk/cmake/SouiSdkConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/SouiSdkConfig.cmake" INSTALL_DESTINATION lib/cmake/SouiSdk)
get_target_property(links soui_webview2 INTERFACE_LINK_LIBRARIES)
get_target_property(runtime soui_webview2 SOUI_WEBVIEW2_LOADER_RUNTIME)
get_target_property(definitions soui_auto_host COMPILE_DEFINITIONS)
file(WRITE "${CMAKE_BINARY_DIR}/loader-properties.txt" "${links}\n${runtime}\n${definitions}\n")
'@
Put (Join-Path $source 'CMakeLists.txt') ($cmake.Replace('@ROOT@',$root.Replace('\','/')))
$build = Join-Path $OutputRoot 'configure-build'
$results = @()
foreach ($mode in @('OFF','ON','OFF')) {
  Run -Arguments @('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A',($platform+',version=10.0.19041.0'),
    ('-DSOUI_WEBVIEW2_SDK_ROOT='+$WebView2SdkRoot),('-DSOUI_WEBVIEW2_LOADER_SHARED='+$mode))
  $properties = [IO.File]::ReadAllText((Join-Path $build 'loader-properties.txt'))
  $config = [IO.File]::ReadAllText((Join-Path $build 'SouiSdkConfig.cmake'))
  if ($mode -eq 'ON') {
    if ($properties.Contains('WebView2LoaderStatic.lib') -or $properties.Contains('INSTALL_INTERFACE:Soui::WebView2Loader') -or
        -not $properties.Contains('WebView2Loader.dll') -or -not $properties.Contains('SOUI_WEBVIEW2_LOADER_SHARED=1')) {
      throw 'Shared mode retained a static link or lost its runtime/AutoHost definition.'
    }
    if (-not $config.Contains('NOT "ON"') -or -not $config.Contains('${PACKAGE_PREFIX_DIR}/bin/webview2/WebView2Loader.dll')) {
      throw 'Installed shared configuration has no explicit optional-loader mode.'
    }
  } else {
    if (-not $properties.Contains('WebView2LoaderStatic.lib') -or $properties.Contains('SOUI_WEBVIEW2_LOADER_SHARED=1')) {
      throw 'Reconfigured static mode did not restore the static loader contract.'
    }
  }
  $results += @{mode=$mode;passed=$true}
}
$report = @{passed=$true;architecture=$Architecture;cases=$results;guiExecuted=$false;
  scope='actual production CMake reconfiguration only; no SDK compilation, installation or browser execution'}
Put (Join-Path $OutputRoot 'result.json') ($report | ConvertTo-Json -Depth 6)
$report | ConvertTo-Json -Depth 6
