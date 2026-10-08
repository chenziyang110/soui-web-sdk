$client=[Net.Sockets.TcpClient]::new('127.0.0.1',4445)
try {
  $stream=$client.GetStream()
  $writer=[IO.StreamWriter]::new($stream,[Text.Encoding]::ASCII)
  $writer.AutoFlush=$true
  $writer.WriteLine('sendkey meta_l-r')
  Start-Sleep -Milliseconds 900
  foreach($character in 'd:\run-preview5-mixed.cmd'.ToCharArray()){
    $key=if($character -eq ':'){'shift-semicolon'} elseif($character -eq [char]92){'backslash'}
      elseif($character -eq '-'){'minus'} elseif($character -eq '.'){'dot'} else{[string]$character}
    $writer.WriteLine('sendkey '+$key)
    Start-Sleep -Milliseconds 90
  }
  $writer.WriteLine('sendkey ret')
} finally {$client.Dispose()}
