param([Parameter(Mandatory=$true)][ValidateSet('x64','x86')][string]$Architecture,
      [Parameter(Mandatory=$true)][string]$CaseDirectory,
      [Parameter(Mandatory=$true)][string]$ShortWorkspace,
      [Parameter(Mandatory=$true)][string]$FrozenInputs)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository='C:\sc07'
$phase=$null;$failure=$null;$cleanupFailures=[Collections.Generic.List[string]]::new()
$previous=Get-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
$receipt=[ordered]@{schema='soui.message-port-consumer-build-only-root/v1';architecture=$Architecture;rootPid=$PID;
  beganUtc=[DateTime]::UtcNow.ToString('o');completed=$false;productExecuted=$false;executableExecuted=$false;
  originalTimeoutMs=120000;originalDrainMs=2000;wholeJobProofRequired=$true;oldFailureReclassified=$false}
function Pin-File([string]$Path){
  $item=Get-Item -LiteralPath $Path -Force
  if($item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Expected plain input file.'}
  [pscustomobject]@{path=$item.FullName;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Save-New([string]$Name,$Value){
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12)+[char]10)
  $stream=[IO.File]::Open((Join-Path $CaseDirectory $Name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Assert-Frozen {
  foreach($expected in (Get-Content -LiteralPath $FrozenInputs -Raw|ConvertFrom-Json).sourcePins){
    $actual=Pin-File $expected.path
    if($actual.bytes-ne$expected.bytes-or$actual.sha256-cne$expected.sha256){throw 'Frozen source/tool changed before or during the real native short case.'}
  }
}
$toolPaths=@()
try{
  if(-not[IO.Path]::IsPathRooted($CaseDirectory)-or-not[IO.Directory]::Exists($CaseDirectory)){throw 'Missing fresh absolute case directory.'}
  Assert-Frozen
  foreach($name in @('CL','_CL_','LINK','_LINK_')){
    if(-not[string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))){throw 'Ambient compiler/linker option providers must be absent for this exact short smoke.'}
  }
  . (Join-Path $repository 'tools/internal/Find-Toolchain.ps1')
  . (Join-Path $repository 'tools/internal/CMake-Configure.ps1')
  . (Join-Path $repository 'tools/internal/Msvc-CompilerLifecycle.ps1')
  $env:MSBUILDDISABLENODEREUSE='1'
  $toolchain=Get-SouiToolchain
  $cmake=(Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
  $toolBin=Join-Path $toolchain.MsvcRoot ('bin/Hostx64/'+$Architecture)
  $toolPaths=@($cmake,(Join-Path $toolchain.Root 'MSBuild/Current/Bin/amd64/MSBuild.exe'),
    (Join-Path $toolBin 'cl.exe'),(Join-Path $toolBin 'link.exe'),(Join-Path $toolBin 'c1xx.dll'),
    (Join-Path $toolBin 'vctip.exe'),(Join-Path $toolBin 'vctip.exe.config'),$toolchain.DevCmd,
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'))
  $toolPins=@($toolPaths|ForEach-Object{Pin-File $_})
  Save-New 'tool-pins-before.json' $toolPins
  $workspace=[IO.Path]::GetFullPath($ShortWorkspace)
  if($workspace-cnotmatch('^C:\\sci8-[a-f0-9]{8}-'+$Architecture+'$')-or(Test-Path -LiteralPath $workspace)){
    throw 'The explicitly owned short workspace must be fresh and match this architecture.'
  }
  [void](New-Item -ItemType Directory -Path $workspace)
  $receipt.workspace=$workspace
  [IO.File]::WriteAllText((Join-Path $workspace 'ownership.json'),(@{schema='soui.independent-message-port-workspace/v1';
    caseDirectory=$CaseDirectory;architecture=$Architecture;rootPid=$PID;createdUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
  $source=Join-Path $workspace 'source';$build=Join-Path $workspace ('out/'+$Architecture)
  [void](New-Item -ItemType Directory -Path $source -Force)
  [void](New-Item -ItemType Directory -Path $build -Force)
  foreach($name in @('CMakeLists.txt','main.cpp','shutdown-poll.h')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('source/'+$name)) -Destination (Join-Path $source $name)}
  Save-New 'fixture-source-pins.json' @(@('CMakeLists.txt','main.cpp','shutdown-poll.h')|ForEach-Object{Pin-File (Join-Path $source $_)})
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'web-output') -Destination $workspace -Recurse
  $configure=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot $workspace -SourceDirectory $source `
    -BuildDirectory $build -Architecture $Architecture -FreshConfigure -NativeToolchain MSVC143)
  $prefix='C:\sr06\out\release-install-'+$Architecture+'-release'
  $receipt.installedSdkPrefix=$prefix
  $configure+=('-DCMAKE_GENERATOR_INSTANCE:PATH='+$toolchain.Root),('-DSouiSdk_DIR:PATH='+(Join-Path $prefix 'lib/cmake/SouiSdk')),('-DSOUI_ACCEPTANCE_WEB_ROOT:PATH='+(Join-Path $workspace 'web-output'))
  $buildArguments=@('--build',$build,'--config','Release','--target','SouiAcceptance','--parallel','2','--','/nr:false')
  Save-New 'invocations.json' @{toolchain=$toolchain;cmake=$cmake;configure=$configure;build=$buildArguments;
    environment=@{MSBUILDDISABLENODEREUSE=$env:MSBUILDDISABLENODEREUSE};compilerHelperSuppressionConfigured=$false}
  foreach($phaseName in @('configure','build')){
    $phaseEvidence=Join-Path $CaseDirectory $phaseName
    [void](New-Item -ItemType Directory -Path $phaseEvidence)
    $phase=Start-SouiMsvcCompilerPhase -Toolchain $toolchain -Architecture $Architecture -EvidenceRoot $phaseEvidence -Phase $phaseName
    if($phaseName-eq'configure'){& $cmake @configure *> (Join-Path $CaseDirectory 'configure.log')}
    else{& $cmake @buildArguments *> (Join-Path $CaseDirectory 'build.log')}
    $nativeExit=$LASTEXITCODE
    Complete-SouiMsvcCompilerPhase $phase $nativeExit ('Independent MessagePort CMake '+$phaseName+' failed')
  }
  $cache=Join-Path $build 'CMakeCache.txt'
  $cacheLines=Get-Content -LiteralPath $cache
  # The actual compiler declaration is selected from CMake's own cache version.
  $cmakeVersion=@('MAJOR','MINOR','PATCH')|ForEach-Object{
    $key=$_;$line=@($cacheLines|Where-Object{$_-match('^CMAKE_CACHE_'+$key+'_VERSION:INTERNAL=')})
    if($line.Count-ne1){throw 'Ambiguous CMake cache version.'};($line[0]-split'=',2)[1]
  }
  $compilerDeclaration=Join-Path $build ('CMakeFiles/'+($cmakeVersion-join'.')+'/CMakeCXXCompiler.cmake')
  $exe=Join-Path $build 'bin/Release/SouiAcceptance.exe'
  Save-New 'compiled-output-pins.json' @((Pin-File $exe),(Pin-File $cache),(Pin-File $compilerDeclaration))
  $receipt.workspace=$workspace;$receipt.buildDirectory=$build;$receipt.compilerDeclaration=$compilerDeclaration;$receipt.cache=$cache;$receipt.executable=$exe
  $receipt.completed=$true
}catch{$failure=$_;$receipt.failure=$_.Exception.Message}
finally{
  try{Close-SouiMsvcCompilerPhase $phase}catch{$cleanupFailures.Add($_.Exception.Message)}
  try{
    if($null-ne$previous){Set-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -Value $previous.Value}
    else{Remove-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue}
    $restored=Get-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
    if(($null-ne$restored)-ne($null-ne$previous)-or($null-ne$previous-and$restored.Value-cne$previous.Value)){throw 'Caller MSBuild environment was not restored.'}
  }catch{$cleanupFailures.Add($_.Exception.Message)}
  try{if($toolPaths.Count){Save-New 'tool-pins-after.json' @($toolPaths|ForEach-Object{Pin-File $_})};Assert-Frozen}
  catch{$cleanupFailures.Add($_.Exception.Message)}
  $receipt.returnedUtc=[DateTime]::UtcNow.ToString('o');$receipt.cleanupFailures=@($cleanupFailures.ToArray())
  Save-New 'root-return.json' $receipt
}
if($failure-or$cleanupFailures.Count){if($failure){[Console]::Error.WriteLine($failure.Exception.Message)};exit 1}
exit 0
