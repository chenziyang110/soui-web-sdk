param([Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,[Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Attempt)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$env:VcpkgEnabled='false';$env:VcpkgAutoLink='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
$repo='C:\SOUI-SDK-development\preview14-scheduler'
$capture=Join-Path $PSScriptRoot 'Capture-Command.cjs'
function Assert-Idle([string]$stage){
  $busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'})
  [ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=@($busy|Select-Object ProcessId,Name,CreationDate)}|ConvertTo-Json -Depth 5|Out-File "$PSScriptRoot\$Attempt-$stage-gate.json" -Encoding utf8 -NoClobber
  if($busy.Count){throw 'Other workload detected; no next command launched'}
}
Assert-Idle 'before'
& node "$PSScriptRoot\Bind-Inputs.cjs" before $Attempt $Architecture
if($LASTEXITCODE -ne 0){throw 'Input binding failed'}
& node $capture "$Attempt-configure" 120000 cmake.exe -S $repo -B "$repo\out\$Architecture"
if($LASTEXITCODE -ne 0){throw 'Configuration failed'}
Assert-Idle 'compile'
& node $capture "$Attempt-build" 600000 cmake.exe --build "$repo\out\$Architecture" --config Release --target soui_workbench_interaction_tests soui_demo_session_renewal_tests -- /m:2
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
Assert-Idle 'test'
& node $capture "$Attempt-interaction" 60000 "$repo\out\$Architecture\tests\native\Release\soui_workbench_interaction_tests.exe"
if($LASTEXITCODE -ne 0){throw 'Interaction test failed'}
Assert-Idle 'regression'
& node $capture "$Attempt-renewal" 60000 "$repo\out\$Architecture\tests\native\Release\soui_demo_session_renewal_tests.exe"
if($LASTEXITCODE -ne 0){throw 'Adapter renewal regression failed'}
Assert-Idle 'after'
& node "$PSScriptRoot\Bind-Inputs.cjs" after $Attempt $Architecture
if($LASTEXITCODE -ne 0){throw 'Input drift detected'}
