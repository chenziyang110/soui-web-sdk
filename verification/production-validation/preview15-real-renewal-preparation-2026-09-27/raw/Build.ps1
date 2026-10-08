param(
  [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,63}$')][string]$AttemptName,
  [string]$NodePath = (Get-Command node.exe -ErrorAction Stop).Source,
  [string]$CMakePath = (Get-Command cmake.exe -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
$env:VcpkgEnabled='false';$env:VcpkgAutoLink='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
Set-StrictMode -Version Latest
$fixtureRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$attemptRoot = Join-Path (Join-Path $fixtureRoot 'attempts') $AttemptName
if (Test-Path -LiteralPath $attemptRoot) { throw 'Attempt exists; preserve it and choose a new name.' }
[IO.Directory]::CreateDirectory((Split-Path -Parent $attemptRoot)) | Out-Null
New-Item -ItemType Directory -Path $attemptRoot -ErrorAction Stop | Out-Null
$logRoot = Join-Path $attemptRoot 'logs'
[IO.Directory]::CreateDirectory($logRoot) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
function Write-NewJson([string]$Destination, $Value) {
  $stream = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
  try { $bytes = $utf8.GetBytes(($Value | ConvertTo-Json -Depth 20) + "`n"); $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) }
  finally { $stream.Dispose() }
}
function Quote-Argument([string]$Value) {
  # Windows CreateProcess argv quoting, with no cmd.exe/shell expansion.
  if ($Value -notmatch '[\s"]' -and $Value.Length -gt 0) { return $Value }
  return '"' + ([regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1')) + '"'
}
function Latch-Failure([ref]$Failure,[string]$Message) {
  if (-not $Failure.Value) { $Failure.Value=$Message }
}
function Close-LogFile($Stream,[ref]$Failure) {
  if (-not $Stream) { return }
  try { $Stream.Flush($true) } catch { Latch-Failure $Failure $_.Exception.Message }
  finally { try { $Stream.Dispose() } catch { Latch-Failure $Failure $_.Exception.Message } }
}
function Stop-OwnedBuildProcess($Process,[ref]$Failure) {
  try {
    if ($Process.HasExited) { return }
    # The held Process is still alive: this PID is our own build child. Its
    # descendants may include MSBuild/compiler processes and belong to this step.
    $killInfo=New-Object Diagnostics.ProcessStartInfo
    $killInfo.FileName=Join-Path $env:SystemRoot 'System32\taskkill.exe'
    $killInfo.Arguments='/PID '+$Process.Id+' /T /F'
    $killInfo.UseShellExecute=$false; $killInfo.CreateNoWindow=$true
    $killInfo.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $killer=$null
    try {
      $killer=[Diagnostics.Process]::Start($killInfo)
      if (-not $killer.WaitForExit(10000)) {
        $killer.Kill(); [void]$killer.WaitForExit(10000)
        Latch-Failure $Failure 'Owned build-tree cleanup command timed out'
      }
    } catch { Latch-Failure $Failure $_.Exception.Message }
    finally { if ($killer) { try { $killer.Dispose() } catch { Latch-Failure $Failure $_.Exception.Message } } }
    if (-not $Process.HasExited) { $Process.Kill() }
    if (-not $Process.WaitForExit(10000)) { Latch-Failure $Failure 'Own build process did not exit' }
  } catch { Latch-Failure $Failure $_.Exception.Message }
}
function Invoke-Step([string]$Name, [string]$Program, [string[]]$Arguments, [string]$WorkingDirectory, [int]$TimeoutSeconds) {
  $stdoutPath = Join-Path $logRoot ($Name + '.stdout.log')
  $stderrPath = Join-Path $logRoot ($Name + '.stderr.log')
  $resultPath = Join-Path $logRoot ($Name + '.result.json')
  foreach ($file in @($stdoutPath,$stderrPath,$resultPath)) { if (Test-Path -LiteralPath $file) { throw "Step exists: $Name" } }
  $info = New-Object Diagnostics.ProcessStartInfo
  $info.FileName = $Program
  $info.Arguments = (($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
  $info.WorkingDirectory = $WorkingDirectory
  $info.UseShellExecute = $false
  $info.CreateNoWindow = $true
  $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  $info.RedirectStandardOutput = $true
  $info.RedirectStandardError = $true
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $info
  $started = [DateTime]::UtcNow.ToString('o')
  $exitCode = $null; $timedOut = $false; $failure = $null; $processId = $null
  $stdoutTask = $null; $stderrTask = $null; $stdoutFile=$null; $stderrFile=$null
  # Copy raw bytes while the child runs, retaining partial logs even on timeout.
  try {
    $stdoutFile = [IO.File]::Open($stdoutPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $stderrFile = [IO.File]::Open($stderrPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    if (-not $process.Start()) { throw 'Process did not start' }
    $processId = $process.Id
    $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
    $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderrFile)
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      $timedOut = $true
      throw 'Build step timed out'
    }
    $exitCode = $process.ExitCode
    if (-not $stdoutTask.Wait(10000) -or -not $stderrTask.Wait(10000)) { throw 'Output pipe remained open after step completion' }
  } catch { $failure = $_.Exception.Message }
  finally {
    try {
      if ($processId) {
        Stop-OwnedBuildProcess $process ([ref]$failure)
        try { if ($process.HasExited) { $exitCode=$process.ExitCode } } catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        foreach ($which in @('StandardOutput','StandardError')) {
          try { $process.$which.Dispose() } catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        }
      }
      foreach ($copyTask in @($stdoutTask,$stderrTask)) {
        if ($copyTask) {
          try { if (-not $copyTask.Wait(1000)) { Latch-Failure ([ref]$failure) 'Output copy did not stop' } }
          catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        }
      }
      Close-LogFile $stdoutFile ([ref]$failure)
      Close-LogFile $stderrFile ([ref]$failure)
      try {
        Write-NewJson $resultPath ([ordered]@{program=$Program;arguments=$Arguments;workingDirectory=$WorkingDirectory;processId=$processId;
          startedAt=$started;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$exitCode;timedOut=$timedOut;timeoutSeconds=$TimeoutSeconds;error=$failure})
      } catch { Latch-Failure ([ref]$failure) $_.Exception.Message; [Console]::Error.WriteLine('Cannot persist step result: '+$_.Exception.Message) }
    } finally {
      try { $process.Dispose() } catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
    }
  }
  if ($failure -or $timedOut -or $null -eq $exitCode -or $exitCode -ne 0) { throw "Step failed: $Name; see $resultPath" }
}
try {
  $NodePath = [IO.Path]::GetFullPath($NodePath)
  $CMakePath = [IO.Path]::GetFullPath($CMakePath)
  $npmCli = Join-Path (Split-Path -Parent $NodePath) 'node_modules\npm\bin\npm-cli.js'
  if (-not (Test-Path -LiteralPath $npmCli -PathType Leaf)) { throw 'npm-cli.js must belong to the selected Node installation' }
  $prepare = Join-Path $fixtureRoot 'Prepare.cjs'
  Invoke-Step 'prepare' $NodePath @($prepare,'begin',$Architecture,$AttemptName,$NodePath,$CMakePath,$npmCli) $fixtureRoot 180
  $frozenPrepare = Join-Path $attemptRoot 'drivers\Prepare.cjs'
  Invoke-Step 'node-version' $NodePath @('--version') $attemptRoot 30
  Invoke-Step 'cmake-version' $CMakePath @('--version') $attemptRoot 30
  Invoke-Step 'npm-version' $NodePath @($npmCli,'--version') $attemptRoot 30
  Invoke-Step 'npm-ci' $NodePath @($npmCli,'ci','--no-audit','--no-fund') (Join-Path $attemptRoot 'toolchain') 600
  Invoke-Step 'toolchain-freeze' $NodePath @($frozenPrepare,'toolchain-freeze',$AttemptName) $fixtureRoot 180
  Invoke-Step 'web-build' $NodePath @((Join-Path $attemptRoot 'drivers\Build-Web.mjs'),$AttemptName) $fixtureRoot 300
  $platform = if ($Architecture -eq 'x86') { 'Win32' } else { 'x64' }
  $source = Join-Path $attemptRoot 'consumer-source'
  $build = Join-Path $attemptRoot 'native-build'
  $installed = Join-Path $fixtureRoot ("inputs\install\$Architecture\lib\cmake\SouiSdk")
  Invoke-Step 'configure' $CMakePath @('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A',$platform,'-T','v143',
    '-DCMAKE_SYSTEM_VERSION=10.0.19041.0',('-DSouiSdk_DIR=' + $installed)) $attemptRoot 180
  Invoke-Step 'native-build' $CMakePath @('--build',$build,'--config','Release','--target','InstalledRenewalProbe','SupervisorClockTests','--parallel','2') $attemptRoot 600
  Invoke-Step 'clock-check' (Join-Path $build 'Release\SupervisorClockTests.exe') @() $build 30
  Invoke-Step 'deploy' $NodePath @($frozenPrepare,'deploy',$AttemptName) $fixtureRoot 180
  $runtime = Join-Path $attemptRoot 'runtime'
  Invoke-Step 'self-check' (Join-Path $runtime 'InstalledRenewalProbe.exe') @('--self-check') $runtime 30
  Invoke-Step 'finalize' $NodePath @($frozenPrepare,'finalize',$AttemptName) $fixtureRoot 180
  Write-Output "Build and no-GUI self-check complete: $attemptRoot"
  Write-Output 'No desktop/real-clock renewal test has run. Use the frozen runtime only after the desktop gate is clear.'
  exit 0
} catch {
  $originalFailure=$_
  try {
    Write-NewJson (Join-Path $attemptRoot 'build-failed.json') ([ordered]@{finishedAt=[DateTime]::UtcNow.ToString('o');
      architecture=$Architecture;attempt=$AttemptName;message=$originalFailure.Exception.Message;guiExecuted=$false;realRenewalVerified=$false;releaseReady=$false})
  } catch { [Console]::Error.WriteLine('Cannot persist build failure: '+$_.Exception.Message) }
  Write-Error $originalFailure -ErrorAction Continue
  exit 1
}
