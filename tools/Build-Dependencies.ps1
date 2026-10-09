[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('All','Miniblink','SOUI')][string]$Component='All',
  [string]$OutputRoot='',
  [switch]$EnableExperimentalGpu
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$toolchain=Get-SouiToolchain
$node=(Get-Command node.exe -ErrorAction Stop).Source
$output=Join-Path $root "out/dependencies/$Architecture"
if($OutputRoot) {
  $outputProvider=$null
  $outputDrive=$null
  $output=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot,[ref]$outputProvider,[ref]$outputDrive)
  if($outputProvider.Name -ne 'FileSystem'){throw 'OutputRoot must be a filesystem directory.'}
}
if($Component -in @('All','SOUI')) {
  $allowedOutput=[IO.Path]::GetFullPath((Join-Path $root 'out')).TrimEnd('\','/')
  if(-not $output.Equals($allowedOutput,[StringComparison]::OrdinalIgnoreCase) -and
      -not $output.StartsWith($allowedOutput+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'SOUI source staging requires OutputRoot inside this SDK out directory; use Component Miniblink for other output locations.'
  }
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$start=Get-Date
function Assert-SourceEncoding([string]$LogPath) {
  if(Select-String -LiteralPath $LogPath -Pattern 'warning C4819','warning C4828' -Quiet){
    throw "Source encoding warning; see $LogPath"
  }
}
if($Component -in @('All','Miniblink')) {
  $source=Join-Path $root 'third_party/sources/miniblink'
  if(-not(Test-Path -LiteralPath (Join-Path $source 'SOURCE-MANIFEST.json'))){throw 'Miniblink source bundle is required.'}
  $deps=Join-Path $output 'libraries'
  New-Item -ItemType Directory -Force -Path $deps | Out-Null
  foreach($library in @('zlib','libjpeg','libpng','harfbuzz','libxml','ots','openssl','libcurl','v8_5_7','v8_5_7_1','skia')) {
    Write-Output "Building Miniblink dependency: $library / $Architecture"
    $logPath=Join-Path $output "$library.log"
    & (Join-Path $PSScriptRoot 'internal/Build-KernelLibraryArch.cmd') $toolchain.DevCmd $source $deps $library $Architecture *> $logPath
    if($LASTEXITCODE -ne 0){throw "Dependency failed: $library; see $output/$library.log"}
    Assert-SourceEncoding $logPath
  }
  # A nonempty sentinel also survives Windows PowerShell 5 native arguments.
  $gpuLibraries='none'
  if($EnableExperimentalGpu) {
    # A fresh GPU output prevents stale objects after source files are removed.
    $gpu=Join-Path $output ('gpu-'+[Guid]::NewGuid().ToString('N'))
    $gpuLog=Join-Path $output 'skia-gpu.log'
    & (Join-Path $PSScriptRoot 'internal/Build-SkiaGpuLibrary.ps1') -VsDevCmd $toolchain.DevCmd -SourceRoot $source -OutputRoot $gpu -Architecture $Architecture *> $gpuLog
    if($LASTEXITCODE -ne 0){throw "Skia GPU failed; see $gpuLog"}
    Assert-SourceEncoding $gpuLog
    $gpuLibraries=Join-Path $gpu 'lib'
  }
  Write-Output "Building Miniblink kernel / $Architecture"
  $kernel=Join-Path $output 'miniblink'
  New-Item -ItemType Directory -Force -Path $kernel | Out-Null
  $kernelLog=Join-Path $output 'miniblink.log'
  & (Join-Path $PSScriptRoot 'internal/Build-KernelMainArch.cmd') $toolchain.DevCmd $source $kernel (Join-Path $deps 'lib') $gpuLibraries $Architecture *> $kernelLog
  if($LASTEXITCODE -ne 0){throw "Miniblink failed; see $output/miniblink.log"}
  Assert-SourceEncoding $kernelLog
}
$souiSourceStage=$null
if($Component -in @('All','SOUI')) {
  $souiSourceStage=Join-Path $output ('soui-source-'+[Guid]::NewGuid().ToString('N'))
  $stageOutput=@(& $node (Join-Path $PSScriptRoot 'Stage-SouiSource.cjs') $root $souiSourceStage)
  if($LASTEXITCODE -ne 0 -or $stageOutput.Count -ne 1 -or $stageOutput[0] -ne $souiSourceStage){throw 'Failed to stage SOUI source outside the delivery tree.'}
  $source=$souiSourceStage
  if(-not(Test-Path -LiteralPath (Join-Path $source 'SOURCE-MANIFEST.json'))){throw 'SOUI source bundle is required.'}
  $suffix=if($Architecture -eq 'x64'){'64'}else{''}
  foreach($componentPath in @("utilities/utilities$suffix.vcxproj","SOUI/soui$suffix.vcxproj",
      "components/render-gdi/render-gdi$suffix.vcxproj","components/imgdecoder-gdip/imgdecoder-gdip$suffix.vcxproj",
      "soui-sys-resource/soui-sys-resource$suffix.vcxproj")) {
    $name=[IO.Path]::GetFileNameWithoutExtension($componentPath)
    Write-Output "Building SOUI component: $name / $Architecture"
    $logPath=Join-Path $output "$name.log"
    & (Join-Path $PSScriptRoot 'internal/Build-SouiComponent.cmd') $toolchain.DevCmd (Join-Path $source $componentPath) $Architecture (Join-Path $output "soui/obj/$name") *> $logPath
    if($LASTEXITCODE -ne 0){throw "SOUI component failed: $name; see $output/$name.log"}
    Assert-SourceEncoding $logPath
  }
}
$artifacts=@()
$kernelName=if($Architecture -eq 'x86'){'node.dll'}else{'miniblink.dll'}
$kernel=Join-Path $output "miniblink/bin/$kernelName"
if($Component -in @('All','Miniblink') -and -not(Test-Path -LiteralPath $kernel)){throw "Kernel build returned without expected artifact: $kernel"}
if(Test-Path -LiteralPath $kernel){$artifacts+=@{kind='miniblink';path=$kernel;sha256=(Get-FileHash -LiteralPath $kernel).Hash}}
$souiBin=if($souiSourceStage){Join-Path $souiSourceStage "bin$(if($Architecture -eq 'x64'){'64'})"}else{$null}
if($souiBin){
  foreach($name in @('soui','utilities','render-gdi','imgdecoder-gdip','soui-sys-resource')){
    $file=Join-Path $souiBin "$name.dll"
    if(-not(Test-Path -LiteralPath $file)){throw "SOUI build returned without expected artifact: $file"}
    $artifacts+=@{kind=$name;path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash}
  }
}
$report=[ordered]@{schema='soui.dependency-build/v1';architecture=$Architecture;component=$Component;experimentalGpu=$EnableExperimentalGpu.IsPresent;startedAt=$start.ToString('o');finishedAt=(Get-Date).ToString('o');status='built';releaseReady=$false;souiSourceStage=$souiSourceStage;artifacts=$artifacts}
[IO.File]::WriteAllText((Join-Path $output "build-$Component.json"),($report|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output "Dependency source build completed: $output"
