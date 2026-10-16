function Get-SouiToolchain {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Desktop tools, ATL and Windows SDK 10.0.19041.0.' }
  $installations = @(& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
  if ($LASTEXITCODE -ne 0) { throw 'Visual Studio 2022 discovery failed.' }
  $installation = $installations | Select-Object -First 1
  if (-not $installation) { throw 'Visual Studio 2022 C++ tools were not found.' }
  $msvcVersion = '14.44.35207'
  $msvcRoot = Join-Path $installation ('VC/Tools/MSVC/'+$msvcVersion)
  $devCmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
  foreach ($required in @($devCmd,
      (Join-Path $msvcRoot 'bin/Hostx64/x86/cl.exe'), (Join-Path $msvcRoot 'bin/Hostx64/x86/link.exe'),
      (Join-Path $msvcRoot 'bin/Hostx64/x64/cl.exe'), (Join-Path $msvcRoot 'bin/Hostx64/x64/link.exe'),
      (Join-Path $msvcRoot 'atlmfc/include/atlbase.h'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "MSVC $msvcVersion Hostx64 x86/x64 tools and matching ATL are required; missing: $required"
    }
  }
  return [pscustomobject]@{ Root=$installation; DevCmd=$devCmd; MsvcRoot=$msvcRoot;
    MsvcVersion=$msvcVersion; Toolset='v143'; HostArchitecture='x64' }
}
