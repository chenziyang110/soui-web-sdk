param([Parameter(Mandatory=$true)][string]$Target,
      [Parameter(Mandatory=$true)][string]$Transfer,
      [Parameter(Mandatory=$true)][string]$Results,
      [Parameter(Mandatory=$true)][ValidateSet('pre','post')][string]$Phase)
$ErrorActionPreference='Stop'
function Get-Sha256([string]$File) {
  $hasher=[Security.Cryptography.SHA256]::Create()
  $stream=[IO.File]::OpenRead($File)
  try {return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
  finally {$stream.Close();$hasher.Clear()}
}
try {
  foreach($app in @('SouiStarter','SouiWorkbench')) {
    $base=[IO.Path]::GetFullPath((Join-Path $Target $app)).TrimEnd('\')
    $files=@(Get-ChildItem -LiteralPath $base -Recurse -Force | Where-Object {-not $_.PSIsContainer})
    $manifest=@(Get-Content -LiteralPath (Join-Path $Transfer ($app+'-payload.tsv')) -Encoding UTF8 | Where-Object {$_ -ne ''})
    if($files.Count -ne $manifest.Count){throw ($app+' file set count mismatch')}
    $actual=@{}
    foreach($file in $files) {
      $relative=$file.FullName.Substring($base.Length+1).Replace('\','/')
      if($actual.ContainsKey($relative)){throw 'Duplicate payload path'}
      $actual[$relative]=$file
    }
    $seen=@{}
    $observed=New-Object 'System.Collections.Generic.List[string]'
    $legal=0
    foreach($line in $manifest) {
      $f=$line.Split([char]9)
      if($f.Count -ne 3 -or $f[0] -cnotmatch '^[a-f0-9]{64}$' -or $f[1] -notmatch '^\d+$' -or
         $f[2] -match '(^|/)\.\.?(/|$)|[:\\\x00-\x1f]' -or $f[2].StartsWith('/') -or $seen.ContainsKey($f[2])) {throw 'Invalid payload manifest'}
      $seen[$f[2]]=$true
      if(-not $actual.ContainsKey($f[2])){throw ('Missing payload file: '+$f[2])}
      $file=$actual[$f[2]]
      $hash=Get-Sha256 $file.FullName
      if($file.Length -ne [long]$f[1] -or $hash -cne $f[0]){throw ('Payload identity mismatch: '+$f[2])}
      $observed.Add($hash+"`t"+$file.Length+"`t"+$f[2])
      if($f[2].StartsWith('resources/legal/')){$legal++}
    }
    if($legal -ne 5){throw ($app+' legal count mismatch')}
    $destination=Join-Path $Results ($app.ToLowerInvariant()+'-payload-'+$Phase+'.tsv')
    if(Test-Path -LiteralPath $destination){throw 'Refuse to replace observed payload evidence'}
    [IO.File]::WriteAllLines($destination,$observed.ToArray(),[Text.Encoding]::UTF8)
    Write-Output ($app+'='+$manifest.Count+',legal='+$legal+',all-hashes=matched')
  }
  Write-Output ('Payload '+$Phase+' complete')
  exit 0
} catch {Write-Output ('Payload verification failed: '+$_.ToString());exit 1}
