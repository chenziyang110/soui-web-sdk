[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependency = Get-Content -Raw -LiteralPath (Join-Path $root 'sdk/renderers/webview2/dependency.json') | ConvertFrom-Json
$cache = Join-Path $root ('out/webview2-deps/' + $dependency.version)
$archive = Join-Path $cache ('microsoft.web.webview2.' + $dependency.version + '.nupkg')
$package = Join-Path $cache 'package'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
  $temporary = $archive + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
  try {
    Invoke-WebRequest -UseBasicParsing -Uri $dependency.url -OutFile $temporary
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dependency.sha256) {
      throw 'Downloaded WebView2 SDK does not match the pinned SHA-256.'
    }
    Move-Item -LiteralPath $temporary -Destination $archive
  } finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
  }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dependency.sha256) {
  throw 'Cached WebView2 SDK does not match the pinned SHA-256.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (-not (Test-Path -LiteralPath $package)) {
  [IO.Compression.ZipFile]::ExtractToDirectory($archive, $package)
}
# Verify every file consumed by this optional component against the authenticated archive.
$required = @('build/native/include/WebView2.h', 'build/native/include/WebView2EnvironmentOptions.h', 'build/native/x86/WebView2LoaderStatic.lib',
  'build/native/x64/WebView2LoaderStatic.lib', 'LICENSE.txt', 'NOTICE.txt')
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
  foreach ($relative in $required) {
    $entry = $zip.GetEntry($relative)
    if ($null -eq $entry) { throw "Pinned package is missing $relative" }
    $stream = $entry.Open()
    $hash = [Security.Cryptography.SHA256]::Create()
    try { $expected = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
    $actual = (Get-FileHash -LiteralPath (Join-Path $package $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Extracted dependency changed: $relative" }
  }
} finally { $zip.Dispose() }
Write-Output $package
