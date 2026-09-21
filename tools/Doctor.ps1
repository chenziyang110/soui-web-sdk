$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$vs=Get-SouiToolchain
$node=Get-Command node.exe -ErrorAction Stop
$cmake=Get-Command cmake.exe -ErrorAction Stop
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$nodeText=(& $node.Source --version).Trim()
$nodeVersion=[version]($nodeText.TrimStart('v').Split('-')[0])
$nodeSupported=(($nodeVersion.Major -eq 20 -and $nodeVersion -ge [version]'20.19.0') -or $nodeVersion -ge [version]'22.12.0')
$checks=[ordered]@{node=$nodeText;nodeSupported=$nodeSupported;cmake=(& $cmake.Source --version | Select-Object -First 1);visualStudio=$vs.Root;
  windowsSdk19041=(Test-Path -LiteralPath (Join-Path $sdk 'Include/10.0.19041.0/um/windows.h'));
  manifestTool=(Test-Path -LiteralPath (Join-Path $sdk 'bin/10.0.19041.0/x64/mt.exe'));
  atl=(@(Get-ChildItem -LiteralPath (Join-Path $vs.Root 'VC/Tools/MSVC') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'atlmfc/include/atlbase.h')}).Count -gt 0);
  miniblinkSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/miniblink/build/miniblink/miniblink.vcxproj'));
  souiSource=(Test-Path -LiteralPath (Join-Path $root 'third_party/sources/soui/SOUI/soui.vcxproj'))}
$checks|ConvertTo-Json
if(-not $checks.nodeSupported){throw 'Node.js 20.19 LTS or 22.12+ is required by the Web build toolchain.'}
if(-not $checks.windowsSdk19041 -or -not $checks.manifestTool -or -not $checks.atl){throw 'Required C++/ATL/Windows SDK components are missing.'}
