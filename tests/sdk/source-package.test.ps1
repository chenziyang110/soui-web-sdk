param([string]$CandidateSource=(Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference='Stop'
$file=Join-Path $CandidateSource 'tools/Package.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Candidate Package source syntax errors'}
# Extract only the two fixed production data functions; never dot-source the
# package entry or invoke Node, Git, 7-Zip, compilation or a candidate module.
foreach($name in @('Get-SouiExplicitSourcePackagePaths','Get-SouiStagedSourcePackageVersion')){
 $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
 if($definitions.Count -ne 1){throw "Expected one production data function: $name"}
 . ([scriptblock]::Create($definitions[0].Extent.Text))
}
function Assert-Throws($Action,[string]$Reason){
 try {& $Action} catch {if($_.Exception.Message -like "*$Reason*"){return}; throw}
 throw "Expected rejection containing $Reason"
}
$temp=Join-Path ([IO.Path]::GetTempPath()) ('soui-package-data-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp|Out-Null
try {
 # Actual production path planning admits a fresh external workspace, confines
 # archive/logs there, refuses existing and escaped outputs, and creates nothing.
 $workspace=Join-Path $temp 'external'
 $plan=Get-SouiExplicitSourcePackagePaths -Workspace $workspace -Archive (Join-Path $workspace 'dist/custom.zip')
 if($plan.stage -cne (Join-Path $workspace 'source') -or $plan.archiveLog -cne (Join-Path $workspace 'archive.log') -or (Test-Path -LiteralPath $workspace)){throw 'External plan changed or created inputs'}
 Assert-Throws {Get-SouiExplicitSourcePackagePaths -Workspace $temp} 'already exists'
 Assert-Throws {Get-SouiExplicitSourcePackagePaths -Workspace $workspace -Archive (Join-Path $temp 'escaped.zip')} 'under'
 Assert-Throws {Get-SouiExplicitSourcePackagePaths -Workspace 'relative'} 'absolute'
 # Staged VERSION is authoritative. A differing tool-host VERSION never enters
 # this production function; mismatching stage result and unsafe names fail.
 $stage=Join-Path $temp 'staged'; New-Item -ItemType Directory -Path $stage|Out-Null
 [IO.File]::WriteAllText((Join-Path $temp 'VERSION'),'tool-host-other')
 [IO.File]::WriteAllText((Join-Path $stage 'VERSION'),'0.5.0-preview.1')
 $version=Get-SouiStagedSourcePackageVersion -Stage $stage -StageResult @{version='0.5.0-preview.1'}
 if($version -cne '0.5.0-preview.1'){throw 'Host version incorrectly selected'}
 Assert-Throws {Get-SouiStagedSourcePackageVersion -Stage $stage -StageResult @{version='tool-host-other'}} 'differs'
 [IO.File]::WriteAllText((Join-Path $stage 'VERSION'),'../wrong')
 Assert-Throws {Get-SouiStagedSourcePackageVersion -Stage $stage -StageResult @{version='../wrong'}} 'invalid'
 # Source sequencing: the explicit stage branch must have no mkdir command
 # before its genuine stage call; only the legacy branch creates root/dist.
 $entry=$ast.FindAll({param($node) $node -is [Management.Automation.Language.IfStatementAst] -and $node.Clauses[0].Item1.Extent.Text -ceq '$SourceRootsBinding'},$true)
 $dispatch=@($entry|Where-Object {$_.Clauses[0].Item2.Extent.Text.Contains('$stageText=& node')})
 if($dispatch.Count -ne 1){throw 'Explicit stage dispatch not unique'}
 $commands=@($dispatch[0].Clauses[0].Item2.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst]},$true))
 if(@($commands|Where-Object {$_.GetCommandName() -ceq 'New-Item'}).Count){throw 'Explicit input root could be polluted before staging'}
 Write-Output 'Source package data functions and dispatch structure: 3 groups passed; no Package entry / Node / Git / 7-Zip / build executed.'
} finally {
 $resolved=[IO.Path]::GetFullPath($temp)
 if(-not $resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)){throw 'Temporary target escaped temp root'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
