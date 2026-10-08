param([Parameter(Mandatory=$true)][string]$Transfer)
# Windows 7 inbox PowerShell 2.0 / .NET 2.0-compatible guest harness.
$ErrorActionPreference='Stop'
$Target='C:\soui-v02-current-layout-explore-v2'
$Results='C:\soui-v02-current-layout-explore-v2-results'
$Transfer=[IO.Path]::GetFullPath($Transfer)
if((Test-Path -LiteralPath $Target) -or (Test-Path -LiteralPath $Results)) {
  Write-Output 'Refuse to replace prior guest target/results; use a fresh snapshot for a new attempt.'
  exit 10
}
[IO.Directory]::CreateDirectory($Results) | Out-Null
[IO.Directory]::CreateDirectory($Target) | Out-Null
$script:exitCode=1
$script:trace=New-Object 'System.Collections.Generic.List[string]'
function Write-New([string]$Name,[string]$Text) {
  $file=Join-Path $Results $Name
  $stream=New-Object IO.FileStream($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try {$bytes=[Text.Encoding]::UTF8.GetBytes($Text+"`r`n");$stream.Write($bytes,0,$bytes.Length)} finally {$stream.Close()}
}
function Get-Sha256([string]$File) {
  $h=[Security.Cryptography.SHA256]::Create();$s=[IO.File]::OpenRead($File)
  try {return [BitConverter]::ToString($h.ComputeHash($s)).Replace('-','').ToLowerInvariant()}
  finally {$s.Close();$h.Clear()}
}
function To-Base64([string]$Text) {return [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))}
function Write-TransferDiagnostics($Files,$FilteredLines) {
  # Capture evidence before parsing manifest fields or applying the unchanged set guard.
  $manifestPath=Join-Path $Transfer 'transfer-files.tsv'
  $manifestBytes=[IO.File]::ReadAllBytes($manifestPath)
  $rawStream=New-Object IO.FileStream((Join-Path $Results 'transfer-manifest-raw.tsv'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try {$rawStream.Write($manifestBytes,0,$manifestBytes.Length)} finally {$rawStream.Close()}
  $rawLines=@(Get-Content -LiteralPath $manifestPath -Encoding UTF8)
  $entries=New-Object 'System.Collections.Generic.List[string]'
  $index=0
  foreach($entry in $Files) {
    $length='';if(-not $entry.PSIsContainer){$length=[string]$entry.Length}
    $entries.Add([string]$index+"`t"+(To-Base64 $entry.Name)+"`t"+(To-Base64 $entry.FullName)+"`t"+(To-Base64 ([string]$entry.Attributes))+"`t"+([string]$entry.PSIsContainer)+"`t"+$length+"`t"+(To-Base64 ($entry.GetType().FullName)))
    $index++
  }
  Write-New 'transfer-entries.tsv' ([string]::Join("`r`n",$entries.ToArray()))
  $lines=New-Object 'System.Collections.Generic.List[string]'
  $index=0
  foreach($line in $rawLines) {
    $lines.Add([string]$index+"`t"+$line.Length+"`t"+$line.Split([char]9).Count+"`t"+(To-Base64 $line))
    $index++
  }
  Write-New 'transfer-lines.tsv' ([string]::Join("`r`n",$lines.ToArray()))
  $directoryCount=@($Files | Where-Object {$_.PSIsContainer}).Count
  $prefixLength=[Math]::Min(64,$manifestBytes.Length)
  $prefix=[BitConverter]::ToString($manifestBytes,0,$prefixLength)
  Write-New 'transfer-diagnostics.txt' ('DiagnosticsVersion=2'+"`r`nTransfer="+$Transfer+"`r`nPowerShellVersion="+$PSVersionTable.PSVersion+"`r`nClrVersion="+[Environment]::Version+"`r`nManifestBytes="+$manifestBytes.Length+"`r`nManifestSha256="+(Get-Sha256 $manifestPath)+"`r`nManifestPrefixHex="+$prefix+"`r`nRawLineCount="+$rawLines.Count+"`r`nFilteredLineCount="+$FilteredLines.Count+"`r`nEnumeratedCount="+$Files.Count+"`r`nDirectoryCount="+$directoryCount+"`r`nExpectedCount="+($FilteredLines.Count+1)+"`r`nFilesType="+$Files.GetType().FullName+"`r`nFilteredLinesType="+$FilteredLines.GetType().FullName)
}
function Run-Archive([string]$Name,[string[]]$Arguments) {
  & (Join-Path $Transfer '7za.exe') @Arguments > (Join-Path $Results ($Name+'.log')) 2>&1
  $code=$LASTEXITCODE
  Write-New ($Name+'-exitcode.txt') ([string]$code)
  if($code -ne 0){throw ($Name+' failed: '+$code)}
}
function Verify-Payload([string]$Phase) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Transfer 'verify-apps.ps1') -Target $Target -Transfer $Transfer -Results $Results -Phase $Phase > (Join-Path $Results ('payload-'+$Phase+'.log')) 2>&1
  $code=$LASTEXITCODE
  Write-New ('payload-'+$Phase+'-exitcode.txt') ([string]$code)
  if($code -ne 0){throw ('Payload '+$Phase+' verification failed')}
}
function Run-Wheel([int]$PidValue,[string]$Creation,[int]$Index) {
  $output=Join-Path $Results ('wheel-'+$Index+'.json')
  $psi=New-Object Diagnostics.ProcessStartInfo
  $psi.FileName=Join-Path $Transfer 'V02InputProbe.exe'
  $psi.Arguments=([string]$PidValue)+' '+$Creation+' wheel 900,450 "'+$output+'"'
  $psi.WorkingDirectory=$Transfer
  $psi.UseShellExecute=$false
  $psi.CreateNoWindow=$true
  $psi.RedirectStandardOutput=$true
  $psi.RedirectStandardError=$true
  $probe=New-Object Diagnostics.Process
  $probe.StartInfo=$psi
  $code=-1;$stdout='';$stderr='';$timedOut=$false
  try {
    if(-not $probe.Start()){throw 'Cannot start wheel probe'}
    if(-not $probe.WaitForExit(5000)){$timedOut=$true;$probe.Kill();$probe.WaitForExit()}
    $code=$probe.ExitCode
    $stdout=$probe.StandardOutput.ReadToEnd()
    $stderr=$probe.StandardError.ReadToEnd()
  } finally {$probe.Close()}
  Write-New ('wheel-'+$Index+'-process.txt') ('ExitCode='+$code+"`r`nTimedOut="+$timedOut+"`r`nStdoutBase64="+(To-Base64 $stdout)+"`r`nStderrBase64="+(To-Base64 $stderr))
  if($code -ne 0 -or $timedOut -or -not (Test-Path -LiteralPath $output)){
    throw ('Wheel '+$Index+' failed or timed out: '+$code)
  }
}
# A process/thread snapshot, when used, records observed state rather than a symbolic stack.
# This new layout exploration has its own 120-second application deadline and explicit screenshots before/after twelve OS wheel inputs.
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

function Run-App([string]$Name,[string]$App,[string]$Arguments,[string]$LogName) {
  $working=Join-Path $Target $App
  $exe=Join-Path $working ($App+'.exe')
  $log=Join-Path $working $LogName
  if(Test-Path -LiteralPath $log){throw ('Unexpected pre-existing application log: '+$log)}
  $psi=New-Object Diagnostics.ProcessStartInfo
  $psi.FileName=$exe;$psi.Arguments=$Arguments;$psi.WorkingDirectory=$working
  $psi.UseShellExecute=$false
  # Inherited experiment flags must not silently change the three frozen modes.
  $keys=@($psi.EnvironmentVariables.Keys)
  foreach($key in $keys){if($key.StartsWith('SOUI_')){$psi.EnvironmentVariables.Remove($key)}}
  $process=New-Object Diagnostics.Process
  $process.StartInfo=$psi
  $timedOut=$false;$stillRunning=$false;$code=-1;$pidValue=0;$start='';$inspected=$false;$closePosted=$false;$startedProcess=$false
  $nextInspectMs=10000;$nextCloseMs=90000
  $probeSamples=New-Object 'System.Collections.Generic.List[string]'
  $closeSamples=New-Object 'System.Collections.Generic.List[string]'
  try {
    if(-not $process.Start()){throw ('Cannot start '+$Name)}
    $startedProcess=$true
    $pidValue=$process.Id;$start=$process.StartTime.ToUniversalTime().ToString('o')
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while(-not $process.WaitForExit(250)) {
      if(-not $inspected -and $watch.ElapsedMilliseconds -ge $nextInspectMs -and $watch.ElapsedMilliseconds -lt 55000){
        $nextInspectMs+=5000
        $inspectOutput=Join-Path $Results 'window-inspect.json'
        $inspectPsi=New-Object Diagnostics.ProcessStartInfo
        $inspectPsi.FileName=Join-Path $Transfer 'V02InputProbe.exe'
        $inspectPsi.Arguments=([string]$pidValue)+' 0 inspect - "'+$inspectOutput+'"'
        $inspectPsi.WorkingDirectory=$Transfer
        $inspectPsi.UseShellExecute=$false
        $inspectPsi.CreateNoWindow=$true
        $inspectPsi.RedirectStandardOutput=$true
        $inspectPsi.RedirectStandardError=$true
        $probe=New-Object Diagnostics.Process
        $probe.StartInfo=$inspectPsi
        $inspectCode=-1;$inspectStdout='';$inspectStderr='';$probeTimedOut=$false
        try {
          if(-not $probe.Start()){throw 'Cannot start visible window probe'}
          if(-not $probe.WaitForExit(5000)){$probeTimedOut=$true;$probe.Kill();$probe.WaitForExit()}
          $inspectCode=$probe.ExitCode
          $inspectStdout=$probe.StandardOutput.ReadToEnd()
          $inspectStderr=$probe.StandardError.ReadToEnd()
        } finally {$probe.Close()}
        $probeSamples.Add('ElapsedMs='+$watch.ElapsedMilliseconds+';ExitCode='+$inspectCode+';TimedOut='+$probeTimedOut+';StdoutBase64='+(To-Base64 $inspectStdout)+';StderrBase64='+(To-Base64 $inspectStderr))
        if($inspectCode -eq 0 -and -not $probeTimedOut){
          $inspected=$true
          $inspectText=[IO.File]::ReadAllText($inspectOutput,[Text.Encoding]::UTF8)
          $creationMatch=[regex]::Match($inspectText,'"creationFiletime":([0-9]+)')
          if(-not $creationMatch.Success){throw 'Inspect result missing creation identity'}
          Write-New 'wheel-baseline-ready.txt' ('ElapsedMs='+$watch.ElapsedMilliseconds+"`r`nPID="+$pidValue+"`r`nCreationFiletime="+$creationMatch.Groups[1].Value)
          [Threading.Thread]::Sleep(8000)
          Write-New 'wheel-dispatch-start.txt' ('ElapsedMs='+$watch.ElapsedMilliseconds)
          for($wheelIndex=1;$wheelIndex -le 12;$wheelIndex++) {
            Run-Wheel $pidValue $creationMatch.Groups[1].Value $wheelIndex
            [Threading.Thread]::Sleep(80)
          }
          Write-New 'wheel-dispatch-finish.txt' ('ElapsedMs='+$watch.ElapsedMilliseconds)
          [Threading.Thread]::Sleep(8000)
          $script:trace.Add('twelve native outer-wheel inputs sent with before/after pause')
        }
      }
      if(-not $closePosted -and $watch.ElapsedMilliseconds -ge $nextCloseMs -and $watch.ElapsedMilliseconds -lt 115000){
        $nextCloseMs+=5000
        $process.Refresh();$mainHandle=$process.MainWindowHandle
        $posted=$false
        if($mainHandle -ne [IntPtr]::Zero){$posted=$process.CloseMainWindow()}
        $closeSamples.Add('ElapsedMs='+$watch.ElapsedMilliseconds+';MainWindowHandle='+$mainHandle+';CloseMainWindowPosted='+$posted)
        if($posted){$closePosted=$true}
      }
      if($watch.ElapsedMilliseconds -ge 120000){$timedOut=$true;break}
    }
    if($timedOut){if(-not $process.HasExited){$process.Kill()};if(-not $process.WaitForExit(5000)){$stillRunning=$true}}
    if(-not $stillRunning){$code=$process.ExitCode}
    Write-New 'window-inspect-samples.txt' ([string]::Join("`r`n",$probeSamples.ToArray()))
    Write-New 'close-attempt.txt' ('PID='+$pidValue+"`r`n"+([string]::Join("`r`n",$closeSamples.ToArray())))
    Write-New ($Name+'-run.txt') ('Name='+$Name+"`r`nApplication="+$App+"`r`nArguments="+$Arguments+"`r`nPID="+$pidValue+"`r`nStartUtc="+$start+"`r`nTimeoutMs=120000`r`nTimedOut="+$timedOut+"`r`nRemainingProcess="+$stillRunning+"`r`nExitCode="+$code)
    Write-New ($Name+'-exitcode.txt') ([string]$code)
    if(Test-Path -LiteralPath $log){[IO.File]::Move($log,(Join-Path $Results ($Name+'-smoke.log')))}
    $navigation=Join-Path $working 'source-navigation.log'
    if(Test-Path -LiteralPath $navigation){[IO.File]::Move($navigation,(Join-Path $Results ($Name+'-navigation.log')))}
    if($timedOut -or $stillRunning -or $code -ne 0){throw ($Name+' failed or timed out: '+$code)}
    if(-not $inspected -or -not $closePosted){throw ($Name+' did not complete visible inspection, native wheel sequence and natural close')}
    $script:trace.Add($Name+' exit=0')
  } finally {
    if($startedProcess) {
      try {if(-not $process.HasExited){$process.Kill();$process.WaitForExit(5000)}} catch {}
    }
    $process.Close()
  }
}
try {
  $os=Get-WmiObject Win32_OperatingSystem
  Write-New 'guest-identity.txt' ('Version='+$os.Version+"`r`nServicePackMajorVersion="+$os.ServicePackMajorVersion+"`r`nOSArchitecture="+$os.OSArchitecture+"`r`nPointerBytes="+[IntPtr]::Size+"`r`nPowerShellMajor="+$PSVersionTable.PSVersion.Major+"`r`nCaption="+$os.Caption)
  if($os.Version -ne '6.1.7601' -or $os.ServicePackMajorVersion -ne 1 -or [IntPtr]::Size -ne 4){throw 'Guest must be Win7 SP1 x86'}
  $transferLines=@(Get-Content -LiteralPath (Join-Path $Transfer 'transfer-files.tsv') -Encoding UTF8 | Where-Object {$_ -ne ''})
  $files=@(Get-ChildItem -LiteralPath $Transfer -Force)
  Write-TransferDiagnostics $files $transferLines
  if(@($files | Where-Object {$_.PSIsContainer}).Count -ne 0 -or $files.Count -ne $transferLines.Count+1){throw 'Transfer file set differs'}
  $seen=@{};$observed=New-Object 'System.Collections.Generic.List[string]'
  foreach($line in $transferLines) {
    $f=$line.Split([char]9)
    if($f.Count -ne 3 -or $f[0] -cnotmatch '^[a-f0-9]{64}$' -or $f[1] -notmatch '^\d+$' -or $f[2] -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or $seen.ContainsKey($f[2])){throw 'Invalid transfer manifest'}
    $seen[$f[2]]=$true;$file=Get-Item -LiteralPath (Join-Path $Transfer $f[2])
    $hash=Get-Sha256 $file.FullName
    if($file.Length -ne [long]$f[1] -or $hash -cne $f[0]){throw ('Transfer hash differs: '+$f[2])}
    $observed.Add($hash+"`t"+$file.Length+"`t"+$f[2])
  }
  $manifestFile=Get-Item -LiteralPath (Join-Path $Transfer 'transfer-files.tsv')
  $observed.Add((Get-Sha256 $manifestFile.FullName)+"`t"+$manifestFile.Length+"`ttransfer-files.tsv")
  Write-New 'transfer-observed.tsv' ([string]::Join("`r`n",$observed.ToArray()))
  $script:trace.Add('transfer verified')
  & (Join-Path $Transfer 'V02DisplayModeProbe.exe') --set-1024x768 > (Join-Path $Results 'display-mode.log') 2>&1
  $displayCode=$LASTEXITCODE
  Write-New 'display-mode-exitcode.txt' ([string]$displayCode)
  if($displayCode -ne 0){throw ('Display change failed: '+$displayCode)}
  $script:trace.Add('1024x768 display verified')
  Run-Archive 'starter-zip-test' @('t',(Join-Path $Transfer 'SouiStarter.zip'))
  Run-Archive 'workbench-zip-test' @('t',(Join-Path $Transfer 'SouiWorkbench.zip'))
  Run-Archive 'starter-extract' @('x','-y',('-o'+(Join-Path $Target 'SouiStarter')),(Join-Path $Transfer 'SouiStarter.zip'))
  Run-Archive 'workbench-extract' @('x','-y',('-o'+(Join-Path $Target 'SouiWorkbench')),(Join-Path $Transfer 'SouiWorkbench.zip'))
  Verify-Payload 'pre'
  $script:trace.Add('payload pre verified')
  Run-App 'workbench-visible-explore' 'SouiWorkbench' '--arch=x86 --renderer=cpu --react --page=data --width=950 --height=550' 'webview-smoke.log'
  Verify-Payload 'post'
  $script:trace.Add('payload post verified')
  Write-New 'run-complete.txt' 'version=0.3.0-preview.15;buildId=f0c38056-058a-456e-be99-17446127f7ec;source=15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d;apps=1;status=completed;scope=1024x768-layout-wheel-exploration-v2-before-after-not-full-v02-input'
  $script:exitCode=0
} catch {
  Write-New 'harness-exception.txt' $_.ToString()
  $script:trace.Add('failed: '+$_.ToString())
} finally {
  Write-New 'harness.log' ([string]::Join("`r`n",$script:trace.ToArray()))
  Write-New 'run-exitcode.txt' ([string]$script:exitCode)
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Transfer 'export-serial.ps1') -Results $Results
  if($LASTEXITCODE -ne 0){$script:exitCode=20}
}
exit $script:exitCode
