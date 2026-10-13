[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('All','Miniblink','SOUI')][string]$Component='All',
  [string]$OutputRoot='',
  [switch]$EnableExperimentalGpu,
  [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
  [string]$LlvmRoot,
  [string]$MsvcRoot,
  [string]$WindowsSdkRoot,
  [string]$NinjaExe,
  [string]$WindowsCmdExe,
  [string]$PowerShellExe,
  [string]$VsInstallDir
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/Find-Toolchain.ps1')
$llvm=$null
$llvmRoles=$null
$llvmArgumentVector=@()
$externalPaths=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe,$VsInstallDir)
if($NativeToolchain -eq 'MSVC143'){
  if(@($externalPaths|Where-Object{$_}).Count){throw 'LLVM paths and VsInstallDir require explicit NativeToolchain LLVM22MSVC142.'}
  $toolchain=Get-SouiToolchain
  $node=(Get-Command node.exe -ErrorAction Stop).Source
}else{
  if($Component -ne 'Miniblink' -or $EnableExperimentalGpu){throw 'LLVM dependency builds currently support only Component Miniblink, Release and GPU off.'}
  if(@($externalPaths|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count -or -not $OutputRoot){throw 'LLVM requires all six paths, VsInstallDir and an explicit fresh external OutputRoot.'}
  . (Join-Path $PSScriptRoot 'internal/Find-LlvmToolchain.ps1')
  . (Join-Path $PSScriptRoot 'internal/Kernel-LlvmInputs.ps1')
  foreach($path in $externalPaths){[void](Assert-SouiKernelPathText $path)}
  [void](Assert-SouiKernelPathText $OutputRoot)
  $discoveryArgs=@{Architecture=$Architecture;LlvmRoot=$LlvmRoot;MsvcRoot=$MsvcRoot;WindowsSdkRoot=$WindowsSdkRoot;NinjaExe=$NinjaExe;WindowsCmdExe=$WindowsCmdExe;PowerShellExe=$PowerShellExe;ToolIdentityBridge=(Join-Path $PSScriptRoot 'internal/Invoke-ToolFileIdentity.ps1');ToolIdentityHelper=(Join-Path $PSScriptRoot 'internal/Tool-FileIdentity.ps1')}
  $llvm=Get-SouiLlvmToolchain @discoveryArgs
  $llvmRoles=Get-SouiKernelVsRoles -VsInstallDir $VsInstallDir -MsvcRoot $MsvcRoot -Architecture $Architecture
  $targets=Resolve-SouiKernelPath (Join-Path $PSScriptRoot 'internal/kernel-llvm-output.targets') File
  $toolchain=[pscustomobject]@{DevCmd=$llvmRoles.roles.VCVARSALL.path}
  $llvmArgumentVector=@($NativeToolchain,$llvmRoles.vsInstallDir,$llvm.CMakeDefinitions.SOUI_LLVM_ROOT,$llvmRoles.msvcRoot,$llvm.CMakeDefinitions.SOUI_WINDOWS_SDK_ROOT,$targets)
}

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

$llvmContext=$null
$llvmSourceBefore=$null
$llvmInputPins=@()
$llvmLibraries=@()
$comspecSaved=$null
$llvmSavedEnvironment=@{}
$llvmCompilerEnvironment=@('CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH','LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR','EXTERNAL_INCLUDE','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS','RCFLAGS')
if($llvm){
  $source=Resolve-SouiKernelPath (Join-Path $root 'third_party/sources/miniblink') Directory
  $root=Resolve-SouiKernelPath $root Directory
  $output=Assert-SouiKernelFreshOutput -Output $output -ProtectedRoots @($root,$source,$llvmRoles.vsInstallDir,$llvm.CMakeDefinitions.SOUI_LLVM_ROOT,$llvmRoles.msvcRoot,$llvm.CMakeDefinitions.SOUI_WINDOWS_SDK_ROOT,([IO.Path]::GetDirectoryName($NinjaExe)),([IO.Path]::GetDirectoryName($WindowsCmdExe)),([IO.Path]::GetDirectoryName($PowerShellExe)))
  $forcedSymbols=Get-SouiKernelV8ForcedSymbols $source
  $llvmSourceBefore=Get-SouiKernelSourceSnapshot $source
  foreach($row in $forcedSymbols.projects){$llvmInputPins+=$row.projectPin}
  foreach($pin in $llvm.ToolPins.Values){$llvmInputPins+=$pin}
  $llvmInputPins+=@($llvm.RequiredInputPins)+@($llvm.BridgePins)
  foreach($pin in $llvmRoles.roles.Values){$llvmInputPins+=$pin}
  foreach($relative in @('Build-Dependencies.ps1','internal/Build-KernelLibraryArch.cmd','internal/Build-KernelMainArch.cmd','internal/kernel-llvm-output.targets','internal/Kernel-LlvmInputs.ps1','internal/Find-LlvmToolchain.ps1','internal/Find-Toolchain.ps1')){
    $recipePath=Resolve-SouiKernelPath (Join-Path $PSScriptRoot $relative) File
    $llvmInputPins+=Get-SouiKernelPin $recipePath
  }
  $llvmContext=[ordered]@{schema='soui.public-kernel-llvm-inputs/v1';profile=$NativeToolchain;architecture=$Architecture;sourceRoot=$source;outputRoot=$output;buildSystem='MSBuild';discoveryPurpose='file-input identity only; CMake generator fields are unvalidated and unused here';discovery=$llvm;vsOwner=$llvmRoles;inputPins=$llvmInputPins;forcedSymbols=$forcedSymbols;compilerExecutionVerified=$false;wholeToolInventoryVerified=$false;qualified=$false}
}

if($llvm){
  New-Item -ItemType Directory -Path $output -ErrorAction Stop | Out-Null
  [void](Resolve-SouiKernelPath $output Directory)
}else{New-Item -ItemType Directory -Force -Path $output | Out-Null}
$start=Get-Date
function Assert-SourceEncoding([string]$LogPath) {
  if(Select-String -LiteralPath $LogPath -Pattern 'warning C4819','warning C4828' -Quiet){
    throw "Source encoding warning; see $LogPath"
  }
}

if($llvm){
  $llvmBeforePin=Write-SouiKernelData (Join-Path $output 'kernel-source-before.json') $llvmSourceBefore
  $llvmContextPin=Write-SouiKernelData (Join-Path $output 'kernel-llvm-inputs.json') $llvmContext
  $savedItem=Get-Item -LiteralPath Env:ComSpec -ErrorAction SilentlyContinue
  $comspecSaved=@{present=($null -ne $savedItem);value=if($null -ne $savedItem){[string]$savedItem.Value}else{$null}}
}
try {

if($llvm){
  $env:ComSpec=$llvm.NativeProcessPaths.CMD
  foreach($name in $llvmCompilerEnvironment){
    $item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    $llvmSavedEnvironment[$name]=@{present=($null -ne $item);value=if($null -ne $item){[string]$item.Value}else{$null}}
    Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue
    if(Test-Path -LiteralPath ('Env:'+$name)){throw "Could not clear compiler provider: $name"}
  }
  Assert-SouiKernelPins $llvmInputPins
  $versionExeBefore=Get-SouiKernelPin $llvm.ToolPins.CLANG.path
  $versionStdout=Join-Path $output 'clang-version.stdout.raw.log'
  $versionStderr=Join-Path $output 'clang-version.stderr.raw.log'
  $versionProcess=[Diagnostics.Process]::new()
  $versionProcess.StartInfo=[Diagnostics.ProcessStartInfo]::new()
  $versionProcess.StartInfo.FileName=$llvm.ToolPins.CLANG.path
  $versionProcess.StartInfo.Arguments='--no-default-config --version'
  $versionProcess.StartInfo.UseShellExecute=$false
  $versionProcess.StartInfo.CreateNoWindow=$true
  $versionProcess.StartInfo.RedirectStandardOutput=$true
  $versionProcess.StartInfo.RedirectStandardError=$true
  $versionProcess.StartInfo.RedirectStandardInput=$true
  $stdoutStream=$null;$stderrStream=$null
  try{
    $stdoutStream=[IO.File]::Open($versionStdout,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $stderrStream=[IO.File]::Open($versionStderr,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    if(-not $versionProcess.Start()){throw 'Could not start the selected compiler version preflight.'}
    $versionProcess.StandardInput.Close()
    # Two independent BaseStreams retain native bytes. The process inherits the
    # invoking Root Owned job; this preflight adds no kill/timeout/controller.
    $stdoutCopy=$versionProcess.StandardOutput.BaseStream.CopyToAsync($stdoutStream)
    $stderrCopy=$versionProcess.StandardError.BaseStream.CopyToAsync($stderrStream)
    $versionProcess.WaitForExit()
    $stdoutCopy.GetAwaiter().GetResult()
    $stderrCopy.GetAwaiter().GetResult()
    $versionExit=$versionProcess.ExitCode
  }finally{
    if($null -ne $stdoutStream){$stdoutStream.Dispose()}
    if($null -ne $stderrStream){$stderrStream.Dispose()}
    $versionProcess.Dispose()
  }
  Assert-SouiKernelPins @($versionExeBefore)
  $versionExeAfter=Get-SouiKernelPin $llvm.ToolPins.CLANG.path
  if((Get-Item -LiteralPath $versionStdout).Length -gt 32768 -or (Get-Item -LiteralPath $versionStderr).Length -gt 32768){throw 'The selected clang version response exceeded its finite output bound.'}
  $versionText=[Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($versionStdout))
  $observedClangVersion=Get-SouiKernelClangVersion -Text $versionText -ExitCode $versionExit
  $llvmVersionStdoutPin=Get-SouiKernelPin $versionStdout
  $llvmVersionStderrPin=Get-SouiKernelPin $versionStderr
}
if($Component -in @('All','Miniblink')) {
  $source=Join-Path $root 'third_party/sources/miniblink'
  if(-not(Test-Path -LiteralPath (Join-Path $source 'SOURCE-MANIFEST.json'))){throw 'Miniblink source bundle is required.'}
  $deps=Join-Path $output 'libraries'
  New-Item -ItemType Directory -Force -Path $deps | Out-Null
  foreach($library in @('zlib','libjpeg','libpng','harfbuzz','libxml','ots','openssl','libcurl','v8_5_7','v8_5_7_1','skia')) {
    Write-Output "Building Miniblink dependency: $library / $Architecture"
    $logPath=Join-Path $output "$library.log"
    if($llvm){Assert-SouiKernelPins $llvmInputPins; New-Item -ItemType Directory -Force -Path (Join-Path $deps ("llvm-temp/"+$library)),(Join-Path $deps "empty-user-props") | Out-Null}
    if($llvm){
    & (Join-Path $PSScriptRoot 'internal/Build-KernelLibraryArch.cmd') $toolchain.DevCmd $source $deps $library $Architecture @llvmArgumentVector *> $logPath
    }else{
    & (Join-Path $PSScriptRoot 'internal/Build-KernelLibraryArch.cmd') $toolchain.DevCmd $source $deps $library $Architecture *> $logPath
    }
    if($LASTEXITCODE -ne 0){throw "Dependency failed: $library; see $output/$library.log"}
    Assert-SourceEncoding $logPath
    if($llvm){$llvmLibraries+=Get-SouiKernelPin (Join-Path $deps ("lib/"+$library+".lib"))}
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
  if($llvm){
    Assert-SouiKernelPins $llvmInputPins; Assert-SouiKernelPins $llvmLibraries
    Assert-SouiKernelPins @($llvmBeforePin,$llvmContextPin,$llvmVersionStdoutPin,$llvmVersionStderrPin)
    New-Item -ItemType Directory -Force -Path (Join-Path $kernel "llvm-temp"),(Join-Path $kernel "empty-user-props") | Out-Null
  & (Join-Path $PSScriptRoot 'internal/Build-KernelMainArch.cmd') $toolchain.DevCmd $source $kernel (Join-Path $deps 'lib') $gpuLibraries $Architecture @llvmArgumentVector *> $kernelLog
  }else{
  & (Join-Path $PSScriptRoot 'internal/Build-KernelMainArch.cmd') $toolchain.DevCmd $source $kernel (Join-Path $deps 'lib') $gpuLibraries $Architecture *> $kernelLog
  }
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

if($llvm){
  Assert-SouiKernelPins $llvmInputPins
  Assert-SouiKernelPins $llvmLibraries
  Assert-SouiKernelPins @($llvmBeforePin,$llvmContextPin,$llvmVersionStdoutPin,$llvmVersionStderrPin)
  $llvmSourceAfter=Get-SouiKernelSourceSnapshot $source
  Assert-SouiKernelSnapshotEqual $llvmSourceBefore $llvmSourceAfter
  $llvmAfterPin=Write-SouiKernelData (Join-Path $output 'kernel-source-after.json') $llvmSourceAfter
  $map=Join-Path $output ('miniblink/bin/'+[IO.Path]::GetFileNameWithoutExtension($kernelName)+'.map')
  if(-not(Test-Path -LiteralPath $map -PathType Leaf)){throw 'LLVM Main returned without its current MAP.'}
  $report['kernelLlvm']=[ordered]@{
    schema='soui.public-kernel-llvm-build-files/v1';profile=$NativeToolchain;architecture=$Architecture
    inputs=$llvmContextPin;sourceBefore=$llvmBeforePin;sourceAfter=$llvmAfterPin
    sourceImmutable=$true;inputFilesStable=$true;libraries=$llvmLibraries;dll=(Get-SouiKernelPin $kernel);map=(Get-SouiKernelPin $map)
    requestedClangVersion=$llvm.RequestedClangVersion;requestedMsvcCompatibility=$llvm.MsvcCompatibilityVersion
    compilerVersionProbe=[ordered]@{scope='selected-tool-version-response';executableBefore=$versionExeBefore;executableAfter=$versionExeAfter;stdout=$llvmVersionStdoutPin;stderr=$llvmVersionStderrPin;exitCode=$versionExit;observedVersion=$observedClangVersion;translationUnitCompilationObserved=$false;wholeJob=$null}
    compilerExecution=$null;wholeJob=$null;abi=$null;toolchainQualification=$null;releaseReady=$false
  }
}
[IO.File]::WriteAllText((Join-Path $output "build-$Component.json"),($report|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output "Dependency source build completed: $output"

}finally{
  foreach($name in $llvmSavedEnvironment.Keys){$saved=$llvmSavedEnvironment[$name];if($saved.present){Set-Item -LiteralPath ("Env:"+$name) -Value $saved.value}else{Remove-Item -LiteralPath ("Env:"+$name) -ErrorAction SilentlyContinue}}
  if($null -ne $comspecSaved){if($comspecSaved.present){$env:ComSpec=$comspecSaved.value}else{Remove-Item -LiteralPath Env:ComSpec -ErrorAction SilentlyContinue}}
}
