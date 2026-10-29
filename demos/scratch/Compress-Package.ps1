param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Archive already exists.' }
if ([IO.Path]::GetDirectoryName($sourcePath) -ne [IO.Path]::GetDirectoryName($outputPath)) {
  throw 'The package folder and ZIP must have the same parent directory.'
}
[IO.Compression.ZipFile]::CreateFromDirectory($sourcePath,$outputPath,[IO.Compression.CompressionLevel]::Optimal,$true,[Text.Encoding]::UTF8)
