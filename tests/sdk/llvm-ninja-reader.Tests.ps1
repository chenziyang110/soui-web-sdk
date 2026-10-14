param([Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))
if(Test-Path -LiteralPath $OutputPath){throw 'Fresh pure-reader result required'}
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Real reader source has parse errors'}
# Load only the actual pure parsing functions; never invoke the Observer body,
# tool identity helper, native tools or a copied implementation of the parser.
foreach($name in @('Need','Norm','Full','NinjaLogicalLines','NinjaLiteralValue','BoundNinjaWorkdir','NinjaWorkdirScope','NinjaWords','NinjaBuildEdges','CompileBindings','NinjaDependencyBlocks','DependencyBindings','ToolNorm','CompileArgv','CompileNorm','CompileVerboseNorm','CompileVerboseRecords','DatabaseCommand','ResourceDatabaseArgs','ResourceQuery','ResourceQueryNorm','ResourceBindings','ResourceVerboseRecords','ResourceActual','ResourceInput')){
 $definitions=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true))
 if($definitions.Count-ne1){throw 'Unique actual pure reader function required'}
 . ([ScriptBlock]::Create($definitions[0].Extent.Text))
}
$results=[Collections.Generic.List[object]]::new()
$aliases=@{} # Pure normalization cannot create a physical tool alias proof.
function Case([string]$name,[scriptblock]$work,[string]$reject=''){
 $message=$null
 try{&$work|Out-Null}catch{$message=$_.Exception.Message}
 if($reject){if(-not$message-or-not$message.Contains($reject)){throw ('Wrong rejection: '+$name+' '+$message)}}
 elseif($message){throw ('Positive case failed: '+$name+' '+$message)}
 $results.Add([ordered]@{name=$name;passed=$true;rejection=$message})
}
$build='F:/ninja-reader-fixture/build'
$command=[pscustomobject]@{directory=$build;file='F:/src folder/a$b.cpp';output=($build+'/obj file.obj')}
$valid='build obj$ file.obj: CXX_COMPILER_r F$:/src$ folder/a$$b.cpp || dep'
Case 'colon-space-dollar-source-object' {Need (@(CompileBindings $build @($command) @(NinjaBuildEdges $valid)).Count-eq1) 'binding'}
Case 'continuation' {$c=[pscustomobject]@{directory=$build;file='F:/src/a.c';output=($build+'/a.obj')};Need (@(CompileBindings $build @($c) @(NinjaBuildEdges ('build a.obj: C_COMPILER_r F$:/src/$'+"`n"+'  a.c'))).Count-eq1) 'continuation'}
Case 'zero-header-valid-block' {Need ((DependencyBindings $build @($command) 'obj file.obj: #deps 0, deps mtime 1 (VALID)').selected.Count-eq1) 'zero headers'}
Case 'relative-header-resolves' {$x=(DependencyBindings $build @($command) ('obj file.obj: #deps 1, deps mtime 1 (VALID)'+"`n"+'    ../include/a.h')).selected[0];Need ((Norm $x.headers[0].resolved)-ceq'f:/ninja-reader-fixture/include/a.h') 'cwd'}
Case 'variable-rejected' {NinjaBuildEdges 'build a.obj: C_COMPILER_r $src'} 'Unsupported Ninja variable'
Case 'unknown-escape-rejected' {NinjaBuildEdges 'build a.obj: C_COMPILER_r F$:/src/$x.c'} 'Unsupported Ninja variable'
Case 'unterminated-continuation' {NinjaBuildEdges 'build a.obj: C_COMPILER_r F$:/src/a.c$'} 'Unterminated Ninja continuation'
Case 'missing-colon' {NinjaBuildEdges 'build a.obj C_COMPILER_r a.c'} 'Malformed Ninja build edge'
Case 'duplicate-compile-edge' {CompileBindings $build @($command) @(NinjaBuildEdges ($valid+"`n"+$valid))} 'Selected Ninja compiler object missing/duplicate'
Case 'wrong-source-edge' {CompileBindings $build @($command) @(NinjaBuildEdges $valid.Replace('a$$b.cpp','b.cpp'))} 'Actual edge source/object pair'
Case 'wrong-language-rule' {CompileBindings $build @($command) @(NinjaBuildEdges $valid.Replace('CXX_COMPILER','C_COMPILER'))} 'Actual compiler language'
Case 'unknown-extra-selected-compile-edge' {CompileBindings $build @($command) @(NinjaBuildEdges ($valid+"`n"+'build extra.obj: C_COMPILER_r F$:/src/a.c'))} 'Selected Ninja compiler object missing/duplicate'
Case 'duplicate-command-output' {CompileBindings $build @($command,$command) @(NinjaBuildEdges $valid)} 'Unique current build object'
Case 'stale-required-dependency-object' {DependencyBindings $build @($command) 'obj file.obj: #deps 0, deps mtime 1 (STALE)'} 'must be VALID'
Case 'missing-dependency-object' {DependencyBindings $build @($command) ''} 'Every current compiler object'
Case 'duplicate-dependency-object' {DependencyBindings $build @($command) ('obj file.obj: #deps 0, deps mtime 1 (VALID)'+"`n"+'obj file.obj: #deps 0, deps mtime 1 (VALID)')} 'Duplicate actual Ninja dependency block'
Case 'header-count-short' {NinjaDependencyBlocks 'obj file.obj: #deps 1, deps mtime 1 (VALID)'} 'Ninja header count differs'
Case 'header-count-excess' {NinjaDependencyBlocks ('obj file.obj: #deps 0, deps mtime 1 (VALID)'+"`n"+'    ../a.h')} 'Excess Ninja dependency header'
Case 'nonindented-header' {NinjaDependencyBlocks ('obj file.obj: #deps 1, deps mtime 1 (VALID)'+"`n"+'../a.h')} 'Unsupported Ninja dependency line'
Case 'other-configuration-dependency-is-recorded-without-accepting-it' {
 $x=DependencyBindings $build @($command) ('obj file.obj: #deps 0, deps mtime 1 (VALID)'+"`n"+'other.obj: #deps 0, deps mtime 1 (STALE)')
 Need ($x.selected.Count-eq1-and$x.outsideScope.Count-eq1-and$x.outsideScope[0].status-ceq'STALE') 'outside scope classification'
}
Case 'outside-build-object-is-rejected' {$x=[pscustomobject]@{directory=$build;file=$command.file;output='F:/outside.obj'};CompileBindings $build @($x) @(NinjaBuildEdges $valid)} 'Unique current build object'
Case 'compile-cwd-mismatch-is-rejected' {$x=[pscustomobject]@{directory='F:/other';file=$command.file;output=$command.output};CompileBindings $build @($x) @(NinjaBuildEdges $valid)} 'Actual compile database cwd/source'
$compileDb='"C:\tool space\clang-cl.exe" --target=x86_64-pc-windows-msvc /nologo -DCMAKE_INTDIR=\"Release\" /MD /imsvc"C:/SDK include" /FoCMakeFiles\real.dir\Release\a.cpp.obj /Fdobj\ -c -- "F:\source space\a.cpp"'
$compileActual=$compileDb.Replace(' /MD ',('  /MD'+[char]9)).Replace(' /Fo',' /showIncludes /Fo')
Case 'compile-whitespace-and-one-dependency-flag' {Need ((CompileNorm $compileDb)-ceq(CompileNorm $compileActual $true)) 'exact argv'}
Case 'compile-keeps-trailing-backslash-argument-boundary' {$x=@(CompileArgv 'clang-cl.exe /Fdobj\ -c -- a.cpp');Need ($x.Count-eq5-and$x[1]-ceq'/Fdobj\'-and$x[2]-ceq'-c') 'CRT boundary'}
Case 'compile-preserves-quoted-and-empty-literal-arguments' {$x=@(CompileArgv 'clang-cl.exe -DNAME=\"x\" "a""b" ""');Need ($x.Count-eq4-and$x[1]-ceq'-DNAME="x"'-and$x[2]-ceq'a"b'-and$x[3]-ceq'') 'literal argv'}
Case 'compile-actual-missing-dependency-flag' {CompileNorm $compileDb $true} 'Exact actual/database compile dependency flag contract'
Case 'compile-actual-duplicate-dependency-flag' {CompileNorm ($compileActual.Replace('/showIncludes','/showIncludes /showIncludes')) $true} 'Exact actual/database compile dependency flag contract'
Case 'compile-database-must-not-have-dependency-flag' {CompileNorm $compileActual} 'Exact actual/database compile dependency flag contract'
Case 'compile-response-file-rejected' {CompileNorm ($compileActual+' @flags.rsp') $true} 'Direct compile command without response file'
Case 'compile-unmatched-quote-rejected' {CompileArgv 'clang-cl.exe "unterminated'} 'Unterminated compile command quote'
Case 'compile-wrong-compiler-path-remains-different' {Need ((CompileNorm $compileDb)-cne(CompileNorm ($compileActual.Replace('C:\tool space','C:\other tool')) $true)) 'compiler path'}
Case 'compile-wrong-CRT-remains-different' {Need ((CompileNorm $compileDb)-cne(CompileNorm ($compileActual.Replace('/MD','/MT')) $true)) 'CRT'}
Case 'compile-wrong-source-output-define-include-remains-different' {
 foreach($changed in @($compileActual.Replace('a.cpp.obj','b.cpp.obj'),$compileActual.Replace('source space\a.cpp','source space\b.cpp'),$compileActual.Replace('Release','Debug'),$compileActual.Replace('SDK include','other include'))){Need ((CompileNorm $compileDb)-cne(CompileNorm $changed $true)) 'changed compile argument'}
}
Case 'compile-extra-flag-and-order-remains-different' {Need ((CompileNorm $compileDb)-cne(CompileNorm ($compileActual+' /DUNRELATED=1') $true)-and(CompileNorm $compileDb)-cne(CompileNorm ($compileActual.Replace(' /nologo','')+' /nologo') $true)) 'order or extra flag'}
Case 'compile-only-progress-record-is-actual-verbose' {Need ((CompileNorm $compileDb)-ceq(CompileVerboseNorm ('[2/22] '+$compileActual))-and(CompileNorm $compileDb)-cne(CompileVerboseNorm $compileActual)-and(CompileNorm $compileDb)-cne(CompileVerboseNorm ('diagnostic mentions '+$compileActual))) 'progress-prefixed actual command'}
Case 'compile-invalid-progress-rejected' {CompileVerboseNorm ('[23/22] '+$compileActual)} 'Invalid Ninja progress prefix'
Case 'compile-bare-canonical-json-is-not-query-evidence' {$canonical=CompileNorm $compileDb;Need ($null-eq(CompileNorm $canonical $true)) 'bare canonical JSON rejected'}
Case 'compile-bare-canonical-json-is-not-verbose-evidence' {$canonical=CompileNorm $compileDb;Need ($null-eq(CompileVerboseNorm $canonical)) 'bare canonical JSON rejected'}
Case 'compile-progress-canonical-json-is-not-clang-evidence' {$canonical=CompileNorm $compileDb;Need ($null-eq(CompileVerboseNorm ('[2/22] '+$canonical))) 'JSON is not an executable command'}
Case 'compile-eligible-record-retains-its-own-raw-line-after-diagnostics' {
 $canonical=CompileNorm $compileDb;$records=@(CompileVerboseRecords ($canonical+"`n"+'diagnostic mentions '+$compileActual+"`n"+'[2/22] '+$compileActual))
 Need ($records.Count-eq1-and$records[0].canonical-ceq$canonical-and$records[0].raw-ceq$compileActual) 'original actual record association'
}

$fixture=([IO.File]::ReadAllText((Join-Path $PSScriptRoot '../benchmark/fixtures/ninja-workdir-reader.json'))|ConvertFrom-Json);$nl=[Environment]::NewLine
$fixtureImpl=$fixture.implInclude+$nl+($fixture.actualBuildLines-join$nl)
$fixtureCommon=$fixture.commonInclude+$nl+$fixture.definition
Case 'nine-real-CUSTOM_COMMAND-rows-literal-implicit-outputs' {
 $binding=NinjaWorkdirScope $fixtureImpl $fixtureCommon '' $fixture.buildRoot
 $parsed=@(NinjaBuildEdges $fixtureImpl $fixture.buildRoot $binding.value)
 Need ($parsed.Count-eq9) 'actual nine rows'
 for($i=0;$i-lt9;$i++){Need ($parsed[$i].rawLogical-ceq$fixture.actualBuildLines[$i]-and$parsed[$i].rule-ceq'CUSTOM_COMMAND'-and$parsed[$i].implicitOutputs.Count-eq1-and$parsed[$i].implicitOutputs[0].StartsWith($binding.value,[StringComparison]::Ordinal)) 'raw row and implicit output'}
}
Case 'expanded-colon-space-dollar-is-one-literal-path' {
 $b='F:/ninja reader/$literal/build';$row='build a | '+('$'+'{cmake_ninja_workdir}')+'sidecar: rule source'
 $binding=NinjaWorkdirScope ('include CMakeFiles/common.ninja'+$nl+$row) ('include CMakeFiles/rules.ninja'+$nl+'cmake_ninja_workdir = F$:/ninja$ reader/$$literal/build/') '' $b
 $edge=@(NinjaBuildEdges $row $b $binding.value)[0];Need ($edge.implicitOutputs.Count-eq1-and$edge.implicitOutputs[0]-ceq'F:/ninja reader/$literal/build/sidecar') 'literal expansion'
 $edge=@(NinjaBuildEdges 'build a | $cmake_ninja_workdir/sidecar: rule source' $b $binding.value)[0];Need ($edge.implicitOutputs[0]-ceq'F:/ninja reader/$literal/build//sidecar') 'unbraced variable'
}
Case 'known-value-does-not-authorize-unknown-variable' {NinjaBuildEdges 'build a: rule $foreign' $fixture.buildRoot 'D:/work/sm05-sdk-native-input-normalized-v2/out/x86/'} 'Unsupported Ninja variable'
Case 'known-value-must-equal-bound-build-root' {NinjaBuildEdges 'build a: rule $cmake_ninja_workdir' $fixture.buildRoot 'F:/foreign/'} 'bound build root'
Case 'missing-common-definition' {NinjaWorkdirScope $fixtureImpl $fixture.commonInclude '' $fixture.buildRoot} 'Unique top-level'
Case 'duplicate-common-definition' {NinjaWorkdirScope $fixtureImpl ($fixtureCommon+$nl+$fixture.definition) '' $fixture.buildRoot} 'Unique top-level'
Case 'indented-common-shadow' {NinjaWorkdirScope $fixtureImpl ($fixture.commonInclude+$nl+'  '+$fixture.definition) '' $fixture.buildRoot} 'shadow'
Case 'impl-shadow' {NinjaWorkdirScope ($fixtureImpl+$nl+$fixture.definition) $fixtureCommon '' $fixture.buildRoot} 'shadow'
Case 'rules-shadow' {NinjaWorkdirScope $fixtureImpl $fixtureCommon ('  '+$fixture.definition) $fixture.buildRoot} 'shadow'
Case 'foreign-common-include' {NinjaWorkdirScope $fixtureImpl.Replace('CMakeFiles/common.ninja','../foreign.ninja') $fixtureCommon '' $fixture.buildRoot} 'Foreign Ninja'
Case 'subninja-is-a-foreign-scope' {NinjaWorkdirScope $fixtureImpl.Replace('include ','subninja ') $fixtureCommon '' $fixture.buildRoot} 'Foreign Ninja'
Case 'extra-common-include' {NinjaWorkdirScope $fixtureImpl ($fixtureCommon+$nl+'include foreign.ninja') '' $fixture.buildRoot} 'Foreign Ninja'
Case 'extra-rules-include' {NinjaWorkdirScope $fixtureImpl $fixtureCommon 'include foreign.ninja' $fixture.buildRoot} 'Foreign Ninja'
Case 'workdir-used-before-include' {NinjaWorkdirScope ($fixture.actualBuildLines[0]+$nl+$fixture.implInclude) $fixtureCommon '' $fixture.buildRoot} 'declared include scope'
Case 'workdir-used-before-definition-in-rules' {NinjaWorkdirScope $fixtureImpl $fixtureCommon 'build a: rule $cmake_ninja_workdir/source' $fixture.buildRoot} 'declared include scope'
Case 'foreign-workdir-value' {NinjaWorkdirScope $fixtureImpl $fixtureCommon.Replace($fixture.definition,'cmake_ninja_workdir = F$:/foreign/') '' $fixture.buildRoot} 'bound build root'
Case 'definition-cannot-expand-unknown-variable' {NinjaWorkdirScope $fixtureImpl $fixtureCommon.Replace($fixture.definition,'cmake_ninja_workdir = $foreign') '' $fixture.buildRoot} 'Unsupported Ninja variable'


Case 'known-workdir-value-itself-is-drive-absolute' {
 $b=[IO.Path]::GetFullPath((Get-Location).Path)
 foreach($value in @('./',('../'+[IO.Path]::GetFileName($b)+'/'),($b.Substring(2).Replace('\','/')+'/'),($b.Substring(0,2)+'relative/'))){
  $rejected=$false;try{BoundNinjaWorkdir $b $value|Out-Null}catch{$rejected=$_.Exception.Message.Contains('bound build root')};Need $rejected 'relative/root-relative/drive-relative value must reject'
  $rejected=$false;try{NinjaWorkdirScope 'include CMakeFiles/common.ninja' ('include CMakeFiles/rules.ninja'+[Environment]::NewLine+'cmake_ninja_workdir = '+$value.Replace(':','$:')) '' $b|Out-Null}catch{$rejected=$_.Exception.Message.Contains('bound build root')};Need $rejected 'relative declaration must reject'
 }
}

# These aliases are explicit inert test inputs. They cannot establish physical
# identity or satisfy a real Owned process role, and no helper is loaded.
$aliases=@{'c:/sdkto~1/rc.exe'='c:/sdk tools/rc.exe';'c:/sdk tools/rc.exe'='c:/sdk tools/rc.exe';'c:/cmake~1/bin/cmcldeps.exe'='c:/cmake tools/bin/cmcldeps.exe';'c:/cmake tools/bin/cmcldeps.exe'='c:/cmake tools/bin/cmcldeps.exe';'c:/llvm tools/clang-cl.exe'='c:/llvm tools/clang-cl.exe'}
$rb='F:/resource-reader-fixture/build'
$rcTail='-DCMAKE_INTDIR=\"Release\" -I "F:\source folder\include" /fo "obj folder\demo.rc.res" "F:\source folder\demo.rc"'
$rcDb='"C:\SDK Tools\rc.exe" '+$rcTail
$rc=[pscustomobject]@{directory=$rb;file='F:/source folder/demo.rc';output=($rb+'/obj folder/demo.rc.res');command=$rcDb}
$rcEdge=@(NinjaBuildEdges 'build obj$ folder/demo.rc.res: RC_COMPILER_r F$:/source$ folder/demo.rc')[0]
$rcWrapped='"C:\CMAKE~1\bin\cmcldeps.exe" RC "F:\source folder\demo.rc" "obj folder\demo.rc.res.d" "obj folder\demo.rc.res" "Note: including file: " "C:\LLVM Tools\clang-cl.exe" "C:\SDKTO~1\rc.exe" '+$rcTail
Case 'database-classifies-clang-and-rc-without-discarding-rc' {Need ((DatabaseCommand $compileDb).kind-ceq'CLANG'-and(DatabaseCommand $rcDb).kind-ceq'RC') 'classification'}
Case 'unknown-database-program-rejected' {DatabaseCommand 'cmd.exe /c rc.exe /fo a.res a.rc'} 'fixed direct CLANG or RC'
Case 'database-response-and-shell-rejected' {foreach($value in @($rcDb+' @flags.rsp',$rcDb+' && other.exe')){$rejected=$false;try{DatabaseCommand $value|Out-Null}catch{$rejected=$_.Exception.Message.Contains('without response file or shell')};Need $rejected 'direct database only'}}
Case 'direct-rc-binds-complete-database-edge-query' {$x=ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcDb));Need ($x.selected.Count-eq1-and$x.outsideScope.Count-eq0-and$x.selected[0].query.raw-ceq$rcDb) 'direct RC binding'}
Case 'cmcldeps-eight-fixed-slots-and-entire-db-suffix-bind' {$x=ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcWrapped));Need ($x.selected.Count-eq1-and$x.selected[0].query.kind-ceq'cmcldeps-RC'-and$x.selected[0].query.arguments[5]-ceq'Note: including file: ') 'wrapped RC binding'}
Case 'rc-link-substring-is-never-a-resource-candidate' {$link='cmd.exe /C "C:\CMake Tools\bin\cmake.exe -E vs_link_exe --rc=C:\SDK Tools\rc.exe linker.exe obj folder\demo.rc.res"';Need ($null-eq(ResourceQuery $link)) 'link is not resource compile'}
Case 'rc-no-query-retains-outside-default-graph-declaration' {$x=ResourceBindings $rb @($rc) @($rcEdge) @();Need ($x.selected.Count-eq0-and$x.outsideScope.Count-eq1-and$x.outsideScope[0].output-ceq$rc.output) 'uncompiled declaration retained'}
Case 'rc-duplicate-database-output-rejected' {ResourceBindings $rb @($rc,$rc) @($rcEdge) @((ResourceQuery $rcWrapped))} 'Unique RC database'
Case 'rc-duplicate-generated-command-rejected' {ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcWrapped),(ResourceQuery $rcWrapped))} 'Unique generated RC'
Case 'rc-duplicate-edge-rejected' {ResourceBindings $rb @($rc) @($rcEdge,$rcEdge) @((ResourceQuery $rcWrapped))} 'one matching resource edge'
Case 'rc-missing-edge-rejected' {ResourceBindings $rb @($rc) @() @((ResourceQuery $rcWrapped))} 'one matching resource edge'
Case 'rc-edge-source-mismatch-rejected' {$bad=@(NinjaBuildEdges 'build obj$ folder/demo.rc.res: RC_COMPILER_r F$:/source$ folder/other.rc')[0];ResourceBindings $rb @($rc) @($bad) @((ResourceQuery $rcWrapped))} 'Exact RC edge'
Case 'rc-query-entire-suffix-extra-or-reordered-arguments-reject' {foreach($changed in @($rcWrapped+' -DEXTRA=1',$rcWrapped.Replace('-I "F:\source folder\include" /fo','/fo'))){$rejected=$false;try{ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $changed))|Out-Null}catch{$rejected=$_.Exception.Message.Contains('Complete RC query argv')};Need $rejected 'whole suffix contract'}}
Case 'rc-foreign-inner-tool-path-does-not-normalize-by-basename' {ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcWrapped.Replace('C:\SDKTO~1\rc.exe','C:\other\rc.exe')))} 'Complete RC query argv'
Case 'rc-wrapper-language-prefix-and-compiler-slots-reject' {foreach($changed in @($rcWrapped.Replace('" RC ','" CXX '),$rcWrapped.Replace('Note: including file: ','other prefix'),$rcWrapped.Replace('clang-cl.exe','not-clang.exe'))){$rejected=$false;try{ResourceQuery $changed|Out-Null}catch{$rejected=$_.Exception.Message.Contains('Fixed cmcldeps RC wrapper slots')};Need $rejected 'fixed wrapper slots'}}
Case 'rc-wrapper-source-output-and-depfile-slots-reject' {foreach($changed in @($rcWrapped.Replace('RC "F:\source folder\demo.rc"','RC "F:\source folder\other.rc"'),$rcWrapped.Replace('demo.rc.res.d','other.rc.res.d'),$rcWrapped.Replace('"obj folder\demo.rc.res" "Note:','"obj folder\other.rc.res" "Note:'))){$rejected=$false;try{ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $changed))|Out-Null}catch{$rejected=$_.Exception.Message.Contains('Exact cmcldeps RC source/output/depfile')};Need $rejected 'wrapper positional operands'}}
Case 'rc-database-cwd-source-output-and-fo-are-independent-contracts' {foreach($field in @('directory','file','output','command')){$bad=[pscustomobject]@{directory=$rc.directory;file=$rc.file;output=$rc.output;command=$rc.command};switch($field){directory{$bad.directory='F:/other'}file{$bad.file='F:/source folder/other.rc'}output{$bad.output='F:/outside/demo.rc.res'}command{$bad.command=$rcDb.Replace('/fo ','/fo other.res /fo ')}};$rejected=$false;try{ResourceDatabaseArgs $bad $rb|Out-Null}catch{$rejected=$true};Need $rejected 'independent RC DB field validation'}}
Case 'rc-verbose-needs-progress-and-one-whole-vector-match' {$binding=(ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcWrapped))).selected[0];Need (@(ResourceActual $binding @(ResourceVerboseRecords ($rcWrapped+"`n"+'diagnostic mentions '+$rcWrapped))).Count-eq0) 'bare/diagnostic is not actual command';Need (@(ResourceActual $binding @(ResourceVerboseRecords ('[2/22] '+$rcWrapped))).Count-eq1) 'progress actual';Need (@(ResourceActual $binding @(ResourceVerboseRecords ('[2/22] '+$rcWrapped+' EXTRA'))).Count-eq0) 'whole argv only'}
Case 'rc-duplicate-actual-verbose-rejected' {$binding=(ResourceBindings $rb @($rc) @($rcEdge) @((ResourceQuery $rcWrapped))).selected[0];ResourceActual $binding @(ResourceVerboseRecords ('[2/22] '+$rcWrapped+"`n"+'[3/22] '+$rcWrapped))} 'Resource command observed repeatedly'
Case 'rc-invalid-progress-rejected' {ResourceVerboseRecords ('[23/22] '+$rcWrapped)} 'Invalid Ninja progress prefix'
Case 'selected-binary-resource-input-requires-actual-bound-resource-map' {$pin=[ordered]@{path=$rc.output;bytes=17;sha256='synthetic-only'};$resources=@{};$resources[(Norm $rc.output)]=$pin;Need ((ResourceInput $rc.output $resources).path-ceq$rc.output) 'bound input membership'}
Case 'selected-binary-cannot-omit-an-unbound-res-input' {ResourceInput $rc.output @{}} 'complete RC database/query binding'
Case 'rc-database-root-relative-source-rejected-before-normalization' {
 $cwd=[IO.Path]::GetFullPath((Get-Location).Path);$saved=[Environment]::CurrentDirectory
 try{
  [Environment]::CurrentDirectory=$cwd;$absolute=Join-Path $cwd 'ambient-source.rc';$relative=$absolute.Substring(2)
  Need ([IO.Path]::IsPathRooted($relative)-and$relative-notmatch'^[A-Za-z]:[\\/]'-and(Norm (Full $relative $cwd))-ceq(Norm $absolute)) 'meaningful legacy rooted/same-source precondition'
  $db=[pscustomobject]@{directory=$cwd;file=$relative;output=(Join-Path $cwd 'Release/ambient-source.rc.res');command=('"C:\SDK Tools\rc.exe" /fo "'+(Join-Path $cwd 'Release/ambient-source.rc.res')+'" "'+$absolute+'"')}
  ResourceDatabaseArgs $db $cwd
 }finally{[Environment]::CurrentDirectory=$saved}
} 'RC database cwd/source required'
Case 'rc-database-drive-relative-source-rejected-before-normalization' {
 $cwd=[IO.Path]::GetFullPath((Get-Location).Path);$saved=[Environment]::CurrentDirectory
 try{
  [Environment]::CurrentDirectory=$cwd;$absolute=Join-Path $cwd 'ambient-source.rc';$relative=$absolute.Substring(0,2)+'ambient-source.rc'
  Need ([IO.Path]::IsPathRooted($relative)-and$relative-notmatch'^[A-Za-z]:[\\/]'-and(Norm (Full $relative $cwd))-ceq(Norm $absolute)) 'meaningful legacy rooted/same-source precondition'
  $db=[pscustomobject]@{directory=$cwd;file=$relative;output=(Join-Path $cwd 'Release/ambient-source.rc.res');command=('"C:\SDK Tools\rc.exe" /fo "'+(Join-Path $cwd 'Release/ambient-source.rc.res')+'" "'+$absolute+'"')}
  ResourceDatabaseArgs $db $cwd
 }finally{[Environment]::CurrentDirectory=$saved}
} 'RC database cwd/source required'
$aliases=@{}
$record=[ordered]@{schema='soui.pure-standard-ninja-reader-regression/v1';source=$source;cases=$results.ToArray();count=$results.Count;resourceAliasesAreExplicitSynthetic=$true;nativeOrToolFileIdentityTested=$false;qualificationPassed=$false;releaseReady=$false}
$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record|ConvertTo-Json -Depth 6))
$stream=[IO.File]::Open($OutputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
