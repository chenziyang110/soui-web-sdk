[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$VsDevCmd,
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [ValidateSet('x86','x64')][string]$Architecture = 'x86'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $SourceRoot).Path
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
if (Test-Path -LiteralPath $out) { throw "Refusing to overwrite GPU build output: $out" }
$obj = Join-Path $out 'obj'
$lib = Join-Path $out 'lib'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
New-Item -ItemType Directory -Force -Path $lib | Out-Null
$gpu = Join-Path $root 'third_party/skia/src/gpu'
$sources = @(Get-ChildItem -LiteralPath $gpu -Recurse -Filter '*.cpp' | Where-Object {
  $_.FullName -notmatch '\\(glx|mesa|egl|android|iOS|mac|angle|debug|nacl)\\' -and
  $_.Name -notmatch '(_none|_egl|_glx|_android|_iOS|_mac)\.cpp$'
})
$sources += @(Get-ChildItem -LiteralPath (Join-Path $gpu 'gl/win') -Filter '*.cpp')
$sources += @(Get-ChildItem -LiteralPath (Join-Path $root 'third_party/skia/src/utils') -Filter 'SkTextureCompressor*.cpp')
$sources += @(Get-Item (Join-Path $root 'third_party/skia/third_party/ktx/ktx.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/third_party/etc1/etc1.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/src/opts/SkTextureCompression_opts_none.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/src/core/SkRecord.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/src/core/SkRecordDraw.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/src/core/SkRecordOpts.cpp'))
$sources += @(Get-Item (Join-Path $root 'third_party/skia/src/core/SkRecorder.cpp'))
$sources = @($sources | Sort-Object FullName -Unique)
if ($sources.Count -lt 150) { throw "Unexpectedly small Skia GPU source set: $($sources.Count)" }
$sourceRsp = Join-Path $out 'gpu-sources.rsp'
$sources | ForEach-Object { '"' + $_.FullName + '"' } | Set-Content -LiteralPath $sourceRsp -Encoding ascii
$include = @(
  (Join-Path $root 'third_party/skia/include/gpu'),
  (Join-Path $root 'third_party/skia/include/gpu/gl'),
  (Join-Path $root 'third_party/skia/include/core'),
  (Join-Path $root 'third_party/skia/include/config'),
  (Join-Path $root 'third_party/skia/include/effects'),
  (Join-Path $root 'third_party/skia/include/utils'),
  (Join-Path $root 'third_party/skia/src/gpu'),
  (Join-Path $root 'third_party/skia/src/gpu/gl'),
  (Join-Path $root 'third_party/skia/src/gpu/gl/win'),
  (Join-Path $root 'third_party/skia/src/core'),
  (Join-Path $root 'third_party/skia/src/effects'),
  (Join-Path $root 'third_party/skia/src/image'),
  (Join-Path $root 'third_party/skia/src/utils'),
  (Join-Path $root 'third_party/skia/src/opts'),
  (Join-Path $root 'third_party'),
  (Join-Path $root 'third_party/khronos'),
  (Join-Path $root 'third_party/zlib'),
  (Join-Path $root 'third_party/skia/third_party/ktx'),
  (Join-Path $root 'third_party/skia/third_party/etc1'),
  $root
)
$includeArgs = ($include | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
$defines = '/DSK_SUPPORT_GPU=1 /DGR_GL_FUNCTION_TYPE=__stdcall /DWIN32 /DNDEBUG /D_LIB /DSK_GAMMA_SRGB /DSK_GAMMA_CONTRAST=0.5'
$buildCmd = Join-Path $out 'build-gpu.cmd'
$clObj = ($obj -replace '/', '\') + '/'
$rsp = $sourceRsp -replace '/', '\'
$libOut = (Join-Path $lib 'skia_gpu.lib') -replace '/', '\'
$objWin = $obj -replace '/', '\'
@"
@echo off
setlocal
call "$VsDevCmd" -no_logo -arch=$Architecture -host_arch=x64
if errorlevel 1 exit /b 65
cl.exe /nologo /c /EHsc /MT /O2 /MP2 /bigobj $defines $includeArgs /Fo"$clObj" @$rsp
if errorlevel 1 exit /b %errorlevel%
lib.exe /nologo /OUT:"$libOut" "$objWin\*.obj"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $buildCmd -Encoding ascii
& cmd.exe /d /c $buildCmd
if ($LASTEXITCODE -ne 0) { throw "Skia GPU library build failed: $LASTEXITCODE" }
@{
  sourceCount = $sources.Count
  sourceHash = (Get-FileHash -LiteralPath $sourceRsp).Hash
  library = Get-FileHash -LiteralPath (Join-Path $lib 'skia_gpu.lib')
  scope = 'Compile-only GPU Ganesh/WGL source inventory; not yet integrated into Miniblink or a window.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'identity.json')
Write-Output "Skia GPU library built: $($sources.Count) source files; $libOut"
