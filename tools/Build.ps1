[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [switch]$SdkOnly,
  [switch]$SkipWeb,
  [switch]$WithWebView2,
  [switch]$Install,
  [switch]$FreshConfigure
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
. (Join-Path $PSScriptRoot 'internal/CMake-Configure.ps1')
$toolchain=Get-SouiToolchain
$cmake=Get-Command cmake.exe -ErrorAction Stop
$node=Get-Command node.exe -ErrorAction Stop
$npm=Get-Command npm.cmd -ErrorAction Stop
$generated=Test-Path -LiteralPath (Join-Path $root 'project.json')
if($generated){
  $projectMetadata=Get-Content -Raw -LiteralPath (Join-Path $root 'project.json') | ConvertFrom-Json
  if($projectMetadata.runtimeProfile -eq 'modern'){$WithWebView2=$true}
}
if(Test-Path -LiteralPath (Join-Path $root 'sdk-lock.json')) {
  & $node.Source (Join-Path $root 'tools/sdk/soui-sdk.cjs') verify-project --project $root
  if($LASTEXITCODE -ne 0){throw 'Pinned SDK was modified; install a reviewed SDK version or restore its files.'}
}
$output=Join-Path $root "out/$(if($SdkOnly){'sdk-'}else{''})$Architecture"
$source=if($SdkOnly){Join-Path $root 'sdk'}else{$root}
$configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake.Source -WorkspaceRoot $root -SourceDirectory $source `
  -BuildDirectory $output -Architecture $Architecture -FreshConfigure:$FreshConfigure)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
$evidenceArgs=@($Architecture,$Configuration)
if($SdkOnly){$evidenceArgs+='sdk-only'}
& $node.Source $evidence start @evidenceArgs
if($LASTEXITCODE -ne 0){throw 'Build identity initialization failed'}
if(-not $SkipWeb) {
  $webRoots=@('sdk/web')
  if(-not $SdkOnly){$webRoots+=if($generated){@('web')}else{@('templates/desktop-app/web','templates/modern-app/web','demos/workbench/web')}}
  foreach($relative in $webRoots){
    $web=Join-Path $root $relative
    Write-Output "Building Web package: $relative"
    $ErrorActionPreference='Continue'
    & $npm.Source --prefix $web ci --no-audit --no-fund *> (Join-Path $output ($relative.Replace('/','-')+'-install.log'))
    $ErrorActionPreference='Stop'
    if($LASTEXITCODE -ne 0){throw "npm ci failed for $relative; inspect $output"}
    $ErrorActionPreference='Continue'
    & $npm.Source --prefix $web run build *> (Join-Path $output ($relative.Replace('/','-')+'-build.log'))
    $ErrorActionPreference='Stop'
    if($LASTEXITCODE -ne 0){throw "Web build failed for $relative; inspect $output"}
  }
}
& $node.Source $evidence $(if($SkipWeb){'verify-web'}else{'record-web'}) @evidenceArgs
if($LASTEXITCODE -ne 0){throw 'Web source/output identity verification failed'}
$optional=@('-DSOUI_BUILD_WEBVIEW2=OFF')
if($WithWebView2){
  $webview2Package=& (Join-Path $PSScriptRoot 'internal/Restore-WebView2.ps1')
  $optional=@('-DSOUI_BUILD_WEBVIEW2=ON',"-DSOUI_WEBVIEW2_SDK_ROOT=$webview2Package")
}
& $cmake.Source @configure @optional
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $cmake.Source --build $output --config $Configuration --parallel 2 *> (Join-Path $output "native-$Configuration.log")
if($LASTEXITCODE -ne 0){throw "Native build failed; inspect $output/native-$Configuration.log"}
if($WithWebView2){& (Join-Path $PSScriptRoot 'internal/Restore-WebView2.ps1') | Out-Null}
if($Install){
  & $cmake.Source --install $output --config $Configuration --prefix (Join-Path $root "out/install/$Architecture")
  if($LASTEXITCODE -ne 0){throw 'SDK install failed'}
}
& $node.Source $evidence finish @evidenceArgs
if($LASTEXITCODE -ne 0){throw 'Build payload recording failed'}
Write-Output "Build passed: $output / $Configuration"
