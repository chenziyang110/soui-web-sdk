[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$candidate=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))
$original='D:/work/sm05-terminal-trace-fixed-sdk-v1/tools/internal/Observe-LlvmBuild.ps1'
$diagnosisPath='C:/work/sm05-terminal-contract-fix-source-v1/ACTUAL-FAILING-ROWS.json'
$diagnosis=[IO.File]::ReadAllText($diagnosisPath)|ConvertFrom-Json
$databasePath=$diagnosis.compileDatabase.path
if((Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash.ToLowerInvariant()-cne$diagnosis.compileDatabase.sha256){throw 'Original actual database changed'}
function Read-FunctionText([string]$File,[string[]]$Names){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($File,[ref]$tokens,[ref]$errors)
 if($errors.Count-ne0){throw 'Pure source AST must parse'}
 $result=@();foreach($name in $Names){$matches=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true));if($matches.Count-ne1){throw ('Unique pure function required: '+$name)};$result+=$matches[0].Extent.Text}
 return ($result-join"`n")
}
# Only these pure AST function definitions execute. Never dot-source the observer entry.
. ([scriptblock]::Create((Read-FunctionText $candidate @('Need','Norm','Full','CompileArgv','Test-CrtOption','Test-RuntimeDoctorCompileBinding','Test-RuntimeDoctorCompileOptions'))))
$oldText=Read-FunctionText $original @('Test-CrtOption')
. ([scriptblock]::Create(($oldText-replace'function Test-CrtOption\(','function Old-Test-CrtOption(')))
$database=@([IO.File]::ReadAllText($databasePath)|ConvertFrom-Json)
$failedSource=[string]$diagnosis.failures[0].file;$root=$failedSource.Substring(0,(Norm $failedSource).IndexOf('/sdk/src/'));$build=[IO.Path]::GetDirectoryName($databasePath);$configuration='Release'
$doctor=@($database|Where-Object{(Norm $_.output).StartsWith(((Norm $build).TrimEnd('/')+'/tools/runtime-doctor/cmakefiles/souiruntimedoctor.dir/release/'),[StringComparison]::Ordinal)})
Need ($doctor.Count-eq2) 'Exact two original Release Doctor rows'
$profileRow=@($doctor|Where-Object{(Norm $_.file)-ceq(Norm $failedSource)})[0]
Need ($profileRow.command-ceq$diagnosis.failures[0].actualCommand-and(Norm $profileRow.output)-ceq(Norm $diagnosis.failures[0].object)) 'Genuine original failed row must remain exact'
$sdk=@($database|Where-Object{(Norm $_.file).StartsWith(((Norm $root).TrimEnd('/')+'/sdk/src/'),[StringComparison]::Ordinal)-and(Norm $_.output)-match'/release/'})
Need ($sdk.Count-eq41) 'Original actual SDK-prefix Release rows'
$sdkRow=@($sdk|Where-Object{(Norm $_.output)-notmatch'/tools/runtime-doctor/cmakefiles/souiruntimedoctor.dir/'})[0]
[string[]]$doctorArgs=@(CompileArgv $profileRow.command);[string[]]$sdkArgs=@(CompileArgv $sdkRow.command)
function Assert-SemanticRow([string]$Source,[string]$Object,[string[]]$Arguments,[string]$Config='Release'){
 $isDoctor=Test-RuntimeDoctorCompileBinding -Source $Source -Object $Object -DistributionRoot $root -BuildRoot $build -Configuration $Config
 if($isDoctor){Need (Test-RuntimeDoctorCompileOptions -Arguments $Arguments -Configuration $Config) 'RuntimeDoctor static CRT/Unicode Win7 macro contract';return 'RuntimeDoctor'}
 if((Norm $Source).StartsWith(((Norm $root).TrimEnd('/')+'/sdk/src/'),[StringComparison]::Ordinal)){
  $flags=Norm ($Arguments-join' ')
  Need ((Test-CrtOption -Arguments $Arguments -ExpectedCRT MD)-and$flags-match'(?<!\S)/u_debug(?!\S)'-and$flags-match'(?<!\S)[/-]dwinver=0x0601(?!\S)'-and$flags-match'(?<!\S)[/-]d_win32_winnt=0x0601(?!\S)') 'SDK target CRT/Unicode OS macro contract'
  return 'SDK'
 }
 return 'Other unchanged scope'
}
$cases=[Collections.Generic.List[object]]::new();$red=$null
function Test-Case([string]$Name,[scriptblock]$Body){try{& $Body;$cases.Add([ordered]@{name=$Name;passed=$true})}catch{$cases.Add([ordered]@{name=$Name;passed=$false;error=$_.Exception.Message})}}
function Must-Reject([scriptblock]$Body){$rejected=$false;try{& $Body|Out-Null}catch{$rejected=$true};Need $rejected 'Expected rejection was missing'}
Test-Case 'genuine-old-SDK-prefix-row-RED' {
 $oldAccepted=$true;$oldFailure=$null;$flags=Norm ($doctorArgs-join' ')
 try{Need ((Old-Test-CrtOption -Arguments $doctorArgs -ExpectedCRT MD)-and$flags-match'(?<!\S)/u_debug(?!\S)'-and$flags-match'(?<!\S)[/-]dwinver=0x0601(?!\S)'-and$flags-match'(?<!\S)[/-]d_win32_winnt=0x0601(?!\S)') 'SDK target CRT/Unicode OS macro contract'}catch{$oldAccepted=$false;$oldFailure=$_.Exception.Message}
 $script:red=[ordered]@{source=$profileRow.file;object=$profileRow.output;originalRawCommand=$profileRow.command;originalParsedArgv=$doctorArgs;oldAccepted=$oldAccepted;oldFailure=$oldFailure;newPolicy=(Assert-SemanticRow $profileRow.file $profileRow.output $doctorArgs);actualOriginalAttemptRemainsFailed=$true;newNativeAttempt=$null}
 Need (-not$oldAccepted-and$oldFailure-ceq'SDK target CRT/Unicode OS macro contract'-and$red.newPolicy-ceq'RuntimeDoctor') 'Old RED/current pure GREEN must be genuine'
}
foreach($row in @($sdk)+@($doctor|Where-Object{(Norm $_.file)-cne(Norm $failedSource)})){
 Test-Case ('actual-original-row-'+$row.output) {[string[]]$argv=@(CompileArgv $row.command);[void](Assert-SemanticRow $row.file $row.output $argv)}
}
Test-Case 'ordinary-SDK-MD-control' {Need ((Assert-SemanticRow $sdkRow.file $sdkRow.output $sdkArgs)-ceq'SDK') 'Original SDK MD policy'}
Test-Case 'ordinary-SDK-MT-reject' {[string[]]$bad=@($sdkArgs|ForEach-Object{if($_-cmatch'^[/-]MD$'){'-MT'}else{$_}});Must-Reject {Assert-SemanticRow $sdkRow.file $sdkRow.output $bad}}
foreach($crt in @('-MD','-MDd','-MTd','-mt')){Test-Case ('Doctor-wrong-CRT-'+$crt) {[string[]]$bad=@($doctorArgs|ForEach-Object{if($_-ceq'-MT'){$crt}else{$_}});Need (-not(Test-RuntimeDoctorCompileOptions $bad Release)) 'Doctor wrong CRT must reject'}}
foreach($extra in @('-MD','-MT','-MTd','-fms-runtime-lib=static','/clang:-fms-runtime-lib=dll','-clang:-fms-runtime-lib=static_dbg','-Xclang=-fms-runtime-lib=static_dbg','/clang:-Xclang=-fms-runtime-lib=static_dbg','/clang:-MTd')){Test-Case ('Doctor-mixed-or-override-'+$extra) {[string[]]$bad=@($doctorArgs)+@($extra);Need (-not(Test-RuntimeDoctorCompileOptions $bad Release)) 'Doctor duplicate/override must reject'}}
foreach($macro in @('UNICODE','_UNICODE','NOMINMAX','WINVER','_WIN32_WINNT')){Test-Case ('Doctor-missing-macro-'+$macro) {[string[]]$bad=@($doctorArgs|Where-Object{$_-cnotmatch('^[/-]D'+[regex]::Escape($macro)+'(?:=|$)')});Need (-not(Test-RuntimeDoctorCompileOptions $bad Release)) 'Doctor required macro must reject'}}
foreach($extra in @('/UUNICODE','/U_UNICODE','-DUNICODE=0','-DWINVER=0x0602','-D_WIN32_WINNT=0x0602')){Test-Case ('Doctor-conflicting-macro-'+$extra) {Need (-not(Test-RuntimeDoctorCompileOptions (@($doctorArgs)+@($extra)) Release)) 'Doctor conflicting macro must reject'}}
Test-Case 'Doctor-misleading-config-reject' {[string[]]$bad=@($doctorArgs|ForEach-Object{if($_-cmatch'^[/-]DCMAKE_INTDIR='){'-DCMAKE_INTDIR="Debug"'}else{$_}});Need (-not(Test-RuntimeDoctorCompileOptions $bad Release)) 'Configuration flag must match actual object configuration'}
Test-Case 'Doctor-missing-config-reject' {Need (-not(Test-RuntimeDoctorCompileOptions (@($doctorArgs|Where-Object{$_-cnotmatch'^[/-]DCMAKE_INTDIR='})) Release)) 'Path alone is not config proof'}
Test-Case 'Doctor-repeated-config-reject' {Need (-not(Test-RuntimeDoctorCompileOptions (@($doctorArgs)+@('-DCMAKE_INTDIR="Release"')) Release)) 'Repeated config flag must reject'}
foreach($pair in @(
 @(($root+'/sdk/src/wrong/runtime_profile.cpp'),$profileRow.output),
 @(($root+'/tools/other/main.cpp'),($build+'/tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/Release/main.cpp.obj')),
 @($failedSource,($build+'/tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/Debug/__/__/sdk/src/host/runtime_profile.cpp.obj')),
 @($failedSource,($build+'/tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/Release/runtime_profile.cpp.obj')),
 @($sdkRow.file,($build+'/tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/Release/unknown.cpp.obj'))
 )){Test-Case ('Doctor-exact-pair-reject-'+$pair[1]) {Must-Reject {Test-RuntimeDoctorCompileBinding $pair[0] $pair[1] $root $build Release}}}
Test-Case 'Doctor-main-wrong-target-reject' {Must-Reject {Test-RuntimeDoctorCompileBinding ($root+'/tools/runtime-doctor/main.cpp') ($build+'/other/CMakeFiles/Other.dir/Release/main.cpp.obj') $root $build Release}}
Test-Case 'Doctor-shared-source-wrong-object-MT-reject' {Must-Reject {Assert-SemanticRow $failedSource ($build+'/sdk/CMakeFiles/Other.dir/Release/runtime_profile.cpp.obj') $doctorArgs}}
Test-Case 'shared-SDK-source-still-MD-control' {[string[]]$argv=@($doctorArgs|ForEach-Object{if($_-ceq'-MT'){'-MD'}else{$_}})+@('/U_DEBUG');Need ((Assert-SemanticRow $failedSource ($build+'/sdk/CMakeFiles/soui_sdk.dir/Release/host/runtime_profile.cpp.obj') $argv)-ceq'SDK') 'Shared source is not a filename exemption'}
foreach($crtArgs in @(@('/MD'),@('-MD'),@('/MDd'),@('-MT'),@('/MD','/MT'),@('/MD','/MD'),@('/MDd','/MD'),@('/DNAME=MD'),@('/MD','/clang:-fms-runtime-lib=static'),@('/md'))){Test-Case ('MD-policy-byte-preservation-'+($crtArgs-join'|')) {Need ((Test-CrtOption $crtArgs MD)-eq(Old-Test-CrtOption $crtArgs MD)) 'Existing MD predicate cannot weaken'}}
Test-Case 'Doctor-Debug-explicit-static-control' {[string[]]$argv=@($doctorArgs|ForEach-Object{if($_-cmatch'^[/-]DCMAKE_INTDIR='){'-DCMAKE_INTDIR="Debug"'}else{$_}});Need (Test-RuntimeDoctorCompileBinding $failedSource ($build+'/tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/Debug/__/__/sdk/src/host/runtime_profile.cpp.obj') $root $build Debug) 'Exact Debug pair';Need (Test-RuntimeDoctorCompileOptions $argv Debug) 'CMake fixes static nondebug CRT in all Doctor configurations'}
$report=[ordered]@{schema='soui.runtime-doctor-compile-contract-pure-results/1';sourceOnly=$true;sourceAuthor='/root/host64_observer_review';classification='Actual PS Parser extraction and pure predicate calls over immutable original DB commands, plus inert argv/path mutations. No observer entry, compiler, Ninja, Tool identity helper, Owned, SDK or VM runs.';candidate=@{path=$candidate;bytes=(Get-Item -LiteralPath $candidate).Length;sha256=(Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()};originalObserver=@{path=$original;bytes=(Get-Item -LiteralPath $original).Length;sha256=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()};actualOriginalDatabase=$diagnosis.compileDatabase;actualDoctorRows=$doctor;actualSdkPrefixRows=41;actualGrammarRowsChecked=42;genuineOldRedNewGreen=$red;total=$cases.Count;passed=@($cases|Where-Object{$_.passed}).Count;failed=@($cases|Where-Object{-not$_.passed}).Count;cases=$cases.ToArray();actualCompilerInvoked=$false;actualSemanticObserverInvoked=$false;newSdkObservation=$null;actualOriginalWholeAttemptStillFailed=$true;releaseReady=$false}
$fullOutput=[IO.Path]::GetFullPath($OutputPath);if(Test-Path -LiteralPath $fullOutput){throw 'Pure report must be fresh'}
[IO.File]::WriteAllText($fullOutput,($report|ConvertTo-Json -Depth 14)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
@{total=$report.total;passed=$report.passed;failed=$report.failed;actualGrammarRows=42;oldAccepted=$red.oldAccepted;newPolicy=$red.newPolicy;actualNativeInvoked=$false}|ConvertTo-Json -Compress
if($report.failed-ne0){exit 1}else{exit 0}
