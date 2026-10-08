param([Parameter(Mandatory=$true)][string]$Transfer)
# Windows 7 inbox PowerShell 2.0 / .NET-compatible environment and probe-loader check.
$ErrorActionPreference='Stop'
$Transfer=[IO.Path]::GetFullPath($Transfer)
$Results='C:\soui-v02-current-preflight-results'
if(Test-Path -LiteralPath $Results){Write-Output 'Refuse to replace earlier guest results';exit 10}
[IO.Directory]::CreateDirectory($Results) | Out-Null
$script:exitCode=1
function Write-New([string]$Name,[string]$Text){
  $file=Join-Path $Results $Name
  $stream=New-Object IO.FileStream($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$bytes=[Text.Encoding]::UTF8.GetBytes($Text+"`r`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Close()}
}
function Get-Sha256([string]$File){
  $h=[Security.Cryptography.SHA256]::Create();$s=[IO.File]::OpenRead($File)
  try{return [BitConverter]::ToString($h.ComputeHash($s)).Replace('-','').ToLowerInvariant()}
  finally{$s.Close();$h.Clear()}
}
function Read-Layout([string]$Key){
  $items=New-Object 'System.Collections.Generic.List[string]'
  if(Test-Path -LiteralPath $Key){
    $value=Get-ItemProperty -LiteralPath $Key
    foreach($property in $value.PSObject.Properties){
      if($property.Name -notmatch '^PS'){ $items.Add($property.Name+'='+[string]$property.Value) }
    }
  }
  return [string]::Join("`r`n",$items.ToArray())
}
try{
  $os=Get-WmiObject Win32_OperatingSystem
  Write-New 'guest-identity.txt' ('Version='+$os.Version+"`r`nServicePackMajorVersion="+$os.ServicePackMajorVersion+"`r`nOSArchitecture="+$os.OSArchitecture+"`r`nPointerBytes="+[IntPtr]::Size+"`r`nPowerShellMajor="+$PSVersionTable.PSVersion.Major+"`r`nCaption="+$os.Caption)
  if($os.Version -ne '6.1.7601' -or $os.ServicePackMajorVersion -ne 1 -or [IntPtr]::Size -ne 4){throw 'Guest must be Win7 SP1 x86'}
  $manifest=Join-Path $Transfer 'transfer-files.tsv'
  $lines=@(Get-Content -LiteralPath $manifest -Encoding UTF8 | Where-Object {$_ -ne ''})
  $files=@(Get-ChildItem -LiteralPath $Transfer -Force)
  if(@($files | Where-Object {$_.PSIsContainer}).Count -ne 0 -or $files.Count -ne $lines.Count+1){throw 'Transfer file set differs'}
  $seen=@{};$observed=New-Object 'System.Collections.Generic.List[string]'
  foreach($line in $lines){
    $parts=$line.Split([char]9)
    if($parts.Count -ne 3 -or $parts[0] -cnotmatch '^[a-f0-9]{64}$' -or $parts[1] -notmatch '^\d+$' -or $parts[2] -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or $seen.ContainsKey($parts[2])){throw 'Invalid transfer manifest'}
    $seen[$parts[2]]=$true
    $file=Get-Item -LiteralPath (Join-Path $Transfer $parts[2])
    $hash=Get-Sha256 $file.FullName
    if($file.Length -ne [long]$parts[1] -or $hash -cne $parts[0]){throw ('Transfer hash differs: '+$parts[2])}
    $observed.Add($hash+"`t"+$file.Length+"`t"+$parts[2])
  }
  Write-New 'transfer-observed.tsv' ([string]::Join("`r`n",$observed.ToArray()))
  $video=@(Get-WmiObject Win32_VideoController)
  $modes=New-Object 'System.Collections.Generic.List[string]'
  foreach($item in $video){$modes.Add('Name='+$item.Name+';Width='+$item.CurrentHorizontalResolution+';Height='+$item.CurrentVerticalResolution+';Mode='+$item.VideoModeDescription)}
  Write-New 'display.txt' ([string]::Join("`r`n",$modes.ToArray()))
  Write-New 'input-layouts.txt' ('Culture='+[Globalization.CultureInfo]::CurrentCulture.Name+"`r`nPreload="+(Read-Layout 'HKCU:\Keyboard Layout\Preload')+"`r`nSubstitutes="+(Read-Layout 'HKCU:\Keyboard Layout\Substitutes'))
  $probe=Join-Path $Transfer 'V02InputProbe.exe'
  $psi=New-Object Diagnostics.ProcessStartInfo
  $psi.FileName=$probe;$psi.WorkingDirectory=$Transfer;$psi.UseShellExecute=$false
  $psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
  $process=New-Object Diagnostics.Process;$process.StartInfo=$psi
  try{
    if(-not $process.Start()){throw 'Probe loader did not start'}
    $out=$process.StandardOutput.ReadToEnd();$err=$process.StandardError.ReadToEnd()
    if(-not $process.WaitForExit(5000)){$process.Kill();throw 'Probe loader did not exit'}
    Write-New 'probe-loader-stdout.log' $out
    Write-New 'probe-loader-stderr.log' $err
    Write-New 'probe-loader-exitcode.txt' ([string]$process.ExitCode)
    if($process.ExitCode -ne 1 -or $err -notmatch 'usage: V02InputProbe'){throw 'Probe did not reach its argument validation'}
  }finally{$process.Close()}
  Write-New 'preflight-result.txt' 'status=guest-identity-transfer-and-probe-loader-verified;app-not-launched;v02-not-tested;release-ready=false'
  $script:exitCode=0
}catch{
  Write-New 'preflight-error.txt' $_.ToString()
  Write-New 'preflight-result.txt' 'status=failed;app-not-launched;v02-not-tested;release-ready=false'
}finally{
  Write-New 'preflight-exitcode.txt' ([string]$script:exitCode)
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Transfer 'export-serial.ps1') -Results $Results
  if($LASTEXITCODE -ne 0){$script:exitCode=20}
}
exit $script:exitCode
