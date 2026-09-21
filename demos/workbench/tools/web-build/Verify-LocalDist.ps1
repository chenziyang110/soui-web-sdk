[CmdletBinding()]
param(
  [string] $DistPath = '',
  [switch] $RequireLegacy
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
. (Join-Path $scriptRoot 'RemoteRuntimePolicy.ps1')
$webRoot = Resolve-Path (Join-Path $scriptRoot '../../web')
if ([string]::IsNullOrWhiteSpace($DistPath)) {
  $DistPath = Join-Path $webRoot 'dist'
}

if (-not (Test-Path -LiteralPath $DistPath -PathType Container)) {
  throw "Local dist directory does not exist: $DistPath"
}

$files = @(Get-ChildItem -LiteralPath $DistPath -Recurse -File)
if ($files.Count -eq 0) { throw "Local dist directory is empty: $DistPath" }
$index = Join-Path $DistPath 'index.html'
if (-not (Test-Path -LiteralPath $index -PathType Leaf)) { throw 'dist/index.html is required' }

$violations = @(Get-RemoteRuntimeDependencyViolations -Root $DistPath -Files $files)
if ($violations.Count -gt 0) {
  throw "Remote runtime dependency found in local dist: $($violations -join ', ')"
}

$manifest = Join-Path $DistPath '.vite/manifest.json'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
  $manifest = Join-Path $DistPath 'manifest.json'
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Vite manifest is required for dist integrity checks' }

if ($RequireLegacy) {
  $legacyDist = Join-Path $webRoot 'dist-legacy'
  if (-not (Test-Path -LiteralPath (Join-Path $legacyDist 'index.html') -PathType Leaf)) {
    throw 'Legacy dist/index.html is required when -RequireLegacy is used'
  }
}

Write-Output ("Local dist OK: {0} files; manifest={1}; legacyRequired={2}" -f $files.Count, $manifest, [bool]$RequireLegacy)
