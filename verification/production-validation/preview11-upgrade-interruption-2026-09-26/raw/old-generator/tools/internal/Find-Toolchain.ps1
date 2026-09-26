function Get-SouiToolchain {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Desktop tools, ATL and Windows SDK 10.0.19041.0.' }
  $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
  if (-not $installation) { throw 'Visual Studio 2022 C++ tools were not found.' }
  return [pscustomobject]@{ Root=$installation; DevCmd=(Join-Path $installation 'Common7/Tools/VsDevCmd.bat') }
}
