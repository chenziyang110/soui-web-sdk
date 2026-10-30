[CmdletBinding()]
param([ValidateSet('Prepare','Build')][string]$Mode,[ValidateSet('x64','x86')][string]$Architecture,[string]$PreparedSha256='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$runRoot=$PSScriptRoot
$sdkRoot='C:/sr06';$sourceRoot=$sdkRoot+'/third_party/sources/miniblink';$outputRoot=$sdkRoot+'/out/release-kernel-'+$Architecture+'-3'
$internal=$sdkRoot+'/tools/internal'
. ($internal+'/Find-LlvmToolchain.ps1')
. ($internal+'/Kernel-LlvmInputs.ps1')
$vs='C:/Program Files/Microsoft Visual Studio/18/Community'
$llvmRoot=$vs+'/VC/Tools/Llvm/x64';$msvcRoot=$vs+'/VC/Tools/MSVC/14.29.30133';$winSdk='C:/Program Files (x86)/Windows Kits/10'
$ninja=$vs+'/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe';$cmd='C:/Windows/System32/cmd.exe';$pwsh='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'
$inputFile=$runRoot+'/Prepared-'+$Architecture+'.json'
function Assert-NoEncodingWarning([string]$Log){if(Select-String -LiteralPath $Log -Pattern 'warning C4819','warning C4828' -Quiet){throw 'Source encoding warning; preserve failure and log.'}}
if($Mode -eq 'Prepare'){
  if(Test-Path -LiteralPath $inputFile){throw 'One-shot prepared inputs already exist.'}
  # Output is the explicitly owned task out subtree, disjoint from compiler source/tools.
  [void](Assert-SouiKernelFreshOutput $outputRoot @($sourceRoot,($sdkRoot+'/tools'),$vs,$winSdk))
  $discovery=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $llvmRoot -MsvcRoot $msvcRoot -WindowsSdkRoot $winSdk -NinjaExe $ninja -WindowsCmdExe $cmd -PowerShellExe $pwsh -ToolIdentityBridge ($internal+'/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper ($internal+'/Tool-FileIdentity.ps1')
  $roles=Get-SouiKernelVsRoles -VsInstallDir $vs -MsvcRoot $msvcRoot -Architecture $Architecture
  $forced=Get-SouiKernelV8ForcedSymbols $sourceRoot
  $source=Get-SouiKernelSourceSnapshot $sourceRoot
  $alignedInventoryPath=$runRoot+'/Source2.source-after.json';$alignedInventoryPin=Get-SouiKernelPin $alignedInventoryPath
  if($alignedInventoryPin.sha256 -cne 'c698985f7d46d02984a7eec46d96c1ebeac5164b816b61b560d96b657c136809'){throw 'The independently reported Source2 inventory file differs.'}
  $alignedInventory=Get-Content -LiteralPath $alignedInventoryPath -Raw|ConvertFrom-Json -AsHashtable
  if($alignedInventory.identity -cne '26c5b73e4915bb4539e87b987cd8e952005f0fe41d987138d8d8e3b13eb25c95'){throw 'Source2 inventory identity differs.'}
  Assert-SouiKernelSnapshotEqual $alignedInventory $source
  $sourceFile=$runRoot+'/Prepared-'+$Architecture+'-source.json';$sourcePin=Write-SouiKernelData $sourceFile $source
  $inputPins=@($discovery.ToolPins.Values)+@($discovery.RequiredInputPins)+@($discovery.BridgePins)+@($roles.roles.Values)+@($forced.projects|ForEach-Object{$_.projectPin})
  foreach($relative in @('Build-KernelLibraryArch.cmd','Build-KernelMainArch.cmd','kernel-llvm-output.targets','Find-LlvmToolchain.ps1','Kernel-LlvmInputs.ps1','Invoke-ToolFileIdentity.ps1','Tool-FileIdentity.ps1')){$inputPins+=Get-SouiKernelPin ($internal+'/'+$relative)}
  $inputPins+=Get-SouiKernelPin $PSCommandPath
  $inputPins+=$alignedInventoryPin
  $inputPins+=Get-SouiKernelPin ($taskRoot+'/LibrarySourceAlignment.actual.json')
  foreach($file in @('Source2SourceDiff.json','Run2ReuseProof.json','Continue-KernelMainArch.cmd')){$inputPins+=Get-SouiKernelPin ($runRoot+'/'+$file)}
  $reuse=Get-Content -LiteralPath ($runRoot+'/Run2ReuseProof.json') -Raw|ConvertFrom-Json -AsHashtable
  if($reuse.objectCount -ne 1903 -or -not $reuse.changedCppNotInOtherTuOrPch -or $reuse.historicalP9MainObjectsOrPchAdmitted){throw 'Admitted fresh run2 closure differs.'}
  $inputPins+=@($reuse.preserved|ForEach-Object{$_.copy})
  $beforeOnly=@();if($Architecture -eq 'x64'){$beforeOnly=@($reuse.preserved|ForEach-Object{$_.original});Assert-SouiKernelPins $beforeOnly}
  $admitted=@();if($Architecture -eq 'x64'){$admitted=@($reuse.preserved|Where-Object{$_.relativePath -match '\.(obj|pch)$'});Assert-SouiKernelPins @($admitted|ForEach-Object{$_.original;$_.copy})}
  $auditPath=$taskRoot+'/LibraryReuseAudit.aligned.json';$audit=Get-Content -LiteralPath $auditPath -Raw|ConvertFrom-Json -AsHashtable
  $spec=@($audit.specs|Where-Object{$_.architecture -eq $Architecture})[0]
  if(-not $spec -or $spec.libraries.Count -ne 11 -or -not $audit.translationUnitHeaderPathsIncluded){throw 'Actual complete eleven-library ledger required.'}
  Assert-SouiKernelPins @((Get-SouiKernelPin $auditPath),$spec.receipt,$spec.sourceBefore)
  $oldContext=Get-Content -LiteralPath ($spec.warmRoot+'/kernel-llvm-inputs.json') -Raw|ConvertFrom-Json -AsHashtable
  $selectedToolPins=@($discovery.ToolPins.Values)+@($discovery.RequiredInputPins)+@($roles.roles.Values)
  foreach($current in $selectedToolPins){$prior=@($oldContext.inputPins|Where-Object{$_.path.Replace('\','/').Equals($current.path.Replace('\','/'),[StringComparison]::OrdinalIgnoreCase)})
    if($prior.Count -ne 1 -or $prior[0].bytes -ne $current.bytes -or $prior[0].sha256 -cne $current.sha256){throw "Original selected provider file differs: $($current.path)"}}
  foreach($name in @('Build-KernelLibraryArch.cmd','kernel-llvm-output.targets','Find-LlvmToolchain.ps1','Invoke-ToolFileIdentity.ps1','Tool-FileIdentity.ps1')){
    $current=Get-SouiKernelPin ($internal+'/'+$name);$prior=@($oldContext.inputPins|Where-Object{$_.path.Replace('\','/').EndsWith('/'+$name,[StringComparison]::OrdinalIgnoreCase)})
    if($prior.Count -ne 1 -or $prior[0].bytes -ne $current.bytes -or $prior[0].sha256 -cne $current.sha256){throw "Library recipe changed: $name"}}
  $externalPaths=@($spec.libraries|ForEach-Object{$_.externalInputPaths}|Sort-Object -Unique);$externalPins=@($externalPaths|ForEach-Object{Get-SouiKernelPin $_})+@($reuse.currentExternalReadPins)
  foreach($library in $spec.libraries){if(-not $library.reuseEligible -or $library.sourceChanges.Count -or -not $library.project.equal){throw 'Alignment proof no longer supports library reuse; rebuild affected library explicitly.'};Assert-SouiKernelPins @($library.original,$library.compileRead,$library.project.after)}
  $project=$sourceRoot+'/build/miniblink/miniblink.vcxproj';$inputPins+=Get-SouiKernelPin $project
  $xml=[Xml.XmlDocument]::new();$xml.XmlResolver=$null;$xml.Load($project);$ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m','http://schemas.microsoft.com/developer/msbuild/2003')
  $mainSources=@();$platform=if($Architecture -eq 'x64'){'x64'}else{'Win32'}
  foreach($node in $xml.SelectNodes('//m:ItemGroup/m:ClCompile[@Include]',$ns)){
    $excluded=$false;foreach($flag in $node.SelectNodes('m:ExcludedFromBuild',$ns)){if($flag.InnerText -eq 'true' -and (-not $flag.GetAttribute('Condition') -or $flag.GetAttribute('Condition').Contains('Release|'+$platform))){$excluded=$true}}
    if(-not $excluded){$actual=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($project)) $node.GetAttribute('Include')));$mainSources+=Get-SouiKernelPin $actual}
  }
  $value=[ordered]@{schema='soui.release.source2-kernel-inputs/v1';architecture=$Architecture;sdkRoot=$sdkRoot;sourceRoot=$sourceRoot;outputRoot=$outputRoot;source=$sourcePin;alignedSourceInventory=$alignedInventoryPin;sourceIdentity=$source.identity;sourceFileCount=$source.fileCount;discovery=$discovery;roles=$roles;forcedSymbols=$forced;inputPins=$inputPins;externalLibraryReadPins=$externalPins;externalLibraryReadPinsAreCurrentOnly=$true;oldWholeProviderHeaderBytesProven=$false;oldContext=(Get-SouiKernelPin ($spec.warmRoot+'/kernel-llvm-inputs.json'));libraryAudit=(Get-SouiKernelPin $auditPath);libraries=$spec.libraries;sourcesToRebuild=$mainSources;historicalP9MainObjectsOrPchReused=$false;admittedFreshRun2Objects=$admitted;admittedBeforeOnlyPins=$beforeOnly;explicitIncrementalFromRun2=($Architecture -eq 'x64');timeoutMs=14400000;drainMs=2000;perLibraryDeadlineReset=$false;wholeToolInventoryVerified=$false;releaseReady=$false}
  $pin=Write-SouiKernelData $inputFile $value;$pin|ConvertTo-Json -Compress;exit 0
}
if($Mode -ne 'Build' -or $PreparedSha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'Explicit Build and exact prepared SHA required.'}
$preparedPin=Get-SouiKernelPin $inputFile;if($preparedPin.sha256 -cne $PreparedSha256){throw 'Prepared inputs changed.'}
$prepared=Get-Content -LiteralPath $inputFile -Raw|ConvertFrom-Json -AsHashtable
if($prepared.sourceRoot -cne $sourceRoot -or $prepared.outputRoot -cne $outputRoot -or $prepared.architecture -cne $Architecture){throw 'Source/output/architecture binding differs.'}
Assert-SouiKernelPins (@($prepared.inputPins)+@($prepared.externalLibraryReadPins)+@($prepared.source,$prepared.libraryAudit,$prepared.oldContext))
$before=Get-SouiKernelSourceSnapshot $sourceRoot;$frozen=Get-Content -LiteralPath $prepared.source.path -Raw|ConvertFrom-Json -AsHashtable;Assert-SouiKernelSnapshotEqual $frozen $before
[void](Assert-SouiKernelFreshOutput $outputRoot @($sourceRoot,($sdkRoot+'/tools'),$vs,$winSdk));New-Item -ItemType Directory -Path $outputRoot|Out-Null
$beforePin=Write-SouiKernelData ($outputRoot+'/source-before.json') $before
$libraryRoot=$outputRoot+'/libraries';New-Item -ItemType Directory -Path ($libraryRoot+'/lib') -Force|Out-Null
$savedComspec=$env:ComSpec;$env:ComSpec=$prepared.discovery.NativeProcessPaths.CMD
try{
  # Runtime version probe belongs to this one whole architecture Job/deadline.
  & $prepared.discovery.ToolPins.CLANG.path --no-default-config --version 1>($outputRoot+'/clang-version.stdout.log') 2>($outputRoot+'/clang-version.stderr.log')
  $versionExit=$LASTEXITCODE;$versionText=[IO.File]::ReadAllText($outputRoot+'/clang-version.stdout.log');$observed=Get-SouiKernelClangVersion -Text $versionText -ExitCode $versionExit
  $copied=@();foreach($library in $prepared.libraries){Assert-SouiKernelPins @($library.original,$library.compileRead,$library.project.after);$destination=$libraryRoot+'/lib/'+$library.library+'.lib';[IO.File]::Copy($library.original.path,$destination,$false);$copy=Get-SouiKernelPin $destination;if($copy.bytes -ne $library.original.bytes -or $copy.sha256 -cne $library.original.sha256){throw 'Copied library differs.'};$copied+=@{library=$library.library;origin=$library.original;copied=$copy;recompiled=$false}}
  Assert-SouiKernelPins (@($prepared.inputPins)+@($prepared.externalLibraryReadPins))
  $main=if($Architecture -eq 'x64'){'C:/sr06/out/release-kernel-x64-2/miniblink'}else{$outputRoot+'/miniblink'}
  New-Item -ItemType Directory -Path ($outputRoot+'/miniblink'),($main+'/llvm-temp'),($main+'/empty-user-props') -Force|Out-Null
  $entry=if($Architecture -eq 'x64'){$runRoot+'/Continue-KernelMainArch.cmd'}else{$internal+'/Build-KernelMainArch.cmd'}
  if($Architecture -eq 'x64'){Assert-SouiKernelPins $prepared.admittedBeforeOnlyPins}
  $env:SOUI_SOURCE2_BINLOG=[IO.Path]::GetFullPath($outputRoot+'/miniblink/build.binlog')
  [void](Assert-SouiKernelPathText $env:SOUI_SOURCE2_BINLOG)
  if([string]::IsNullOrWhiteSpace($env:SOUI_SOURCE2_BINLOG) -or (Test-Path -LiteralPath $env:SOUI_SOURCE2_BINLOG)){throw 'A new nonempty Source2 binlog path is required.'}
  $arguments=@($prepared.roles.roles.VCVARSALL.path,$sourceRoot,$main,($libraryRoot+'/lib'),'none',$Architecture,'LLVM22MSVC142',$vs,$llvmRoot,$msvcRoot,$winSdk,($internal+'/kernel-llvm-output.targets'))
  [void](Write-SouiKernelData ($outputRoot+'/main-command.json') @{program=$entry;arguments=$arguments;project=($sourceRoot+'/build/miniblink/miniblink.vcxproj');solutionDir=($sourceRoot+'/build/');intDir=($main+'/obj/');explicitIncrementalFromRun2=($Architecture -eq 'x64');timeoutReset=$false;binlog=$env:SOUI_SOURCE2_BINLOG})
  & $entry @arguments *> ($outputRoot+'/miniblink.log')
  $mainExit=$LASTEXITCODE;if($mainExit -ne 0){throw "Final source main kernel failed naturally with native exit $mainExit; preserve output and log."};Assert-NoEncodingWarning ($outputRoot+'/miniblink.log')
  Assert-SouiKernelPins (@($prepared.inputPins)+@($prepared.externalLibraryReadPins)+@($copied|ForEach-Object{$_.origin;$_.copied}))
  $after=Get-SouiKernelSourceSnapshot $sourceRoot;$afterPin=Write-SouiKernelData ($outputRoot+'/source-after.json') $after;Assert-SouiKernelSnapshotEqual $before $after
  $objectChanges=@();foreach($r in $prepared.admittedFreshRun2Objects){$actual=Get-SouiKernelPin $r.original.path;$item=Get-Item -LiteralPath $r.original.path;$sameHash=($actual.bytes -eq $r.original.bytes -and $actual.sha256 -ceq $r.original.sha256);$nowNs=([long]$item.LastWriteTimeUtc.Ticks-[long]621355968000000000)*100;if((-not $sameHash) -or $nowNs.ToString() -cne $r.original.mtimeNs){$objectChanges+=@{before=$r.original;after=$actual;bytesUnchanged=$sameHash;timestampChanged=($nowNs.ToString() -cne $r.original.mtimeNs)}}}
  $name=if($Architecture -eq 'x64'){'miniblink'}else{'node'}
  [void](Write-SouiKernelData ($outputRoot+'/build-result.json') @{schema='soui.release.source2-kernel-result/v1';architecture=$Architecture;prepared=$preparedPin;sourceBefore=$beforePin;sourceAfter=$afterPin;sourceIdentity=$after.identity;sourceImmutable=$true;libraries=$copied;mainSources=$prepared.sourcesToRebuild.Count;historicalP9MainObjectsOrPchReused=$false;explicitIncrementalFromRun2=($Architecture -eq 'x64');admittedFreshRun2ObjectCount=@($prepared.admittedFreshRun2Objects|Where-Object{$_.relativePath.EndsWith('.obj')}).Count;admittedFreshRun2Pch=($Architecture -eq 'x64');admittedObjectChanges=$objectChanges;mainOutputRoot=$main;observedClangVersion=$observed;nativeMainExitCode=$mainExit;dll=(Get-SouiKernelPin ($main+'/bin/'+$name+'.dll'));map=(Get-SouiKernelPin ($main+'/bin/'+$name+'.map'));importLibrary=(Get-SouiKernelPin ($main+'/bin/'+$name+'.lib'));dllLoaded=$false;productExecuted=$false;testsExecuted=$false;releaseReady=$false})
}catch{
  [void](Write-SouiKernelData ($outputRoot+'/build-failure.json') @{message=$_.Exception.Message;stack=$_.ScriptStackTrace;architecture=$Architecture;nativeMainExitCode=$mainExit;sourceAfter=(Get-SouiKernelSourceSnapshot $sourceRoot);remainingArchitectureStarted=$false;retried=$false;productExecuted=$false});throw
}finally{$env:ComSpec=$savedComspec}
