$ErrorActionPreference='Stop'
$root='D:\SOUI-SDK-QEMU-validation'
$launch=Join-Path $root 'preview6-apps-qemu-launch-20260925.json'
$marker=Join-Path $root 'preview6-apps-key-send-20260925.json'
if(-not(Test-Path -LiteralPath $launch -PathType Leaf)){throw 'The frozen preview.6 guest was not launched'}
if(Test-Path -LiteralPath $marker){throw 'Guest command was already attempted; inspect the serial output before retrying'}
$launched=Get-Content -LiteralPath $launch -Raw | ConvertFrom-Json
$process=Get-Process -Id $launched.pid -ErrorAction Stop
if($process.ProcessName -ne 'qemu-system-i386'){throw 'The recorded process is not the QEMU guest'}
$startDifference=([DateTimeOffset]$launched.startedAt)-[DateTimeOffset]::new($process.StartTime.ToUniversalTime())
if([Math]::Abs($startDifference.TotalSeconds) -gt 10){throw 'The QEMU PID no longer matches the recorded launch'}
$client=[Net.Sockets.TcpClient]::new('127.0.0.1',4452)
try {
  $markerFile=[IO.File]::Open($marker,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try {
    $record=[ordered]@{pid=$process.Id;attemptedAt=[DateTimeOffset]::UtcNow.ToString('o');
      command='d:\run-preview6.cmd';status='attempted';releaseReady=$false}
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Depth 4))
    $markerFile.Write($bytes,0,$bytes.Length)
  } finally {$markerFile.Dispose()}
  $stream=$client.GetStream()
  $writer=[IO.StreamWriter]::new($stream,[Text.Encoding]::ASCII)
  $writer.AutoFlush=$true
  $writer.WriteLine('sendkey meta_l-r')
  Start-Sleep -Milliseconds 900
  foreach($character in 'd:\run-preview6.cmd'.ToCharArray()){
    $key=if($character -eq ':'){'shift-semicolon'} elseif($character -eq [char]92){'backslash'}
      elseif($character -eq '-'){'minus'} elseif($character -eq '.'){'dot'} else{[string]$character}
    $writer.WriteLine('sendkey '+$key)
    Start-Sleep -Milliseconds 90
  }
  $writer.WriteLine('sendkey ret')
  $record.status='sent'
  $record.sentAt=[DateTimeOffset]::UtcNow.ToString('o')
  [IO.File]::WriteAllText($marker,($record | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
} finally {$client.Dispose()}
Write-Output 'Preview.6 app command sent to the verified Win7 desktop'
