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
foreach($name in @('Need','Norm','Full','NinjaLogicalLines','NinjaWords','NinjaBuildEdges','CompileBindings','NinjaDependencyBlocks','DependencyBindings','ToolNorm','CompileArgv','CompileNorm','CompileVerboseNorm','CompileVerboseRecords')){
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
$record=[ordered]@{schema='soui.pure-standard-ninja-reader-regression/v1';source=$source;cases=$results.ToArray();count=$results.Count;nativeOrToolFileIdentityTested=$false;qualificationPassed=$false;releaseReady=$false}
$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record|ConvertTo-Json -Depth 6))
$stream=[IO.File]::Open($OutputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
