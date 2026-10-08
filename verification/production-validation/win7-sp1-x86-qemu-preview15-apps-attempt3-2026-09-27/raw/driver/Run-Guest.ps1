param([Parameter(Mandatory=$true)][ValidatePattern('^preview15-win7-apps-attempt[0-9]+$')][string]$Attempt)
$ErrorActionPreference='Stop'
# Windows PowerShell must not inherit the PowerShell 7 module directory.
# This process-local environment change also applies to its child tools.
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
$hashCommand=Get-Command Get-FileHash -ErrorAction Stop
[ordered]@{attempt=$Attempt;powerShellVersion=$PSVersionTable.PSVersion.ToString();processModulePath=$env:PSModulePath;hashCommandModule=$hashCommand.Module.Path}|ConvertTo-Json -Compress|Write-Output
& "$PSScriptRoot\Launch-Current.ps1" -Attempt $Attempt
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
