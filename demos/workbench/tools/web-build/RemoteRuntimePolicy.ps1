function Get-RemoteRuntimeDependencyViolations {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory = $true)][string] $Root,
    [Parameter(Mandatory = $true)][System.IO.FileInfo[]] $Files
  )

  $resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root))
  $patterns = @{
    '.html' = @(
      '(?is)\b(?:src|href|action|poster)\s*=\s*["'']\s*(?:https?:)?//',
      '(?is)\bsrcset\s*=\s*["''][^"'']*(?:https?:)?//',
      '(?is)<\s*(?:iframe|frame|object|embed)\b'
    )
    '.css' = @(
      '(?is)url\(\s*["'']?\s*(?:https?:)?//',
      '(?is)@import\s+(?:url\()?\s*["'']?\s*(?:https?:)?//'
    )
    '.js' = @(
      '(?is)\b(?:fetch|importScripts|WebSocket|EventSource|import)\s*\(\s*["'']\s*(?:https?:)?//',
      '(?is)\.open\s*\(\s*["''][A-Z]+["'']\s*,\s*["'']\s*(?:https?:)?//',
      '(?is)\.(?:src|href)\s*=\s*["'']\s*(?:https?:)?//',
      '(?is)setAttribute\s*\(\s*["''](?:src|href)["'']\s*,\s*["'']\s*(?:https?:)?//',
      '(?is)createElement\s*\(\s*["''](?:iframe|frame|object|embed)["'']'
    )
    '.mjs' = @(
      '(?is)\b(?:fetch|importScripts|WebSocket|EventSource|import)\s*\(\s*["'']\s*(?:https?:)?//',
      '(?is)\.open\s*\(\s*["''][A-Z]+["'']\s*,\s*["'']\s*(?:https?:)?//',
      '(?is)\.(?:src|href)\s*=\s*["'']\s*(?:https?:)?//',
      '(?is)setAttribute\s*\(\s*["''](?:src|href)["'']\s*,\s*["'']\s*(?:https?:)?//',
      '(?is)createElement\s*\(\s*["''](?:iframe|frame|object|embed)["'']'
    )
    '.json' = @(
      '(?is)["''](?:file|src|href|url)["'']\s*:\s*["'']\s*(?:https?:)?//'
    )
  }

  $violations = New-Object System.Collections.Generic.List[string]
  foreach ($file in $Files) {
    $extension = [IO.Path]::GetExtension($file.Name).ToLowerInvariant()
    if (-not $patterns.ContainsKey($extension)) { continue }
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($pattern in $patterns[$extension]) {
      if ([regex]::IsMatch($content, $pattern)) {
        $violations.Add($file.FullName.Substring($resolvedRoot.Length).TrimStart('\', '/'))
        break
      }
    }
  }
  return @($violations)
}
