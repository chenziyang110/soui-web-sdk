param(
  [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Attempt
)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$repo='C:\SOUI-SDK-development\work-area-placement'
$test=Join-Path $repo "out\$Architecture\tests\native\Release\soui_workbench_window_placement_tests.exe"
if(-not (Test-Path -LiteralPath $test -PathType Leaf)){ throw 'Placement test executable is missing' }

function Assert-Idle([string]$stage) {
  $busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'
  })
  [ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=@($busy|Select-Object ProcessId,Name,CreationDate)} |
    ConvertTo-Json -Depth 5 |
    Out-File "$PSScriptRoot\$Attempt-$stage-test-gate.json" -Encoding utf8 -NoClobber
  if($busy.Count){ throw 'Other workload detected; placement test not run' }
}

Assert-Idle 'before'
$before=(Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash
& ctest --test-dir (Join-Path $repo "out\$Architecture") -C Release -R '^demo\.native\.window-placement$' --output-on-failure
$code=$LASTEXITCODE
$after=(Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash
[ordered]@{architecture=$Architecture;attempt=$Attempt;test=$test;sha256Before=$before;sha256After=$after;exitCode=$code} |
  ConvertTo-Json -Depth 4 |
  Out-File "$PSScriptRoot\$Attempt-test-identity.json" -Encoding utf8 -NoClobber
if($before -ne $after){ throw 'Placement test executable changed during execution' }
if($code -ne 0){ throw "Placement test failed with exit $code" }
Assert-Idle 'after'
