param(
  [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Attempt,
  [switch]$SkipWeb
)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$env:VcpkgEnabled='false'
$env:VcpkgAutoLink='false'
$env:VcpkgEnableManifest='false'
$env:MSBUILDDISABLENODEREUSE='1'
$repo='C:\SOUI-SDK-development\work-area-placement'

function Assert-Idle([string]$stage) {
  $busy=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\.exe$'
  })
  [ordered]@{at=[DateTime]::UtcNow.ToString('o');processes=@($busy|Select-Object ProcessId,Name,CreationDate)} |
    ConvertTo-Json -Depth 5 |
    Out-File "$PSScriptRoot\$Attempt-$stage-official-gate.json" -Encoding utf8 -NoClobber
  if ($busy.Count) { throw 'Other workload detected; no next command launched' }
}

Assert-Idle 'before'
& node "$PSScriptRoot\Bind-OfficialBuild.cjs" before $Attempt $Architecture
if ($LASTEXITCODE -ne 0) { throw 'Source binding failed' }

$buildCommand=@("$Attempt-official-build",'1200000','powershell.exe','-NoProfile','-File',
  "$repo\tools\Build.ps1",'-Architecture',$Architecture,'-Configuration','Release')
if ($SkipWeb) { $buildCommand+='-SkipWeb' }
& node "$PSScriptRoot\Capture-IsolatedBuild.cjs" @buildCommand
if ($LASTEXITCODE -ne 0) { throw 'Official Build.ps1 failed' }

Assert-Idle 'after'
& node "$PSScriptRoot\Bind-OfficialBuild.cjs" after $Attempt $Architecture
if ($LASTEXITCODE -ne 0) { throw 'Official build identity failed' }
