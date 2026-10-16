[CmdletBinding()]
param([Parameter(Mandatory)][string]$CandidatePath,[Parameter(Mandatory)][string]$BaselinePath,[Parameter(Mandatory)][string]$FailureDirectory)
$ErrorActionPreference='Stop'
function Need([bool]$value,[string]$message){if(-not$value){throw $message}}
function Norm([string]$value){$value.Replace('\','/').ToLowerInvariant()}
function Full([string]$value,[string]$base){if([IO.Path]::IsPathRooted($value)){return [IO.Path]::GetFullPath($value)};[IO.Path]::GetFullPath((Join-Path $base $value))}
function Declarations([string]$path,[string[]]$names){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors);Need ($errors.Count-eq0) 'PowerShell syntax'
 foreach($name in $names){$nodes=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true));Need ($nodes.Count-eq1) ('Unique pure declaration: '+$name);$nodes[0].Extent.Text}
}
$names=@('GeneratedTestHash','GeneratedTestRelative','GeneratedTestFrozen','GeneratedTestWriter','GeneratedTestInterval','GeneratedTestMarker','GeneratedTestActual','GeneratedTestDependencies','NinjaDependencyBlocks','DependencyBindings')
foreach($declaration in @(Declarations $CandidatePath $names)){. ([scriptblock]::Create($declaration))}
foreach($declaration in @(Declarations $BaselinePath @('DependencyPin'))){. ([scriptblock]::Create($declaration))}
# Only AST-extracted functions run. The observer entry point and tools never run.
$cfg=Get-Content -LiteralPath (Join-Path $FailureDirectory 'llvm-observer-inputs.json') -Raw|ConvertFrom-Json
$root=$cfg.root;$build=$cfg.buildDirectory;$configuration=$cfg.configuration
$allowed=@{};foreach($pin in $cfg.dependencyPins){$allowed[(Norm $pin.path)]=$pin};$original=$allowed.Clone();$dependencyPins=@{}
$reader={param($path)return ,([IO.File]::ReadAllBytes($path))}
function MatchPin($pin){Need ($null-ne$pin) 'Known pin';$raw=[IO.File]::ReadAllBytes($pin.path);Need ($raw.Length-eq$pin.bytes-and(GeneratedTestHash $raw)-ceq$pin.sha256) 'Physical pin changed';$pin}
$rawDeps=[IO.File]::ReadAllText((Join-Path $FailureDirectory 'llvm-observer/ninja-dependencies.txt'))
$db=[IO.File]::ReadAllText((Join-Path $build 'compile_commands.json'))|ConvertFrom-Json
$commands=@($db|Where-Object{$_.output.Replace('\','/').Contains('/'+$configuration+'/',[StringComparison]::Ordinal)})
$bound=DependencyBindings $build $commands $rawDeps;$selected=$bound.selected
$missing=@{};foreach($record in $selected){foreach($header in $record.headers){if(-not$original.ContainsKey((Norm $header.resolved))){$missing[(Norm $header.resolved)]=$header.resolved}}}

@{selected=$selected.Count;missing=$missing.Count;pins=$cfg.dependencyPins.Count;configuration=$configuration;withVendorTests=$cfg.withVendorTests;hasVendorField=($null-ne$cfg.PSObject.Properties['withVendorTests'])}|ConvertTo-Json
