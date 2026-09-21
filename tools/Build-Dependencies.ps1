[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('All','Miniblink','SOUI')][string]$Component='All'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$toolchain=Get-SouiToolchain
$output=Join-Path $root "out/dependencies/$Architecture"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$start=Get-Date
if($Component -in @('All','Miniblink')) {
  $source=Join-Path $root 'third_party/sources/miniblink'
  if(-not(Test-Path -LiteralPath (Join-Path $source 'SOURCE-MANIFEST.json'))){throw 'Miniblink source bundle is required.'}
  $deps=Join-Path $output 'libraries'
  New-Item -ItemType Directory -Force -Path $deps | Out-Null
  foreach($library in @('zlib','libjpeg','libpng','harfbuzz','libxml','ots','openssl','libcurl','v8_5_7','v8_5_7_1','skia')) {
    Write-Output "Building Miniblink dependency: $library / $Architecture"
    & (Join-Path $PSScriptRoot 'internal/Build-KernelLibraryArch.cmd') $toolchain.DevCmd $source $deps $library $Architecture *> (Join-Path $output "$library.log")
    if($LASTEXITCODE -ne 0){throw "Dependency failed: $library; see $output/$library.log"}
  }
  # A fresh GPU output prevents stale objects after source files are removed.
  $gpu=Join-Path $output ('gpu-'+[Guid]::NewGuid().ToString('N'))
  & (Join-Path $PSScriptRoot 'internal/Build-SkiaGpuLibrary.ps1') -VsDevCmd $toolchain.DevCmd -SourceRoot $source -OutputRoot $gpu -Architecture $Architecture *> (Join-Path $output 'skia-gpu.log')
  Write-Output "Building Miniblink kernel / $Architecture"
  $kernel=Join-Path $output 'miniblink'
  New-Item -ItemType Directory -Force -Path $kernel | Out-Null
  & (Join-Path $PSScriptRoot 'internal/Build-KernelMainArch.cmd') $toolchain.DevCmd $source $kernel (Join-Path $deps 'lib') (Join-Path $gpu 'lib') $Architecture *> (Join-Path $output 'miniblink.log')
  if($LASTEXITCODE -ne 0){throw "Miniblink failed; see $output/miniblink.log"}
}
if($Component -in @('All','SOUI')) {
  $source=Join-Path $root 'third_party/sources/soui'
  if(-not(Test-Path -LiteralPath (Join-Path $source 'SOURCE-MANIFEST.json'))){throw 'SOUI source bundle is required.'}
  $suffix=if($Architecture -eq 'x64'){'64'}else{''}
  foreach($componentPath in @("utilities/utilities$suffix.vcxproj","SOUI/soui$suffix.vcxproj",
      "components/render-gdi/render-gdi$suffix.vcxproj","components/imgdecoder-gdip/imgdecoder-gdip$suffix.vcxproj",
      "soui-sys-resource/soui-sys-resource$suffix.vcxproj")) {
    $name=[IO.Path]::GetFileNameWithoutExtension($componentPath)
    Write-Output "Building SOUI component: $name / $Architecture"
    & (Join-Path $PSScriptRoot 'internal/Build-SouiComponent.cmd') $toolchain.DevCmd (Join-Path $source $componentPath) $Architecture (Join-Path $output "soui/obj/$name") *> (Join-Path $output "$name.log")
    if($LASTEXITCODE -ne 0){throw "SOUI component failed: $name; see $output/$name.log"}
  }
}
$artifacts=@()
$kernelName=if($Architecture -eq 'x86'){'node.dll'}else{'miniblink.dll'}
$kernel=Join-Path $output "miniblink/bin/$kernelName"
if($Component -in @('All','Miniblink') -and -not(Test-Path -LiteralPath $kernel)){throw "Kernel build returned without expected artifact: $kernel"}
if(Test-Path -LiteralPath $kernel){$artifacts+=@{kind='miniblink';path=$kernel;sha256=(Get-FileHash -LiteralPath $kernel).Hash}}
$souiBin=Join-Path $root "third_party/sources/soui/bin$(if($Architecture -eq 'x64'){'64'})"
foreach($name in @('soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource')){
  $file=Join-Path $souiBin "$name.dll"
  if(Test-Path -LiteralPath $file){$artifacts+=@{kind=$name;path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash}}
}
$report=[ordered]@{schema='soui.dependency-build/v1';architecture=$Architecture;component=$Component;startedAt=$start.ToString('o');finishedAt=(Get-Date).ToString('o');status='built';releaseReady=$false;artifacts=$artifacts}
[IO.File]::WriteAllText((Join-Path $output "build-$Component.json"),($report|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output "Dependency source build completed: $output"
