[CmdletBinding()]
param([string]$SdkRoot = (Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference = 'Stop'
function Read([string]$relative) { [IO.File]::ReadAllText((Join-Path $SdkRoot $relative)) }
function Need([bool]$ok, [string]$message) { if (-not $ok) { throw $message } }
$cmake = Read 'sdk/renderers/webview2/CMakeLists.txt'
$webview = Read 'sdk/renderers/webview2/webview2_host.cpp'
$auto = Read 'sdk/renderers/auto/auto_webview_host.cpp'
$bridge = Read 'sdk/renderers/webview2/webview2_loader_bridge.cpp'
$header = Read 'sdk/renderers/webview2/webview2_loader_bridge.h'
Need ($cmake.Contains('option(SOUI_WEBVIEW2_LOADER_SHARED') -and $cmake.Contains('WebView2Loader.dll.lib')) 'Shared loader option is missing.'
Need ($cmake.Contains('WebView2LoaderStatic') -and $cmake.Contains('else()')) 'Static loader must remain the default.'
Need ($cmake.Contains('webview2_loader_bridge.cpp') -and $cmake.Contains('target_compile_definitions(soui_auto_host PRIVATE SOUI_WEBVIEW2_LOADER_SHARED=1)')) 'Shared loader target wiring is incomplete.'
Need ($webview.Contains('#include "webview2_loader_bridge.h"') -and $webview.Contains('detail::webview2_loader_bridge::create_environment(')) 'WebView2 host does not use the bridge.'
Need ($auto.Contains('#include "../webview2/webview2_loader_bridge.h"') -and $auto.Contains('detail::webview2_loader_bridge::get_available_version(')) 'Auto host does not use the bridge.'
Need ($bridge.Contains('LoadLibraryExW') -and $bridge.Contains('GetProcAddress') -and $bridge.Contains('ERROR_FILE_NOT_FOUND') -and $bridge.Contains('ERROR_PROC_NOT_FOUND')) 'Bridge does not fail as an optional dependency.'
Need ($header.Contains('ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler')) 'Bridge ABI declaration is incomplete.'
Write-Output 'WebView2 shared-loader source contract passed; native build/runtime acceptance remains separate.'
