param(
  [Parameter(Mandatory=$true)][string]$PairManifest,
  [Parameter(Mandatory=$true)][ValidateSet('baseline','candidate')][string]$Variant,
  [Parameter(Mandatory=$true)][ValidateSet('miniblink','webview2')][string]$Backend,
  [ValidateSet(0,1000,2000)][int]$EngineBudget = 0,
  [ValidateRange(30,1000)][int]$FrameRate = 60,
  [ValidateSet('off','scalar')][string]$Observation = 'off',
  [ValidateSet('off','scalar')][string]$InputTrace = 'off',
  [switch]$SampleProcessTree,
  [string]$FixedWebView2 = ''
)
$ErrorActionPreference = 'Stop'
$manifestPath = (Resolve-Path -LiteralPath $PairManifest).Path
$bundleRoot = Split-Path -Parent $manifestPath
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'soui.sdk-performance-manual-pair/v1') { throw 'Unrecognized frozen manual-pair manifest' }
function Resolve-BundlePath([string]$relative) {
  if ([IO.Path]::IsPathRooted($relative)) { throw 'Manifest paths must stay relative to its bundle' }
  $resolved = [IO.Path]::GetFullPath((Join-Path $bundleRoot $relative))
  $prefix = [IO.Path]::GetFullPath($bundleRoot).TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
  if (!$resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes bundle' }
  return $resolved
}
function Verify-File($identity) {
  if ($identity.sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Missing frozen SHA-256 identity' }
  $path = Resolve-BundlePath $identity.path
  $file = Get-Item -LiteralPath $path
  if ($file.PSIsContainer -or $file.Length -ne $identity.bytes -or
      (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $identity.sha256) {
    throw "Frozen file changed: $($identity.path)"
  }
  return $path
}
$chosen = $manifest.variants.$Variant
if (!$chosen -or !$chosen.executable -or !$chosen.runtimeFiles -or !$manifest.pageFiles) { throw 'Incomplete frozen variant' }
$executable = Verify-File $chosen.executable
foreach ($file in $chosen.runtimeFiles) { $null = Verify-File $file }
foreach ($file in $manifest.pageFiles) { $null = Verify-File $file }
$samplerExecutable = $null
if ($SampleProcessTree) {
  if (!$manifest.processTreeSampler) { throw 'This bundle has no frozen process-tree sampler' }
  $samplerExecutable = Verify-File $manifest.processTreeSampler
}
$pageRoot = Resolve-BundlePath $manifest.pageRoot
$resultsRoot = Join-Path $bundleRoot 'results'
if (!(Test-Path -LiteralPath $resultsRoot)) { New-Item -ItemType Directory -Path $resultsRoot | Out-Null }
$run = '{0}-{1}-budget{2}-{3}' -f $Variant,$Backend,$EngineBudget,([Guid]::NewGuid().ToString('N'))
$output = Join-Path $resultsRoot $run
$arguments = @("--backend=$Backend","--page-root=$pageRoot","--result-dir=$output",
  "--engine-budget=$EngineBudget","--frame-rate=$FrameRate","--observation=$Observation","--input-trace=$InputTrace")
if ($Backend -eq 'miniblink') { $arguments += '--miniblink=' + (Verify-File $chosen.miniblink) }
if ($FixedWebView2) {
  if ($Backend -ne 'webview2') { throw 'FixedWebView2 only applies to webview2' }
  $arguments += '--fixed-webview2=' + (Resolve-Path -LiteralPath $FixedWebView2).Path
}
function Quote-NativeArgument([string]$value) {
  # All generated arguments have no quotes. Escape trailing backslashes before
  # the closing quote; Start-Process otherwise joins an unquoted argument array.
  if ($value.Contains('"')) { throw 'Unexpected quote in native argument' }
  $slashes = [regex]::Match($value,'\\*$').Value
  return '"' + $value + $slashes + '"'
}
$commandArguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
$receipt = [ordered]@{schema='soui.sdk-performance-launch/v1';variant=$Variant;backend=$Backend;engineBudgetUs=$EngineBudget;
  frameRate=$FrameRate;observation=$Observation;inputTrace=$InputTrace;executable=$chosen.executable;output=$output;guiRequestedByOperator=$true;performanceVerified=$false;
  coldEnvironment=[ordered]@{
    SOUI_WEBVIEW2_PREPARE_OUTPUT=[Environment]::GetEnvironmentVariable('SOUI_WEBVIEW2_PREPARE_OUTPUT','Process');
    SOUI_WEBVIEW2_OWNER_BUDGET_US=[Environment]::GetEnvironmentVariable('SOUI_WEBVIEW2_OWNER_BUDGET_US','Process');
    SOUI_WEBVIEW2_OUTPUT_TIMING=[Environment]::GetEnvironmentVariable('SOUI_WEBVIEW2_OUTPUT_TIMING','Process')};
  processTree=[ordered]@{requested=[bool]$SampleProcessTree;intervalMs=200;maximumDurationMs=600000;
    scope='read-only host PID + creation FILETIME and observed descendants; completeness reported by sampler';
    tool=$manifest.processTreeSampler;exitCode=$null;forced=$false;retirementDeadlineExceeded=$false;error=$null}}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resultsRoot "$run-launch.json") -Encoding UTF8
Write-Host "Manual window: $Variant / $Backend / budget $EngineBudget / requested $FrameRate Hz"
Write-Host "Results: $output"
# This script is an explicit manual launcher. Build/test scripts never call it.
$process = Start-Process -FilePath $executable -ArgumentList $commandArguments -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Normal -PassThru
# Windows PowerShell 5.1 can lose ExitCode on a short-lived PassThru process
# unless its native handle is cached before WaitForExit. Keep it for this run.
$heldApplicationHandle = $null
try { $heldApplicationHandle = $process.Handle }
catch { $receipt.applicationHandleError = $_.Exception.Message }
$sampler = $null
if ($SampleProcessTree) {
  try {
    # Process.StartTime is read from the held process identity, preserving native
    # FILETIME ticks. The observer is a sibling, not part of the measured tree.
    $creationTime = $process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
    $receipt.processTree.rootPid = $process.Id
    $receipt.processTree.rootCreationFileTime = $creationTime
    $treeOutput = Join-Path $resultsRoot "$run-process-tree.ndjson"
    $receipt.processTree.output = $treeOutput
    $samplerArguments = @([string]$process.Id,'200','600000',$treeOutput,$creationTime)
    $samplerCommand = ($samplerArguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $sampler = Start-Process -FilePath $samplerExecutable -ArgumentList $samplerCommand -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput (Join-Path $resultsRoot "$run-sampler.stdout.log") `
      -RedirectStandardError (Join-Path $resultsRoot "$run-sampler.stderr.log")
    $heldSamplerHandle = $sampler.Handle
  } catch {
    $receipt.processTree.error = $_.Exception.Message
    Write-Warning 'Process-tree sampling did not start; this run has no complete tree observation.'
  }
}
$observedApplicationExit = $null
try { $process.WaitForExit(); $observedApplicationExit = $process.ExitCode }
catch { $receipt.applicationExitError = $_.Exception.Message }
$receipt.exitCode = $observedApplicationExit
$receipt.applicationExitCodeKnown = $null -ne $observedApplicationExit
$launcherExit = if ($null -eq $observedApplicationExit) { 3 } else { $observedApplicationExit }
if ($null -eq $observedApplicationExit) { Write-Warning 'Application exit code is unavailable; this run is not a successful observed exit.' }
if ($sampler) {
  if (!$sampler.WaitForExit(5000)) {
    # Only the task-owned read-only observer is stopped; application exit above
    # was already observed. A forced observer is explicitly a failed sample.
    $receipt.processTree.retirementDeadlineExceeded = $true
    $receipt.processTree.error = 'Observer did not retire within 5s after application exit.'
    try {
      if (!$sampler.HasExited) { $sampler.Kill(); $receipt.processTree.forced = $true }
      if ($sampler.WaitForExit(1000)) { $receipt.processTree.exitCode = $sampler.ExitCode }
    } catch { $receipt.processTree.error += ' ' + $_.Exception.Message }
  } else { $receipt.processTree.exitCode = $sampler.ExitCode }
}
if ($SampleProcessTree -and ($null -eq $receipt.processTree.exitCode -or $receipt.processTree.exitCode -ne 0 -or
    $receipt.processTree.retirementDeadlineExceeded -or $receipt.processTree.error)) {
  if ($launcherExit -eq 0) { $launcherExit = 3 }
  Write-Warning 'Process-tree observation is missing, incomplete, expired or failed; inspect its terminal record.'
}
$receipt.launcherExitCode = $launcherExit
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resultsRoot "$run-exit.json") -Encoding UTF8
exit $launcherExit
