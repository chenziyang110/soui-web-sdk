[CmdletBinding()]
param([Parameter(Mandatory)][string]$SourceRoot,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
$source = 'third_party/sources/miniblink/content/web_impl_win/CurrentTimeImpl.cpp'
$originalSha = '83de811fe28ef57f6b0e0a73614ef81925c08097288be9f4de4c8eddc06c8d96'
$optimizedSha = '28e65698db58df2261a054bce73b67b86c692e21ddcaa948e9d611e72fe58495'
function Hash([byte[]]$Bytes) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose() }
}
function Find-Unique([byte[]]$Bytes,[string]$Anchor) {
  # ASCII anchors keep one character per raw byte, including CP936 comments.
  $text = [Text.Encoding]::ASCII.GetString($Bytes)
  $at = $text.IndexOf($Anchor,[StringComparison]::Ordinal)
  if ($at -lt 0 -or $text.IndexOf($Anchor,$at+1,[StringComparison]::Ordinal) -ge 0) {
    throw "Missing or ambiguous source anchor: $Anchor"
  }
  return $at
}
function Slice([byte[]]$Bytes,[int]$Start,[int]$End) {
  if ($Start -lt 0 -or $End -le $Start -or $End -gt $Bytes.Length) { throw 'Invalid source span' }
  $part = [byte[]]::new($End-$Start)
  [Array]::Copy($Bytes,$Start,$part,0,$part.Length)
  return ,$part
}
if (-not [IO.Path]::IsPathRooted($SourceRoot) -or -not [IO.Path]::IsPathRooted($OutputRoot)) {
  throw 'Source and output roots must be absolute'
}
$hexPath = Join-Path $PSScriptRoot 'CurrentTimeImpl.original.cpp.hex'
$hexBytes = [IO.File]::ReadAllBytes($hexPath)
$hex = [Text.Encoding]::ASCII.GetString($hexBytes)
if ($hex -notmatch '\A(?:[0-9a-f]{2}|\r?\n)+\z') { throw 'Original HEX fixture is malformed' }
$hex = $hex.Replace("`r",'').Replace("`n",'')
$original = [byte[]]::new($hex.Length/2)
for ($index=0; $index -lt $original.Length; ++$index) {
  $original[$index] = [Convert]::ToByte($hex.Substring($index*2,2),16)
}
if ((Hash $original) -cne $originalSha) { throw 'Original complete source SHA256 differs' }
$current = [IO.File]::ReadAllBytes((Join-Path $SourceRoot $source))
$currentSha = Hash $current
if ($currentSha -cne $originalSha -and $currentSha -cne $optimizedSha) {
  throw 'Current complete source is neither the pinned original nor the reviewed ADR0145 candidate'
}
$functionAnchor = 'double currentTimeImpl()'
$unusedAnchor = 'double currentTimeImpl_unuse()'
$originalFunction = Find-Unique $original $functionAnchor
$currentFunction = Find-Unique $current $functionAnchor
$originalUnused = Find-Unique $original $unusedAnchor
$currentUnused = Find-Unique $current $unusedAnchor
if ((Hash (Slice $original 0 $originalFunction)) -cne (Hash (Slice $current 0 $currentFunction)) -or
    (Hash (Slice $original $originalUnused $original.Length)) -cne
      (Hash (Slice $current $currentUnused $current.Length))) {
  throw 'Bytes outside currentTimeImpl changed, including the untouched legacy function'
}
if ($PSVersionTable.PSEdition -eq 'Core') { [Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance) }
$cp936 = [Text.Encoding]::GetEncoding(936,[Text.EncoderExceptionFallback]::new(),[Text.DecoderExceptionFallback]::new())
$utf8 = [Text.UTF8Encoding]::new($false,$true)
$null = [IO.Directory]::CreateDirectory($OutputRoot)
$entries = [Collections.Generic.List[object]]::new()
foreach ($entry in @(@{role='original';bytes=$original},@{role='current';bytes=$current})) {
  $whole = $entry.bytes
  $start = (Find-Unique $whole 'namespace content {') + 'namespace content {'.Length
  $end = Find-Unique $whole '} // namespace WTF'
  $raw = Slice $whole $start $end
  $text = $cp936.GetString($raw)
  if ([Convert]::ToBase64String($raw) -cne [Convert]::ToBase64String($cp936.GetBytes($text))) {
    throw 'CP936 namespace body does not round trip exactly'
  }
  $projected = $utf8.GetBytes($text)
  [IO.File]::WriteAllBytes((Join-Path $OutputRoot "$($entry.role).whole.raw"),$whole)
  [IO.File]::WriteAllBytes((Join-Path $OutputRoot "$($entry.role).raw.inc"),$raw)
  [IO.File]::WriteAllBytes((Join-Path $OutputRoot "$($entry.role).utf8.inc"),$projected)
  $entries.Add([ordered]@{role=$entry.role;source=$source;sourceSha256=(Hash $whole);
    sourceArtifact="$($entry.role).whole.raw";rawFragment="$($entry.role).raw.inc";
    utf8Fragment="$($entry.role).utf8.inc";
    startByte=$start;endByteExclusive=$end;rawFragmentSha256=(Hash $raw);
    utf8FragmentSha256=(Hash $projected);cp936RoundTripExact=$true})
}
$binding = [ordered]@{schema='current-time-hot-path-source/1';
  originalHex='tests/native/current_time_hot_path/CurrentTimeImpl.original.cpp.hex';
  originalHexSha256=(Hash $hexBytes);encodingFrom='CP936';encodingTo='UTF-8';
  coverage='exact namespace bodies compiled with deterministic Win32/CRT shims; not complete production translation units';
  entries=$entries.ToArray()}
[IO.File]::WriteAllText((Join-Path $OutputRoot 'source-binding.json'),($binding|ConvertTo-Json -Depth 8),$utf8)
