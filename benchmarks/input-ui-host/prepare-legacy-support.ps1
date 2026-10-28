param(
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedSourceCommit,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
$inputUiSource=[IO.Path]::GetFullPath($SourceRoot)
$inputUiOutput=[IO.Path]::GetFullPath($OutputRoot)
if(-not [IO.Path]::IsPathRooted($OutputRoot) -or (Test-Path -LiteralPath $inputUiOutput)) {
  throw 'Legacy measurement support requires a new absolute output directory.'
}
$inputUiRevision=(& git -C $inputUiSource rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $inputUiRevision -ne $ExpectedSourceCommit) { throw 'Measurement support source commit differs.' }
$inputUiDirty=& git -C $inputUiSource status --porcelain
if($LASTEXITCODE -ne 0 -or $inputUiDirty) { throw 'Freeze and commit measurement support source before copying.' }
$inputUiFiles=@(
  'sdk/include/soui/diagnostics/input_frame_trace.h',
  'sdk/include/soui/diagnostics/miniblink_trace_source.h',
  'sdk/src/runtime/diagnostics/input_frame_trace.cpp',
  'sdk/src/runtime/diagnostics/input_frame_trace_backend_validation.h',
  'sdk/src/runtime/diagnostics/miniblink_trace_source.cpp',
  'third_party/include/miniblink/input_ui_api.h'
)
foreach($inputUiRelative in $inputUiFiles) {
  if(-not (Test-Path -LiteralPath (Join-Path $inputUiSource $inputUiRelative) -PathType Leaf)) { throw 'Missing diagnostic support source.' }
}
[IO.Directory]::CreateDirectory($inputUiOutput) | Out-Null
$inputUiManifest=@()
foreach($inputUiRelative in $inputUiFiles) {
  $inputUiFrom=Join-Path $inputUiSource $inputUiRelative
  $inputUiTo=Join-Path $inputUiOutput $inputUiRelative
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($inputUiTo)) | Out-Null
  Copy-Item -LiteralPath $inputUiFrom -Destination $inputUiTo
  $inputUiHash=(Get-FileHash -LiteralPath $inputUiTo -Algorithm SHA256).Hash.ToLowerInvariant()
  if($inputUiHash -ne (Get-FileHash -LiteralPath $inputUiFrom -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'Copied diagnostic support differs.' }
  $inputUiManifest+=@{path=$inputUiRelative;bytes=(Get-Item -LiteralPath $inputUiTo).Length;sha256=$inputUiHash}
}
$inputUiFinalRevision=(& git -C $inputUiSource rev-parse HEAD).Trim()
$inputUiFinalRevisionExit=$LASTEXITCODE
$inputUiFinalDirty=& git -C $inputUiSource status --porcelain
if($inputUiFinalRevisionExit -ne 0 -or $LASTEXITCODE -ne 0 -or $inputUiFinalRevision -ne $ExpectedSourceCommit -or $inputUiFinalDirty) {
  throw 'Diagnostic support source changed during copy.'
}
$inputUiRecord=@{schema='soui.input-ui-legacy-support/v1';source_commit=$ExpectedSourceCommit;files=$inputUiManifest}
[IO.File]::WriteAllText((Join-Path $inputUiOutput 'support-manifest.json'),($inputUiRecord | ConvertTo-Json -Depth 6)+"`n",[Text.UTF8Encoding]::new($false))
