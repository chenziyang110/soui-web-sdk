$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$env:VcpkgEnabled='false';$env:VcpkgAutoLink='false';$env:VcpkgEnableManifest='false';$env:MSBUILDDISABLENODEREUSE='1'
$repo='C:\SOUI-SDK-development\preview14-scheduler'
$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'})
[ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=@($busy|Select-Object ProcessId,Name,CreationDate)}|ConvertTo-Json -Depth 5|Out-File "$PSScriptRoot\red-gate.json" -Encoding utf8 -NoClobber
if($busy.Count){exit 2}
& node "$PSScriptRoot\Capture-Command.cjs" red-configure-x64 120000 cmake.exe -S $repo -B "$repo\out\x64"
if($LASTEXITCODE -ne 0){throw 'Red test configuration failed'}
& node "$PSScriptRoot\Capture-Command.cjs" red-build-x64 600000 cmake.exe --build "$repo\out\x64" --config Release --target soui_workbench_interaction_tests -- /m:2
if($LASTEXITCODE -ne 0){throw 'Red test compilation failed'}
& node "$PSScriptRoot\Capture-Command.cjs" red-run-x64 60000 "$repo\out\x64\tests\native\Release\soui_workbench_interaction_tests.exe"
$captureExit=$LASTEXITCODE
$original=Get-Content -LiteralPath "$PSScriptRoot\command-logs\red-run-x64\result.json" -Raw|ConvertFrom-Json
if($original.exitCode -ne 1 -or $original.timedOut -or $original.error){throw 'Expected a behavior failure with raw exit 1'}
$failure=Get-Content -LiteralPath "$PSScriptRoot\command-logs\red-run-x64\stderr.log" -Raw
if($failure -notmatch 'Demo interaction task kind is not available through task.start'){throw 'Unexpected red failure'}
[ordered]@{status='red-behavior-reproduced';originalExitCode=$original.exitCode;captureExitCode=$captureExit;failure=$failure;guiExecuted=$false;releaseReady=$false}|ConvertTo-Json|Out-File "$PSScriptRoot\red-result.json" -Encoding utf8 -NoClobber
