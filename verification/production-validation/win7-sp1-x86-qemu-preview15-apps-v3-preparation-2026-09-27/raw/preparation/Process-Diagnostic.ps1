# Read-only, once after 55 seconds; the original 60-second application deadline remains.
# A process/thread snapshot records observed state, not a symbolic stack or a passing run.
function Write-ProcessDiagnostic([string]$Name,$Process,$Watch,[string]$SmokeLog) {
  $rows=New-Object 'System.Collections.Generic.List[string]'
  $rows.Add('DiagnosticVersion=1')
  $rows.Add('ElapsedMs='+$Watch.ElapsedMilliseconds)
  $rows.Add('PID='+$Process.Id)
  try {
    $Process.Refresh()
    $rows.Add('StartUtc='+$Process.StartTime.ToUniversalTime().ToString('o'))
    $rows.Add('CpuMs='+$Process.TotalProcessorTime.TotalMilliseconds)
    $rows.Add('HandleCount='+$Process.HandleCount)
    $rows.Add('WorkingSetBytes='+$Process.WorkingSet64)
    $rows.Add('PrivateBytes='+$Process.PrivateMemorySize64)
    $threads=@($Process.Threads)
    $rows.Add('ThreadCount='+$threads.Count)
    $count=0
    foreach($thread in $threads) {
      if($count -ge 128){$rows.Add('ThreadSnapshotTruncated=True');break}
      try {
        $state=$thread.ThreadState
        $reason='';if($state -eq [Diagnostics.ThreadState]::Wait){$reason=[string]$thread.WaitReason}
        $rows.Add('Thread='+$thread.Id+';State='+$state+';WaitReason='+$reason+';CpuMs='+$thread.TotalProcessorTime.TotalMilliseconds)
      } catch {$rows.Add('ThreadReadError='+$_.ToString())}
      $count++
    }
    if(Test-Path -LiteralPath $SmokeLog) {
      $lines=@(Get-Content -LiteralPath $SmokeLog | Select-Object -Last 128)
      foreach($line in $lines){$rows.Add('Smoke='+$line)}
    }
  } catch {$rows.Add('DiagnosticError='+$_.ToString())}
  $rows.Add('CompletedElapsedMs='+$Watch.ElapsedMilliseconds)
  try {Write-New ($Name+'-process-diagnostic.txt') ([string]::Join("`r`n",$rows.ToArray()))}
  catch {$script:trace.Add('diagnostic-write-failed: '+$_.ToString())}
}
