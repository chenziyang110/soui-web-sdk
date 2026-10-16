param(
 [Parameter(Mandatory)][string]$OutputPath,
 [Parameter(Mandatory)][string]$BeforeObserverPath,
 [string]$ObserverPath=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))),
 [string]$FixturePath=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../fixtures/llvm-crt/actual-release-command.json')))
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $OutputPath){throw 'Fresh CRT test result required'}
function SourceAst([string]$path){
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw 'The actual Observer source has parse errors'}
 return $ast
}
$ast=SourceAst $ObserverPath
# Evaluate only definitions from the real Observer. Its executable body,
# tool identity helper and Ninja queries are never invoked by this test.
foreach($name in @('Need','Norm','ToolNorm','CompileArgv','CompileNorm','CompileVerboseNorm','Test-CrtOption')){
 $definitions=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true))
 if($definitions.Count-ne1){throw ('Unique actual production function required: '+$name)}
 . ([ScriptBlock]::Create($definitions[0].Extent.Text))
}
function SdkGate($sourceAst){
 # RuntimeDoctor has a separate CRT branch before the SDK elseif. Select the
 # actual SDK condition, never the first clause or a surrounding statement.
 $matches=@($sourceAst.FindAll({param($node)$node-is[Management.Automation.Language.IfStatementAst]},$true)|ForEach-Object{
  foreach($clause in $_.Clauses){
   if($clause.Item1.Extent.Text-ceq'$isSdk'-and$clause.Item2.Extent.Text.Contains("'SDK target CRT/Unicode OS macro contract'")){$clause.Item2}
  }
 })
 if($matches.Count-ne1){throw 'Unique actual SDK gate required'}
 $statements=@($matches[0].Statements)
 if($statements.Count-ne1){throw 'The actual SDK gate must be one Need statement'}
 return [ScriptBlock]::Create($statements[0].Extent.Text)
}
$beforeGate=SdkGate (SourceAst $BeforeObserverPath)
$candidateGate=SdkGate $ast
$aliases=@{} # This pure regression supplies no physical alias proof.
$fixture=Get-Content -LiteralPath $FixturePath -Raw|ConvertFrom-Json
if($fixture.schema-cne'soui.actual-normal-release-crt-fixture/v1'-or$fixture.configuration-cne'Release'){throw 'Expected the captured closed Release fixture'}
$results=[Collections.Generic.List[object]]::new()
function Case([string]$name,[scriptblock]$work,[string]$rejection=''){
 $message=$null
 try{&$work|Out-Null}catch{$message=$_.Exception.Message}
 if($rejection){if(-not$message-or-not$message.Contains($rejection)){throw ('Wrong rejection: '+$name+' '+$message)}}
 elseif($message){throw ('Positive case failed: '+$name+' '+$message)}
 $results.Add([ordered]@{name=$name;passed=$true;rejection=$message})
}
function AssertCrt([string[]]$options,[string]$expected){Need (Test-CrtOption -Arguments $options -ExpectedCRT $expected) 'CRT contract rejected'}
function SetFlags([string]$command){
 $script:flagArgs=[string[]]@(CompileArgv $command)
 $script:flags=Norm ($script:flagArgs-join' ')
}
Case 'SDK-selector-rejects-doctor-only-message' {
 SdkGate ([ScriptBlock]::Create("if(`$isRuntimeDoctor){Need `$true 'SDK target CRT/Unicode OS macro contract'} elseif(`$isSdk){Need `$true 'another contract'}").Ast)
} 'Unique actual SDK gate required'
Case 'SDK-selector-rejects-duplicate-conditions' {
 SdkGate ([ScriptBlock]::Create("if(`$isSdk){Need `$true 'SDK target CRT/Unicode OS macro contract'}; if(`$isSdk){Need `$true 'SDK target CRT/Unicode OS macro contract'}").Ast)
} 'Unique actual SDK gate required'
Case 'SDK-selector-rejects-additional-statements' {
 SdkGate ([ScriptBlock]::Create("if(`$isSdk){Need `$true 'SDK target CRT/Unicode OS macro contract'; Write-Output 'unexpected'}").Ast)
} 'The actual SDK gate must be one Need statement'
Case 'captured-normal-before-rejects-real-dash-MD' {SetFlags $fixture.databaseRecord.command;&$beforeGate} 'SDK target CRT/Unicode OS macro contract'
Case 'captured-normal-after-accepts-real-dash-MD' {SetFlags $fixture.databaseRecord.command;&$candidateGate}
Case 'captured-query-verbose-order-and-showIncludes-unchanged' {Need ((CompileNorm $fixture.databaseRecord.command)-ceq(CompileVerboseNorm $fixture.actualVerboseLine)) 'Captured actual command differs'}
foreach($token in @('/MD','-MD','/md','-mD')){
 Case ('MD-explicit-policy-'+$token) {AssertCrt @('clang-cl.exe',$token,'-c','a.cpp') 'MD'}
}
foreach($token in @('/MDd','-MDd','/mdd','-mDd')){
 Case ('MDd-explicit-policy-'+$token) {AssertCrt @('clang-cl.exe',$token,'-c','a.cpp') 'MDd'}
}
Case 'MDd-rejected-when-policy-is-MD' {AssertCrt @('-MDd') 'MD'} 'CRT contract rejected'
Case 'MD-rejected-when-policy-is-MDd' {AssertCrt @('/MD') 'MDd'} 'CRT contract rejected'
foreach($token in @('/MT','-MT','/MTd','-MTd')){
 Case ('static-CRT-rejected-'+$token) {AssertCrt @($token) 'MD'} 'CRT contract rejected'
}
Case 'duplicate-identical-selection-rejected' {AssertCrt @('/MD','/MD') 'MD'} 'CRT contract rejected'
Case 'duplicate-prefix-case-variant-rejected' {AssertCrt @('/MD','-md') 'MD'} 'CRT contract rejected'
Case 'conflicting-static-selection-rejected' {AssertCrt @('/MD','-MT') 'MD'} 'CRT contract rejected'
Case 'conflicting-debug-selection-rejected' {AssertCrt @('-MD','/MDd') 'MD'} 'CRT contract rejected'
Case 'duplicate-debug-selection-rejected' {AssertCrt @('/MDd','-mdd') 'MDd'} 'CRT contract rejected'
foreach($token in @('prefix-MD','/MDsuffix','-MD=1','/DNAME=-MD','/FoC:/MD/file.obj','-MD /MT')){
 Case ('non-token-does-not-select-CRT-'+$token) {AssertCrt @($token) 'MD'} 'CRT contract rejected'
}
Case 'missing-selection-rejected' {AssertCrt @('clang-cl.exe','-c','a.cpp') 'MD'} 'CRT contract rejected'
Case 'empty-array-rejected' {AssertCrt @() 'MD'} 'CRT contract rejected'
Case 'filename-containing-MD-does-not-become-a-second-selection' {AssertCrt @('-MD','/FoC:/MD/file.obj') 'MD'}
Case 'one-quoted-embedded-argument-does-not-select-CRT' {AssertCrt @(CompileArgv 'clang-cl.exe "-MD /MT" -c a.cpp') 'MD'} 'CRT contract rejected'
Case 'standalone-quoted-MD-is-a-real-CRT-argument' {AssertCrt @(CompileArgv 'clang-cl.exe "-MD" -c a.cpp') 'MD'}
foreach($option in @('/U_DEBUG','-DWINVER=0x0601','-D_WIN32_WINNT=0x0601')){
 Case ('unchanged-SDK-macro-rejected-'+$option) {
  SetFlags $fixture.databaseRecord.command
  $script:flagArgs=[string[]]@($script:flagArgs|Where-Object {$_-ine$option-and$_-ine$option.Replace('-D','/D')})
  $script:flags=Norm ($script:flagArgs-join' ')
  &$candidateGate
 } 'SDK target CRT/Unicode OS macro contract'
}
Case 'wrong-OS-value-rejected' {SetFlags $fixture.databaseRecord.command.Replace('WINVER=0x0601','WINVER=0x0602');&$candidateGate} 'SDK target CRT/Unicode OS macro contract'
Case 'compiler-command-CRT-binding-remains-exact' {Need ((CompileNorm $fixture.databaseRecord.command)-cne(CompileNorm ($fixture.databaseRecord.command.Replace(' -MD ',' /MD ')))) 'Different original argv must remain different'}
$report=[ordered]@{schema='soui.observer-crt-pure-regression/v1';passed=$true;count=$results.Count;tests=$results.ToArray();observerSource=$ObserverPath;beforeObserverSource=$BeforeObserverPath;fixture=$FixturePath;actualWholeObserverExecuted=$false;actualToolFileIdentityExecuted=$false;nativeProgramsExecuted=$false;normalSdkAccepted=$false;qualificationPassed=$false}
[IO.File]::WriteAllText($OutputPath,($report|ConvertTo-Json -Depth 12)+"`n",[Text.UTF8Encoding]::new($false))
$report|ConvertTo-Json -Depth 12
