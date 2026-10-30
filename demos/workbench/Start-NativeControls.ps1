[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture = 'x64',
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [string]$Miniblink = ''
)
$ErrorActionPreference = 'Stop'
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $sdkRoot "out/$Architecture/bin/$Configuration/SouiWorkbench/SouiWorkbench.exe"
$resources = Join-Path $PSScriptRoot 'native-controls'
if (!$Miniblink) {
  $demoKernel = Join-Path $sdkRoot "out/native-controls/$Architecture/node.dll"
  if (Test-Path -LiteralPath $demoKernel -PathType Leaf) { $Miniblink = $demoKernel }
}
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
  throw "Build Workbench first: tools/Build.ps1 -Architecture $Architecture -Configuration $Configuration"
}
# Visible window is intentional: this launcher is the user's interactive demo.
$demoArguments = @(
  '--native-controls', '--renderer=software', '--entry=index.html',
  ('--resources="' + $resources + '"')
)
if ($Miniblink) {
  $kernel = (Resolve-Path -LiteralPath $Miniblink).Path
  $demoArguments += ('--miniblink="' + $kernel + '"')
}
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $demoArguments
