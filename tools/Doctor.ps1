[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,[string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$externalPaths=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe)
if($NativeToolchain -eq 'LLVM22MSVC142'){
  if(@($externalPaths|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'LLVM22MSVC142 requires all six explicit tool paths.'}
  . (Join-Path $PSScriptRoot 'internal/Find-LlvmToolchain.ps1')
  $llvm=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot `
    -WindowsSdkRoot $WindowsSdkRoot -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe `
    -ToolIdentityBridge (Join-Path $PSScriptRoot 'internal/Invoke-ToolFileIdentity.ps1') `
    -ToolIdentityHelper (Join-Path $PSScriptRoot 'internal/Tool-FileIdentity.ps1')
  [ordered]@{nativeToolchain=$NativeToolchain;discovery=$llvm;compilerVerified=$false;buildPassed=$false;releaseReady=$false}|ConvertTo-Json -Depth 20
  return
}
if(@($externalPaths|Where-Object{$_}).Count){throw 'LLVM paths require explicit NativeToolchain LLVM22MSVC142.'}
$vs=Get-SouiToolchain
$node=Get-Command node.exe -ErrorAction Stop
$cmake=Get-Command cmake.exe -ErrorAction Stop
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$nodeText=(& $node.Source --version).Trim()
$nodeVersion=[version]($nodeText.TrimStart('v').Split('-')[0])
$nodeSupported=(($nodeVersion.Major -eq 20 -and $nodeVersion -ge [version]'20.19.0') -or $nodeVersion -ge [version]'22.12.0')
$checks=[ordered]@{node=$nodeText;nodeSupported=$nodeSupported;cmake=(& $cmake.Source --version | Select-Object -First 1);visualStudio=$vs.Root;
  msvcVersion=$vs.MsvcVersion;hostTools=$vs.HostArchitecture;
  windowsSdk19041=(Test-Path -LiteralPath (Join-Path $sdk 'Include/10.0.19041.0/um/windows.h'));
  manifestTool=(Test-Path -LiteralPath (Join-Path $sdk 'bin/10.0.19041.0/x64/mt.exe'));
  atl=(Test-Path -LiteralPath (Join-Path $vs.MsvcRoot 'atlmfc/include/atlbase.h') -PathType Leaf);
  miniblinkSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/miniblink/build/miniblink/miniblink.vcxproj'));
  souiSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/soui/SOUI/soui.vcxproj'))}
$checks|ConvertTo-Json
if(-not $checks.nodeSupported){throw 'Node.js 20.19 LTS or 22.12+ is required by the Web build toolchain.'}
if(-not $checks.windowsSdk19041 -or -not $checks.manifestTool -or -not $checks.atl){throw 'Required C++/ATL/Windows SDK components are missing.'}
