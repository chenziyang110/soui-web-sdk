[CmdletBinding()]
param([string]$ObserverPath = (Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($ObserverPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Observer syntax error' }
foreach ($name in @('Need','LocalToolSpelling','CompileArgv','ToolNorm','BinaryCommandTools','BinaryCommandIndex')) {
  $nodes = @($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name}, $true))
  if ($nodes.Count -ne 1) { throw "Expected one actual observer function: $name" }
  . ([scriptblock]::Create($nodes[0].Extent.Text))
}
function Check([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
$aliases = @{'c:/short/link.exe'='c:/tools/link.exe'; 'c:/tools/link.exe'='c:/tools/link.exe'}
$command = '"C:\short\LINK.EXE" /DCase=Mixed /out:Release/App.exe'
$canonical = '"c:/tools/link.exe" /DCase=Mixed /out:Release/App.exe'
$differentCase = '"c:/tools/link.exe" /DCase=mixed /out:Release/App.exe'
$unproven = '"C:/unproven/link.exe" /DCase=Mixed /out:Release/App.exe'
$lookalike = '"C:/short/link.exe.extra" /DCase=Mixed /out:Release/App.exe'
$generated = [string[]]@($command, $command, $differentCase, $unproven, $lookalike)
$verbose = [string[]]@('unrelated compiler diagnostic', $canonical, $command, $differentCase)
$index = BinaryCommandIndex $generated $verbose
Check ($index.generated.Count -eq 5) 'Duplicate generated commands must remain ambiguous'
Check ($index.generated[0].raw -ceq $command -and $index.generated[0].canonical -ceq $canonical) 'Keep raw commands and authenticated tool spelling'
Check ($index.generated[0].search -ceq $canonical.ToLowerInvariant()) 'Output/token search stays case insensitive'
Check ($index.generated[3].canonical -ceq $unproven) 'Unproven tool paths must not collapse'
Check ($index.generated[4].canonical -ceq $lookalike) 'Alias token boundaries must remain intact'
Check ($index.actual[$canonical].Count -eq 2) 'Duplicate actual commands must remain detectable'
Check ($index.actual[$canonical][0] -ceq $canonical -and $index.actual[$canonical][1] -ceq $command) 'Preserve actual verbose bytes and order'
Check ($index.actual[$differentCase].Count -eq 1) 'Argument case must distinguish actual commands'
Check (-not $index.actual.ContainsKey($canonical.ToLowerInvariant())) 'Actual lookup must use ordinal comparison'
Check (-not $index.actual.ContainsKey($unproven)) 'Missing actual commands must not acquire evidence'
$empty = BinaryCommandIndex @() @()
Check ($empty.generated.Count -eq 0 -and $empty.actual.Count -eq 0) 'Empty inputs remain empty'
# Count normalization work deterministically; wall-clock limits would be flaky.
$realNorm = ${function:ToolNorm}; $script:normalizations = 0
function ToolNorm([string]$text) { $script:normalizations++; & $realNorm $text }
$index = BinaryCommandIndex $generated $verbose
for ($i=0; $i -lt 200; $i++) { $null = $index.actual[$canonical]; $null = $index.generated[0].canonical }
Check ($script:normalizations -eq ($generated.Count + $verbose.Count)) 'Normalize each log/graph row once, independent of binary count'
Write-Output 'Binary command index: 12 checks passed'
& (Join-Path $PSScriptRoot 'llvm-binary-alias.Tests.ps1') -ObserverPath $ObserverPath
