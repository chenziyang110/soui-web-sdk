[CmdletBinding()]
param([string]$InputPath)
$ErrorActionPreference = 'Stop'
if (-not $InputPath) { $InputPath = Join-Path $PSScriptRoot 'serial-export.txt' }
$input=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'input.json') -Raw | ConvertFrom-Json
if($input.schema -ne 'soui.win7-x86-preview6-app-input/v1' -or
    $input.version -ne '0.3.0-preview.6' -or $input.architecture -ne 'x86' -or
    $input.configuration -ne 'Release' -or $input.releaseReady -ne $false){
  throw 'Frozen preview.6 input identity is invalid'
}
$lines = [IO.File]::ReadAllLines([IO.Path]::GetFullPath($InputPath), [Text.Encoding]::ASCII)
if ($lines.Length -lt 4 -or $lines[0] -ne 'SOUI_SERIAL_V1' -or
    $lines[-1] -ne 'SOUI_SERIAL_END' -or (($lines.Length - 2) % 2) -ne 0) {
  throw 'Serial export framing is incomplete'
}
$results = Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$seen = @{}
for ($i = 1; $i -lt $lines.Length - 1; $i += 2) {
  $fields = $lines[$i].Split('|')
  if ($fields.Length -ne 4 -or $fields[0] -ne 'FILE' -or
      $fields[1] -cnotmatch '^[a-z0-9][a-z0-9._-]*$') {
    throw "Invalid serial file header at line $($i + 1)"
  }
  $name = $fields[1]
  if ($seen.ContainsKey($name)) { throw "Duplicate serial file: $name" }
  $seen[$name] = $true
  $bytes = [Convert]::FromBase64String($lines[$i + 1])
  if ($bytes.Length -ne [int]$fields[2]) { throw "Length mismatch: $name" }
  $hasher = [Security.Cryptography.SHA256]::Create()
  try { $hash = [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-', '') }
  finally { $hasher.Dispose() }
  if ($hash -cne $fields[3]) { throw "SHA-256 mismatch: $name" }
  [IO.File]::WriteAllBytes((Join-Path $results $name), $bytes)
}

$required = @(
  '7za-sha256.txt', 'engine-sha256.txt', 'os-version.txt', 'sdk-sha256.txt',
  'starter-exe-sha256.txt', 'starter-exitcode.txt', 'starter-extract-exitcode.txt',
  'starter-extract.log', 'starter-smoke.log', 'starter-zip-sha256.txt',
  'starter-zip-test-exitcode.txt', 'starter-zip-test.log', 'verify-apps-exitcode.txt',
  'verify-apps.log', 'workbench-default-exitcode.txt', 'workbench-default-smoke.log',
  'workbench-exe-sha256.txt', 'workbench-extract-exitcode.txt', 'workbench-extract.log',
  'workbench-react-exitcode.txt', 'workbench-react-smoke.log', 'workbench-zip-sha256.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-zip-test.log'
)
if ($seen.Count -ne $required.Count) { throw "Expected $($required.Count) files; got $($seen.Count)" }
foreach ($name in $required) { if (-not $seen.ContainsKey($name)) { throw "Missing: $name" } }

$osText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $results 'os-version.txt'))).Replace([string][char]0, '')
foreach ($expected in @('Version=6.1.7601', 'ServicePackMajorVersion=1', 'OSArchitecture=32-bit')) {
  if (-not $osText.Contains($expected)) { throw "Guest identity missing: $expected" }
}
foreach ($name in @(
    'starter-zip-test-exitcode.txt', 'workbench-zip-test-exitcode.txt',
    'starter-extract-exitcode.txt', 'workbench-extract-exitcode.txt',
    'verify-apps-exitcode.txt', 'starter-exitcode.txt',
    'workbench-default-exitcode.txt', 'workbench-react-exitcode.txt')) {
  $exitCode = (Get-Content -LiteralPath (Join-Path $results $name) -Raw).Trim()
  if ($exitCode -ne '0') { throw "$name is $exitCode" }
}

$expectedHashes = @{
  '7za-sha256.txt' = $input.sevenZipSha256
  'starter-zip-sha256.txt' = $input.packages.SouiStarter.zipSha256
  'workbench-zip-sha256.txt' = $input.packages.SouiWorkbench.zipSha256
  'starter-exe-sha256.txt' = $input.packages.SouiStarter.exeSha256
  'workbench-exe-sha256.txt' = $input.packages.SouiWorkbench.exeSha256
  'engine-sha256.txt' = $input.packages.SouiWorkbench.engineSha256
  'sdk-sha256.txt' = $input.packages.SouiWorkbench.souiDllSha256
}
foreach ($name in $expectedHashes.Keys) {
  $hashLines = Get-Content -LiteralPath (Join-Path $results $name)
  $hashes = @($hashLines | Where-Object { $_ -match '^([0-9a-fA-F]{2} ){31}[0-9a-fA-F]{2}\s*$' } |
      ForEach-Object { ($_ -replace '\s', '').ToUpperInvariant() })
  if ($hashes.Count -ne 1 -or $hashes[0] -cne $expectedHashes[$name]) {
    throw "Unexpected guest SHA-256 in $name"
  }
}

foreach ($name in @('starter-zip-test.log', 'workbench-zip-test.log',
    'starter-extract.log', 'workbench-extract.log')) {
  $content = Get-Content -LiteralPath (Join-Path $results $name) -Raw
  if (-not $content.Contains('Everything is Ok')) { throw "Archive operation incomplete: $name" }
}
$verifyText = Get-Content -LiteralPath (Join-Path $results 'verify-apps.log') -Raw
foreach ($expected in @('SouiStarter=71', 'SouiWorkbench=475',
    'Chinese fixture names preserved', 'Legal files=5x2,hashes=matched')) {
  if (-not $verifyText.Contains($expected)) { throw "Payload verification missing: $expected" }
}
$starter = @(Get-Content -LiteralPath (Join-Path $results 'starter-smoke.log'))
if ($starter[0] -ne 'process-start' -or $starter -notcontains 'application-ready' -or
    $starter[-1] -ne 'host-close-complete') { throw 'Starter smoke log incomplete' }
foreach ($name in @('workbench-default-smoke.log', 'workbench-react-smoke.log')) {
  $events = @(Get-Content -LiteralPath (Join-Path $results $name))
  if ($events[0] -ne 'process-start' -or $events -notcontains 'application-ready' -or
      $events[-1] -ne 'window-destroyed' -or
      @($events | Where-Object { $_ -match '^document=ready,bridge=ready,.*probe=' }).Count -ne 1) {
    throw "Workbench smoke log incomplete: $name"
  }
}
Write-Output "Verified preview.6: $($seen.Count) serial files, Win7 SP1 x86 identity, payload and legal hashes, and all three app smokes"
