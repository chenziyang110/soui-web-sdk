param([string]$ObserverPath=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($ObserverPath,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual LLVM observer must parse'}
foreach($name in @('Need','SameSet')){
  $definitions=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true))
  if($definitions.Count-ne1){throw ('Unique actual observer definition required: '+$name)}
  . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$message='Imported SDK archive must still equal its actually compiled input objects'
$commands=@($ast.FindAll({param($node)
  $node-is[Management.Automation.Language.CommandAst]-and$node.GetCommandName()-ceq'Need'-and
  $node.Extent.Text.EndsWith("'"+$message+"'")
},$true))
if($commands.Count-ne1){throw 'Unique actual installed archive comparison required'}
$gate=[scriptblock]::Create($commands[0].Extent.Text)
$a=[pscustomobject]@{bytes=[int64]123;sha256=('a'*64)}
$b=[pscustomobject]@{bytes=[int64]2147483648;sha256=('b'*64)}
function Check([object[]]$members,[object[]]$objects,[bool]$expected){
  $actual=@{members=$members};$archives=@(@{inputObjects=$objects})
  $accepted=$true
  try { & $gate } catch {
    if($_.Exception.Message-cne$message){throw}
    $accepted=$false
  }
  Need ($accepted-eq$expected) 'Installed archive member comparison outcome differs'
}
Check @($a,$b) @($a,$b) $true
Check @($b,$a) @($a,$b) $true
Check @($a,$b) @($a,([pscustomobject]@{bytes=[int64]2147483649;sha256=('b'*64)})) $false
Check @($a,$b) @($a,([pscustomobject]@{bytes=[int64]2147483648;sha256=('c'*64)})) $false
Check @($a,$b) @($a) $false
Check @() @() $false
Check @($a,$a) @($a,$b) $false
[ordered]@{testsPassed=7;scope='Actual observer comparison with synthetic Int64 size/hash records';compilerEvidence=$false}|ConvertTo-Json -Compress
