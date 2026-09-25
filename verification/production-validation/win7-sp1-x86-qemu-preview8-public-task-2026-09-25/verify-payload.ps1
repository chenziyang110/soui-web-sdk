param([Parameter(Mandatory=$true)][string]$Target,[Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop'
try {
  $root=[IO.Path]::GetFullPath($Target)
  $lines=@([IO.File]::ReadAllLines([IO.Path]::GetFullPath($Manifest),[Text.Encoding]::ASCII))
  if($lines.Count -ne 76){throw ('Expected 76 payload hashes; got '+$lines.Count)}
  $actual=@(Get-ChildItem -LiteralPath $root -Recurse | Where-Object { -not $_.PSIsContainer })
  if($actual.Count -ne $lines.Count){throw ('Payload file count '+$actual.Count)}
  $seen=@{}
  foreach($line in $lines){
    $parts=$line.Split('|')
    if($parts.Length -ne 3 -or $parts[0] -cnotmatch '^[A-Za-z0-9_.\/-]+$' -or
       $parts[0].Contains('..') -or $parts[1] -notmatch '^[0-9]+$' -or
       $parts[2] -cnotmatch '^[A-F0-9]{64}$'){throw 'Invalid payload manifest line'}
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
  Write-Output 'Payload files=76,hashes=matched'
  exit 0
} catch {
  Write-Output ('Verification failed: '+$_.ToString())
  exit 1
}
