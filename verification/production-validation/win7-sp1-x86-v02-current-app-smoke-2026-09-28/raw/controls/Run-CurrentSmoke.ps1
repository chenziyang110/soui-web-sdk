param([Parameter(Mandatory=$true)][ValidatePattern('^guest-app-smoke-attempt[0-9]+$')][string]$Attempt)
$ErrorActionPreference='Stop'
# Keep Windows PowerShell's own module path for Get-FileHash on the host.
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$hashCommand=Get-Command Get-FileHash -ErrorAction Stop
[ordered]@{attempt=$Attempt;powerShellVersion=$PSVersionTable.PSVersion.ToString();processModulePath=$env:PSModulePath;hashCommandModule=$hashCommand.Module.Path}|ConvertTo-Json -Compress|Write-Output
& "$PSScriptRoot\Launch-CurrentSmoke.ps1" -Attempt $Attempt
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
