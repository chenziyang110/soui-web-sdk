param(
  [Parameter(Mandatory=$true)][ValidatePattern('^guest-layout-explore-v2-attempt[0-9]+$')][string]$Attempt,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$ExpectedFreezeSha256
)
$ErrorActionPreference='Stop'
# Keep Windows PowerShell's own module path for Get-FileHash on the host.
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$hashCommand=Get-Command Get-FileHash -ErrorAction Stop
[ordered]@{attempt=$Attempt;expectedFreezeSha256=$ExpectedFreezeSha256;powerShellVersion=$PSVersionTable.PSVersion.ToString();processModulePath=$env:PSModulePath;hashCommandModule=$hashCommand.Module.Path}|ConvertTo-Json -Compress|Write-Output
& "$PSScriptRoot\Launch-LayoutExploreV2.ps1" -Attempt $Attempt -ExpectedFreezeSha256 $ExpectedFreezeSha256
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
