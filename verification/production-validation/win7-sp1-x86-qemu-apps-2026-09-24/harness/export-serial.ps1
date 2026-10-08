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
$failed=$false
try {
  $port.Open()
  $port.WriteLine('SOUI_SERIAL_V1')
  $files=Get-ChildItem -LiteralPath $Results | Where-Object { -not $_.PSIsContainer } | Sort-Object Name
  foreach($file in $files){
    if($file.Length -gt 1048576){throw "Result file too large: $($file.Name)"}
    $bytes=[IO.File]::ReadAllBytes($file.FullName)
    $hasher=New-Object Security.Cryptography.SHA256Managed
    $hash=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')
    $port.WriteLine(('FILE|'+$file.Name+'|'+$bytes.Length+'|'+$hash))
    $port.WriteLine([Convert]::ToBase64String($bytes))
  }
  $port.WriteLine('SOUI_SERIAL_END')
  Write-Output ('Exported '+$files.Count+' files over COM1')
} catch {
  Write-Host ('Serial export failed: '+$_.ToString())
  $failed=$true
} finally {if($port.IsOpen){$port.Close()}}
if($failed){exit 1}
