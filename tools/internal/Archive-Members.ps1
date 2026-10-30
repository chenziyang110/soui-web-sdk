[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Archive)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# PS5 has separate Compression and Compression.FileSystem assemblies. Only read
# metadata here; extraction and long-path file I/O belong to 7-Zip and Node.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
try {
  $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
  try {
    if ($zip.Entries.Count -gt 100000) { throw 'ZIP entry count exceeds budget.' }
    $rows = New-Object 'Collections.Generic.List[object]'
    $pathCharacters = 0L
    $rawBytes = 0L
    foreach ($entry in $zip.Entries) {
      $pathCharacters += $entry.FullName.Length
      $rawBytes += $entry.Length
      if ($entry.FullName.Length -gt 32760 -or $pathCharacters -gt 4194304 -or $rawBytes -gt 549755813888L) {
        throw 'ZIP metadata exceeds path/raw byte budget.'
      }
      # UInt32 conversion through Int64 preserves signed ExternalAttributes bits.
      $attributes = ([int64]$entry.ExternalAttributes) -band 4294967295L
      $rows.Add([ordered]@{path=$entry.FullName; bytes=$entry.Length; attributes=$attributes})
    }
    [Console]::Out.WriteLine(([ordered]@{schema='soui.zip-members/v1'; entries=@($rows.ToArray())} | ConvertTo-Json -Compress -Depth 4))
  } finally { $zip.Dispose() }
} catch {
  Write-Error -ErrorRecord $_ -ErrorAction Continue
  exit 1
}
