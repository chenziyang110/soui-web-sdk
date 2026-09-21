$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$vs=Get-SouiToolchain
$node=Get-Command node.exe -ErrorAction Stop
$cmake=Get-Command cmake.exe -ErrorAction Stop
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$checks=[ordered]@{node=(& $node.Source --version);cmake=(& $cmake.Source --version | Select-Object -First 1);visualStudio=$vs.Root;
  windowsSdk19041=(Test-Path -LiteralPath (Join-Path $sdk 'Include/10.0.19041.0/um/windows.h'));
  manifestTool=(Test-Path -LiteralPath (Join-Path $sdk 'bin/10.0.19041.0/x64/mt.exe'));
  atl=(@(Get-ChildItem -LiteralPath (Join-Path $vs.Root 'VC/Tools/MSVC') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'atlmfc/include/atlbase.h')}).Count -gt 0);
  miniblinkSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/miniblink/build/miniblink/miniblink.vcxproj'));
  souiSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/soui/SOUI/soui.vcxproj'))}
$checks|ConvertTo-Json
if(-not $checks.windowsSdk19041 -or -not $checks.manifestTool -or -not $checks.atl){throw 'Required C++/ATL/Windows SDK components are missing.'}
