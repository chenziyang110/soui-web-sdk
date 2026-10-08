param([Parameter(Mandatory=$true)][string]$Results)
$ErrorActionPreference='Stop'
$port=New-Object System.IO.Ports.SerialPort
$port.PortName='COM1'
$port.BaudRate=115200
$port.Parity='None'
$port.DataBits=8
$port.StopBits='One'
$port.Handshake='None'
$port.NewLine="`n"
$port.WriteTimeout=15000
$failed=$false
try {
  $port.Open()
  $port.WriteLine('SOUI_SERIAL_V1')
  $files=@(Get-ChildItem -LiteralPath $Results -Force | Where-Object {-not $_.PSIsContainer} | Sort-Object Name)
  if($files.Count -gt 128){throw 'Too many result files'}
  foreach($file in $files) {
    if($file.Name -cnotmatch '^[a-z0-9][a-z0-9._-]*$' -or $file.Length -gt 1048576){throw 'Invalid result file'}
    $bytes=[IO.File]::ReadAllBytes($file.FullName)
    $hasher=[Security.Cryptography.SHA256]::Create()
    try {$hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
    finally {$hasher.Clear()}
    $port.WriteLine('FILE|'+$file.Name+'|'+$bytes.Length+'|'+$hash)
    $port.WriteLine([Convert]::ToBase64String($bytes))
  }
  $port.WriteLine('SOUI_SERIAL_END')
  Write-Output ('Exported '+$files.Count+' raw files over COM1')
} catch {Write-Output ('Serial export failed: '+$_.ToString());$failed=$true}
finally {if($port.IsOpen){$port.Close()}}
if($failed){exit 1}
exit 0
