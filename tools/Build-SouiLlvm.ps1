[CmdletBinding()]
param(
  [ValidateSet('x86','x64')][string]$Architecture='x64',
  [string]$NativeToolchain='',[string]$OutputRoot='',
  [string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,[string]$NinjaExe,
  [string]$WindowsCmdExe,[string]$PowerShellExe,[string]$VsInstallDir,[string]$NodeExe
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$externalPaths=@($LlvmRoot,$MsvcRoot,$WindowsSdkRoot,$NinjaExe,$WindowsCmdExe,$PowerShellExe,$VsInstallDir,$NodeExe)
if($NativeToolchain -cne 'LLVM22MSVC142' -or -not $OutputRoot -or @($externalPaths|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){
  throw 'SOUI LLVM requires explicit LLVM22MSVC142, fresh OutputRoot and all eight tool paths.'
}
. (Join-Path $PSScriptRoot 'internal/Find-LlvmToolchain.ps1')
. (Join-Path $PSScriptRoot 'internal/Kernel-LlvmInputs.ps1')
foreach($selectedPath in $externalPaths){[void](Assert-SouiKernelPathText $selectedPath)}
$output=Assert-SouiKernelPathText $OutputRoot
$allowed=([IO.Path]::GetFullPath((Join-Path $root 'out'))).Replace('\','/').TrimEnd('/')+'/'
if(-not $output.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){
  throw 'SOUI LLVM OutputRoot must be a new child of this SDK out directory.'
}
$root=Resolve-SouiKernelPath $root Directory
$source=Resolve-SouiKernelPath (Join-Path $root 'third_party/sources/soui') Directory
$node=Resolve-SouiKernelPath $NodeExe File
$llvm=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot -WindowsSdkRoot $WindowsSdkRoot `
  -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe `
  -ToolIdentityBridge (Join-Path $PSScriptRoot 'internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $PSScriptRoot 'internal/Tool-FileIdentity.ps1')
$roles=Get-SouiKernelVsRoles -VsInstallDir $VsInstallDir -MsvcRoot $MsvcRoot -Architecture $Architecture
$targets=Resolve-SouiKernelPath (Join-Path $PSScriptRoot 'internal/soui-llvm-output.targets') File
$output=Assert-SouiKernelFreshOutput -Output $output -ProtectedRoots @($source,$roles.vsInstallDir,$LlvmRoot,$MsvcRoot,$WindowsSdkRoot,([IO.Path]::GetDirectoryName($NinjaExe)),([IO.Path]::GetDirectoryName($WindowsCmdExe)),([IO.Path]::GetDirectoryName($PowerShellExe)),([IO.Path]::GetDirectoryName($node)))
$sourceBefore=Get-SouiKernelSourceSnapshot $source
$atlBefore=Get-SouiKernelSourceSnapshot (Join-Path $roles.msvcRoot 'atlmfc')
$pins=@($llvm.ToolPins.Values)+@($llvm.RequiredInputPins)+@($llvm.BridgePins)+@($roles.roles.Values)+@((Get-SouiKernelPin $node))
foreach($relative in @('Build-SouiLlvm.ps1','Stage-SouiSource.cjs','internal/delivery-identity.cjs','internal/soui-llvm-output.targets','internal/Kernel-LlvmInputs.ps1','internal/Find-LlvmToolchain.ps1')){
  $pins+=Get-SouiKernelPin (Resolve-SouiKernelPath (Join-Path $PSScriptRoot $relative) File)
}
$environment=@('CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH','LIBRARY_PATH',
  'CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR','EXTERNAL_INCLUDE','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS','RCFLAGS',
  'NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','ComSpec','MSBUILDDISABLENODEREUSE','VcpkgEnabled','VcpkgEnableManifest')
$saved=@{}
foreach($name in $environment){$item=Get-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;$saved[$name]=@{present=($null -ne $item);value=if($null -ne $item){$item.Value}else{$null}}}
$started=Get-Date
try{
  foreach($name in $environment){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath ('Env:'+$name)){throw "Cannot clear SOUI build provider: $name"}}
  $env:ComSpec=$llvm.NativeProcessPaths.CMD;$env:MSBUILDDISABLENODEREUSE='1';$env:VcpkgEnabled='false';$env:VcpkgEnableManifest='false'
  New-Item -ItemType Directory -Path $output | Out-Null
  [void](Resolve-SouiKernelPath $output Directory)
  $beforePin=Write-SouiKernelData (Join-Path $output 'soui-source-before.json') $sourceBefore
  $atlPin=Write-SouiKernelData (Join-Path $output 'atl-before.json') $atlBefore
  $inputPin=Write-SouiKernelData (Join-Path $output 'soui-llvm-inputs.json') ([ordered]@{schema='soui.public-soui-llvm-inputs/v1';profile=$NativeToolchain;architecture=$Architecture;discovery=$llvm;vsOwner=$roles;inputPins=$pins;compilerExecutionVerified=$false;qualified=$false})
  $stage=Join-Path $output 'source'
  $stageResult=@(& $node (Join-Path $PSScriptRoot 'Stage-SouiSource.cjs') $root $stage)
  if($LASTEXITCODE -ne 0 -or $stageResult.Count -ne 1 -or $stageResult[0] -cne $stage){throw 'SOUI staging did not return its exact output'}
  $staged=Get-SouiKernelSourceSnapshot $stage
  if(($staged.files|ConvertTo-Json -Depth 5 -Compress) -cne ($sourceBefore.files|ConvertTo-Json -Depth 5 -Compress)){throw 'SOUI staged files differ from the actual source inventory'}
  $stagedPin=Write-SouiKernelData (Join-Path $output 'staged-source-before.json') $staged
  $stagePins=@($staged.files|ForEach-Object{[ordered]@{path=($stage+'/'+$_.path);bytes=$_.bytes;sha256=$_.sha256}})
  $suffix=if($Architecture -eq 'x64'){'64'}else{''};$platform=if($Architecture -eq 'x64'){'x64'}else{'Win32'}
  $outDir=Join-Path $stage "bin$suffix/"
  New-Item -ItemType Directory -Force -Path $outDir,(Join-Path $output 'empty-user-props') | Out-Null
  $artifacts=@();$records=@()
  foreach($component in @('utilities/utilities','SOUI/soui','components/render-gdi/render-gdi','components/imgdecoder-gdip/imgdecoder-gdip','soui-sys-resource/soui-sys-resource')){
    $project=Resolve-SouiKernelPath (Join-Path $stage "$component$suffix.vcxproj") File
    $name=[IO.Path]::GetFileName($component);$intDir=Join-Path $output "obj/$name/"
    $arguments=@($project,'/t:Build','/m:1','/nr:false','/p:Configuration=Release',"/p:Platform=$platform",'/p:PlatformToolset=ClangCL','/p:VCToolsVersion=14.29.30133','/p:WindowsTargetPlatformVersion=10.0.19041.0','/p:PreferredToolArchitecture=x64',"/p:LLVMInstallDir=$LlvmRoot/",'/p:LLVMToolsVersion=22.1.3','/p:CLToolExe=clang-cl.exe',"/p:CLToolPath=$LlvmRoot/bin",'/p:LinkToolExe=lld-link.exe',"/p:LinkToolPath=$LlvmRoot/bin",'/p:LIBToolExe=llvm-lib.exe',"/p:LIBToolPath=$LlvmRoot/bin",'/p:UseEnv=false','/p:TrackFileAccess=true','/p:ImportDirectoryBuildProps=false','/p:ImportDirectoryBuildTargets=false',"/p:UserRootDir=$output/empty-user-props/",'/p:VcpkgEnabled=false','/p:VcpkgEnableManifest=false','/p:VCPkgLocalAppDataDisabled=true','/p:BuildProjectReferences=false',"/p:SouiMsvcRoot=$MsvcRoot","/p:SouiSdkRoot=$WindowsSdkRoot","/p:SouiArchitecture=$Architecture","/p:IntDir=$intDir","/p:OutDir=$outDir","/p:TargetName=$name",'/p:TargetExt=.dll',"/p:ForceImportBeforeCppTargets=$targets",'/p:RCToolExe=rc.exe',"/p:RCToolPath=$WindowsSdkRoot/bin/10.0.19041.0/x64",'/p:MtToolExe=mt.exe',"/p:MtToolPath=$WindowsSdkRoot/bin/10.0.19041.0/x64",'/v:minimal',"/bl:$output/$name.binlog")
    $argumentsPin=Write-SouiKernelData (Join-Path $output "$name.arguments.json") $arguments
    Assert-SouiKernelPins ($pins+$stagePins)
    Write-Output "Building SOUI LLVM component: $name / $Architecture"
    $log=Join-Path $output "$name.log"
    & $roles.roles.MSBUILD.path @arguments *> $log
    if($LASTEXITCODE -ne 0){throw "SOUI LLVM $name failed; see $log"}
    if(Select-String -LiteralPath $log -Pattern 'warning C4819','warning C4828' -Quiet){throw "SOUI source encoding warning; see $log"}
    $dll=Get-SouiKernelPin (Join-Path $outDir "$name.dll")
    $artifacts+=@{kind=$name;path=$dll.path;sha256=$dll.sha256}
    $records+=@{component=$name;project=(Get-SouiKernelPin $project);arguments=$argumentsPin;log=(Get-SouiKernelPin $log);binlog=(Get-SouiKernelPin (Join-Path $output "$name.binlog"));dll=$dll;importLibrary=(Get-SouiKernelPin (Join-Path $outDir "$name.lib"));map=(Get-SouiKernelPin (Join-Path $outDir "$name.map"))}
  }
  Assert-SouiKernelPins ($pins+$stagePins+@($beforePin,$atlPin,$inputPin,$stagedPin))
  $sourceAfter=Get-SouiKernelSourceSnapshot $source;Assert-SouiKernelSnapshotEqual $sourceBefore $sourceAfter
  $atlAfter=Get-SouiKernelSourceSnapshot (Join-Path $roles.msvcRoot 'atlmfc');Assert-SouiKernelSnapshotEqual $atlBefore $atlAfter
  $afterPin=Write-SouiKernelData (Join-Path $output 'soui-source-after.json') $sourceAfter
  $report=[ordered]@{schema='soui.dependency-build/v1';architecture=$Architecture;component='SOUI';experimentalGpu=$false;startedAt=$started.ToString('o');finishedAt=(Get-Date).ToString('o');status='built';releaseReady=$false;souiSourceStage=$stage;artifacts=$artifacts;
    souiLlvm=[ordered]@{schema='soui.public-soui-llvm-build-files/v1';profile=$NativeToolchain;architecture=$Architecture;inputs=$inputPin;sourceBefore=$beforePin;sourceAfter=$afterPin;stageBefore=$stagedPin;atl=$atlPin;sourceImmutable=$true;inputFilesStable=$true;records=$records;compilerExecution=$null;wholeJob=$null;abi=$null;toolchainQualification=$null;releaseReady=$false}}
  [void](Write-SouiKernelData (Join-Path $output 'build-SOUI.json') $report)
  Write-Output "SOUI LLVM dependency build completed: $output"
}finally{
  foreach($name in $saved.Keys){$previous=$saved[$name];if($previous.present){Set-Item -LiteralPath ('Env:'+$name) -Value $previous.value}else{Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}}
}
