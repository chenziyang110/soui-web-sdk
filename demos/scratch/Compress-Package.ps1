param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Archive already exists.' }
if ([IO.Path]::GetDirectoryName($sourcePath) -ne [IO.Path]::GetDirectoryName($outputPath)) {
  throw 'The package folder and ZIP must have the same parent directory.'
}
# .NET Framework's CreateFromDirectory emits backslashes on some Windows
# versions. ZIP paths must use '/', including when produced by PowerShell 5.
$prefix=$sourcePath.TrimEnd('\')+'\'
$rootName=[IO.Path]::GetFileName($sourcePath.TrimEnd('\'))
$entries=@(Get-ChildItem -LiteralPath $sourcePath -Recurse -Force | Sort-Object FullName)
if($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
  throw 'Package entries must not be links.'
}
$stream=$null
$archive=$null
try {
  $stream=[IO.File]::Open($outputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false,[Text.Encoding]::UTF8)
  foreach($item in $entries) {
    if(!$item.FullName.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {throw 'Package path escaped its source.'}
    $entryName=$rootName+'/'+$item.FullName.Substring($prefix.Length).Replace('\','/')
    if($item.PSIsContainer) {
      [void]$archive.CreateEntry($entryName+'/')
      continue
    }
    $entry=$archive.CreateEntry($entryName,[IO.Compression.CompressionLevel]::Optimal)
    $sourceStream=$null
    $entryStream=$null
    try {
      $sourceStream=[IO.File]::OpenRead($item.FullName)
      $entryStream=$entry.Open()
      $sourceStream.CopyTo($entryStream)
    } finally {
      if($null-ne$entryStream){$entryStream.Dispose()}
      if($null-ne$sourceStream){$sourceStream.Dispose()}
    }
  }
} finally {
  if($null-ne$archive){$archive.Dispose()}
  if($null-ne$stream){$stream.Dispose()}
}
