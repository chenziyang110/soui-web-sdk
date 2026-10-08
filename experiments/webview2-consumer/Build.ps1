[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [switch]$SkipSdkBuild
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$suite=Join-Path $root ('out/webview2/'+[guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $suite -Force | Out-Null
$evidence=Join-Path $PSScriptRoot 'evidence.cjs'
function Invoke-RecordedCommand([string]$Executable,[string[]]$Arguments,[string]$Log) {
  $ErrorActionPreference='Continue'
  & $Executable @Arguments *> $Log
  $code=$LASTEXITCODE
  $ErrorActionPreference='Stop'
  if($code -ne 0){throw "Experimental build command failed ($code); retained log: $Log"}
}
try {
  $package=& (Join-Path $PSScriptRoot 'Restore.ps1')
  if(-not $SkipSdkBuild){
    Invoke-RecordedCommand 'powershell.exe' @('-NoProfile','-ExecutionPolicy','Bypass','-File',
      (Join-Path $root 'tools/Build.ps1'),'-Architecture',$Architecture,'-Configuration',$Configuration,
      '-SdkOnly','-WithWebView2','-Install') (Join-Path $suite 'sdk-build.log')
  }
  # prepare verifies current SDK-only source/build identity even when building is skipped.
  Invoke-RecordedCommand 'node.exe' @($evidence,'prepare',$suite,$Architecture,$Configuration,[string]$package) (Join-Path $suite 'prepare.log')
  Invoke-RecordedCommand 'cmake.exe' @('--install',(Join-Path $root "out/sdk-$Architecture"),
    '--config',$Configuration,'--prefix',(Join-Path $suite 'install')) (Join-Path $suite 'install.log')
  $platform=if($Architecture -eq 'x86'){'Win32'}else{'x64'}
  Invoke-RecordedCommand 'cmake.exe' @('-S',(Join-Path $suite 'source'),'-B',(Join-Path $suite 'native'),
    '-G','Visual Studio 17 2022','-A',$platform,'-DCMAKE_SYSTEM_VERSION=10.0.19041.0',
    "-DSouiSdk_DIR=$suite/install/lib/cmake/SouiSdk", "-DSOUI_WEBVIEW2_SDK_ROOT=$suite/dependency",
    "-DSOUI_WEBVIEW2_WEB_ROOT=$suite/web", "-DSOUI_JSON_INCLUDE_ROOT=$suite/json") (Join-Path $suite 'configure.log')
  Invoke-RecordedCommand 'cmake.exe' @('--build',(Join-Path $suite 'native'),'--config',$Configuration,
    '--parallel','2') (Join-Path $suite 'compile.log')
  Invoke-RecordedCommand 'node.exe' @($evidence,'finish',$suite) (Join-Path $suite 'finish.log')
  # A prepared build is not a successful runtime test. Test.ps1 accepts this directory.
  Write-Output $suite
} catch {
  Write-Error "Experimental build failed; evidence remains in $suite" -ErrorAction Continue
  throw
}
