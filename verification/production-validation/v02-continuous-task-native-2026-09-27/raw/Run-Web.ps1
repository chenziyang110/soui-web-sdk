param([Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Attempt)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$repo='C:\SOUI-SDK-development\preview14-scheduler'
$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'})
[ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=@($busy|Select-Object ProcessId,Name,CreationDate)}|ConvertTo-Json -Depth 5|Out-File "$PSScriptRoot\$Attempt-gate.json" -Encoding utf8 -NoClobber
if($busy.Count){exit 2}
& node "$PSScriptRoot\Capture-Command.cjs" "$Attempt-typecheck" 120000 node.exe "$repo\demos\workbench\web\node_modules\typescript\bin\tsc" --project "$repo\demos\workbench\web\tsconfig.json" --noEmit
if($LASTEXITCODE -ne 0){throw 'Type check failed; retain original diagnostics'}
