param([ValidateSet('original','proposed')][string]$InputKind='proposed')
$ErrorActionPreference='Stop'
$file=Join-Path $PSScriptRoot ($InputKind+'/tools/Verify-Release.ps1')
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
$definition=$ast.Find({param($node) $node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-eq'Build-And-Smoke'},$true)
if(-not$definition){throw 'Build function missing'}
Invoke-Expression $definition.Extent.Text
$root='C:/release/current';$DevkitRoot='C:/release/stock';$NativeToolchain='LLVM22MSVC142';$sdkArchitecture='x64';$node='node.exe';$output='C:/release/output';$runFile='C:/release/run.json'
$llvmPaths=[ordered]@{LlvmRoot='C:/tools/llvm';MsvcRoot='C:/tools/msvc';WindowsSdkRoot='C:/tools/sdk';NinjaExe='C:/tools/ninja.exe';WindowsCmdExe='C:/Windows/System32/cmd.exe';PowerShellExe='C:/tools/pwsh.exe'}
$script:captured=$null
function Invoke-Json([string[]]$Arguments){return [pscustomobject]@{project='C:/release/phases/fresh'}}
function Invoke-Checked([string]$Executable,[string[]]$Arguments,[string]$Log){$script:captured=@{executable=$Executable;arguments=$Arguments;log=$Log};throw 'CAPTURED_BEFORE_EXTERNAL_EXECUTION'}
try{Build-And-Smoke 'C:/release/transaction/fresh' 'fresh'}catch{if($_.Exception.Message-ne'CAPTURED_BEFORE_EXTERNAL_EXECUTION'){throw}}
if(-not$captured){throw 'Actual entry did not invoke the external command boundary'}
$expected=Join-Path 'C:/release/phases/fresh' 'tools/internal/standard-build.cjs'
if($captured.arguments[0]-ne$expected-or$captured.arguments[1]-ne'--root'-or$captured.arguments[2]-ne'C:/release/phases/fresh'){throw 'Controller and compilation workspace owners differ'}
[pscustomobject]@{passed=$true;actualPowerShellFunction=$true;externalCompilationExecuted=$false;arguments=$captured.arguments}|ConvertTo-Json -Depth 4
