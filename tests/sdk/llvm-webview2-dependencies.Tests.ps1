[CmdletBinding()]
param(
  [string]$ObserverPath,
  [Parameter(Mandatory)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $ObserverPath) { $ObserverPath = Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1' }
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($ObserverPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Observer syntax error' }
foreach ($name in @('Need','Norm','Plain','ObsPin','MatchPin','RawText')) {
  $nodes = @($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name}, $true))
  if ($nodes.Count -ne 1) { throw "Expected one actual observer function: $name" }
  . ([scriptblock]::Create($nodes[0].Extent.Text))
}
$blocks = @($ast.FindAll({param($node)
  $node -is [Management.Automation.Language.IfStatementAst] -and
  $node.Clauses[0].Item1.Extent.Text -ceq '$cfg.withWebView2'
}, $true))
Need ($blocks.Count -eq 1) 'Expected the actual optional dependency derivation block'
$derive = [scriptblock]::Create(($blocks[0].Clauses[0].Item2.Statements | ForEach-Object { $_.Extent.Text }) -join "`n")
Need (-not (Test-Path -LiteralPath $OutputRoot)) 'Use a new output directory; retain failed fixtures'
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
function Put([string]$file, [string]$text) {
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
  [IO.File]::WriteAllText($file, $text, [Text.UTF8Encoding]::new($false))
}
function Case([string]$architecture, [string]$variant, [bool]$shared) {
  $root = Join-Path $OutputRoot ($architecture+'-'+$variant+'-'+$shared)
  $build = Join-Path $root 'out/build'
  $fixtureCache = Join-Path $root 'out/webview2-deps/1.0.0.0'
  $header = 'build/native/include/WebView2EnvironmentOptions.h'
  $loader = if ($shared) { 'WebView2Loader.dll' } else { 'WebView2LoaderStatic.lib' }
  $cache = @{SOUI_WEBVIEW2_LOADER_SHARED=$(if ($shared) { 'ON' } else { 'OFF' })}
  $entries = @('build/native/include/WebView2.h', $header,
    ('build/native/'+$architecture+'/'+$loader), 'LICENSE.txt', 'NOTICE.txt')
  $selected = if ($variant.StartsWith('loader-')) { 'build/native/'+$architecture+'/'+$loader } else { $header }
  $archive = Join-Path $fixtureCache 'microsoft.web.webview2.1.0.0.0.nupkg'
  [IO.Directory]::CreateDirectory($fixtureCache) | Out-Null
  $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($relative in $entries) {
      $text = 'authenticated bytes for '+$relative
      Put (Join-Path $fixtureCache ('package/'+$relative)) $text
      if ($variant.EndsWith('missing') -and $relative -eq $selected) { continue }
      $copies = if ($variant.EndsWith('duplicate') -and $relative -eq $selected) { 2 } else { 1 }
      for ($i=0; $i -lt $copies; $i++) {
        $entry = $zip.CreateEntry($relative); $stream = $entry.Open()
        try { $bytes = [Text.Encoding]::UTF8.GetBytes($text); $stream.Write($bytes,0,$bytes.Length) }
        finally { $stream.Dispose() }
      }
    }
  } finally { $zip.Dispose() }
  $descriptor = Join-Path $root 'sdk/renderers/webview2/dependency.json'
  Put $descriptor (@{schema='soui.optional-dependency/1'; id='Microsoft.Web.WebView2'; version='1.0.0.0'; sha256=(ObsPin $archive).sha256} | ConvertTo-Json)
  $template = Join-Path $root 'sdk/renderers/webview2/webview2_preload.h.in'
  $script = Join-Path $root 'sdk/renderers/webview2/native-preload.js'
  Put $template 'before @SOUI_WEBVIEW2_PRELOAD@ after'; Put $script 'fixed preload'
  Put (Join-Path $build 'sdk/renderers/webview2/webview2_preload.h') 'before fixed preload after'
  $allowed = @{}; foreach ($p in @($descriptor,$template,$script)) { $allowed[(Norm $p)] = ObsPin $p }
  $cfg = @{withWebView2=$true; architecture=$architecture; sdkOnly=$false}
  $derivedDependencies = [Collections.Generic.List[object]]::new()
  if ($variant.EndsWith('tampered')) { Put (Join-Path $fixtureCache ('package/'+$selected)) 'changed extracted member' }
  $failure = $null
  try { . $derive } catch { $failure = $_.Exception.Message }
  if ($variant -eq 'valid') {
    Need ($null -eq $failure) ('Valid package rejected: '+$failure)
    $key = Norm (Join-Path $fixtureCache ('package/'+$header))
    Need ($allowed.ContainsKey($key)) 'Environment options compiler header has no authenticated binding'
    Need ($derivedDependencies.Count -eq 6) 'All five package inputs and generated preload must be bound'
    [void](MatchPin $allowed[$key])
  } else {
    $expected = if ($variant.EndsWith('tampered')) { 'Optional dependency extracted member changed' } else { 'Unique authenticated optional dependency member' }
    Need ($null -ne $failure -and $failure.Contains($expected)) ('Missing exact rejection: '+$expected+'; actual='+$failure)
  }
}
$cases = @(); foreach ($architecture in @('x86','x64')) { foreach ($shared in @($false,$true)) { foreach ($variant in @('valid','missing','duplicate','tampered','loader-missing','loader-duplicate','loader-tampered')) {
  try { Case $architecture $variant $shared; $cases += @{name=$architecture+'-'+$variant+'-shared-'+$shared; passed=$true} }
  catch { $cases += @{name=$architecture+'-'+$variant+'-shared-'+$shared; passed=$false; error=$_.Exception.Message} }
} } }
$failed = @($cases | Where-Object { -not $_.passed }).Count
$report = @{passed=($failed -eq 0); total=$cases.Count; failed=$failed; cases=$cases; scope='actual observer derivation against synthetic authenticated archives; no native build'}
Put (Join-Path $OutputRoot 'result.json') ($report | ConvertTo-Json -Depth 8)
$report | ConvertTo-Json -Depth 8
if ($failed) { exit 1 }
