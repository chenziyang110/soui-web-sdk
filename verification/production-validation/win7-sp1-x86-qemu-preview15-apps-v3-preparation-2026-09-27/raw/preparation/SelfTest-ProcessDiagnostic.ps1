# Host helper test; no target application or VM is started.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$Results=Join-Path $root 'host-process-diagnostics'
if(Test-Path -LiteralPath $Results){throw 'Refuse to replace diagnostic test'}
[IO.Directory]::CreateDirectory($Results)|Out-Null
$tokens=$null;$parseErrors=$null
$scriptText=[IO.File]::ReadAllText((Join-Path $root 'harness\run-apps.ps1'))
$ast=[System.Management.Automation.Language.Parser]::ParseInput($scriptText,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Guest script syntax invalid'}
foreach($functionName in @('Write-New','Write-ProcessDiagnostic')) {
  $found=@($ast.FindAll({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst]},$false)|Where-Object {$_.Name -eq $functionName})
  if($found.Count -ne 1){throw 'Missing/ambiguous helper'}
  . ([ScriptBlock]::Create($found[0].Extent.Text))
}
$guard='if($watch.ElapsedMilliseconds -ge 60000){$timedOut=$true;break}'
$diagnostic='if(-not $diagnosticTaken -and $watch.ElapsedMilliseconds -ge 55000){$diagnosticTaken=$true;Write-ProcessDiagnostic $Name $process $watch $log}'
if($scriptText.IndexOf($guard) -lt 0 -or $scriptText.IndexOf($diagnostic) -le $scriptText.IndexOf($guard)){throw 'Original deadline must precede diagnostics'}
if(-not $scriptText.Contains('if($timedOut){$process.Kill();if(-not $process.WaitForExit(5000)){$stillRunning=$true}}')){throw 'Original termination changed'}
$probeLog=Join-Path $Results 'fixture-smoke.log'
[IO.File]::WriteAllText($probeLog,"process-start`r`nhost-close-start`r`n")
$watchFixture=New-Object PSObject -Property @{ElapsedMilliseconds=55001}
$ownProcess=[Diagnostics.Process]::GetCurrentProcess()
try {Write-ProcessDiagnostic 'helper-test' $ownProcess $watchFixture $probeLog}finally{$ownProcess.Close()}
$result=[IO.File]::ReadAllText((Join-Path $Results 'helper-test-process-diagnostic.txt'))
foreach($expected in @('DiagnosticVersion=1','ElapsedMs=55001',('PID='+$PID),'Thread=','Smoke=host-close-start','CompletedElapsedMs=55001')) {
  if(-not $result.Contains($expected)){throw ('Missing real process diagnostic: '+$expected)}
}
if($result.Contains('DiagnosticError=') -or $result.Contains('ThreadReadError=')){throw 'Process diagnostics failed'}
Write-Output 'process-diagnostic-helper-tested;actualProcess=self;deadlineUnchanged=60000;applicationsStarted=false;guestExecuted=false'
