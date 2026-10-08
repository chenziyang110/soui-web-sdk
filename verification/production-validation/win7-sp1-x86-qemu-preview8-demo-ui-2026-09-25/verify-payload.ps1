param([Parameter(Mandatory=$true)][string]$Target,[Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop'
try {
  $root=[IO.Path]::GetFullPath($Target)
  $lines=@([IO.File]::ReadAllLines([IO.Path]::GetFullPath($Manifest),[Text.Encoding]::UTF8))
  if($lines.Count -ne 475){throw ('Expected 475 payload hashes; got '+$lines.Count)}
  $actual=@(Get-ChildItem -LiteralPath $root -Recurse | Where-Object { -not $_.PSIsContainer })
  if($actual.Count -ne $lines.Count){throw ('Payload file count '+$actual.Count)}
  $seen=@{}
  foreach($line in $lines){
    $parts=$line.Split('|')
    if($parts.Length -ne 3 -or -not $parts[0] -or $parts[0].StartsWith('/') -or
       $parts[0].Contains('..') -or $parts[0].Contains(':') -or
       $parts[1] -notmatch '^[0-9]+$' -or $parts[2] -cnotmatch '^[A-F0-9]{64}$'){
      throw 'Invalid payload manifest line'
    }
    $relative=$parts[0].Replace('/',[string][char]92)
    if($seen.ContainsKey($relative)){throw ('Duplicate payload path '+$relative)}
    $seen[$relative]=$true
    $file=Join-Path $root $relative
    if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw ('Missing payload file '+$relative)}
    $bytes=[IO.File]::ReadAllBytes($file)
    if($bytes.Length -ne [long]$parts[1]){throw ('Payload size mismatch '+$relative)}
    $hasher=[Security.Cryptography.SHA256]::Create()
    try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
    finally {$hasher.Clear()}
    if($hash -cne $parts[2]){throw ('Payload hash mismatch '+$relative)}
  }
  $legal=@(Get-ChildItem -LiteralPath (Join-Path $root 'resources\legal') -Recurse |
    Where-Object { -not $_.PSIsContainer })
  if($legal.Count -ne 5){throw ('Expected five legal files; got '+$legal.Count)}
  Write-Output 'Payload files=475,hashes=matched'
  Write-Output 'Legal files=5,hashes=matched'
  exit 0
} catch {
  Write-Output ('Verification failed: '+$_.ToString())
  exit 1
}
