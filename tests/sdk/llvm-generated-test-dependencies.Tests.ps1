[CmdletBinding()]
param([Parameter(Mandatory)][string]$CandidatePath,[string]$BaselinePath='',[string]$FailureDirectory='',[string]$RecipeRoot='')
$ErrorActionPreference='Stop'
function Need([bool]$value,[string]$message){if(-not$value){throw $message}}
function Norm([string]$value){$value.Replace('\','/').ToLowerInvariant()}
function Full([string]$value,[string]$base){if([IO.Path]::IsPathRooted($value)){return [IO.Path]::GetFullPath($value)};[IO.Path]::GetFullPath((Join-Path $base $value))}
function Declarations([string]$path,[string[]]$names){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors);Need ($errors.Count-eq0) 'PowerShell syntax'
 foreach($name in $names){$nodes=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true));Need ($nodes.Count-eq1) ('Unique pure declaration: '+$name);$nodes[0].Extent.Text}
}
$names=@('GeneratedTestHash','GeneratedTestRelative','GeneratedTestFrozen','GeneratedTestWriter','GeneratedTestInterval','GeneratedTestMarker','GeneratedTestActual','GeneratedTestWorkerShutdown','GeneratedTestRecipes','GeneratedTestRecipeMarker','GeneratedTestRecipeDependencies','GeneratedTestDependencies','NinjaDependencyBlocks','DependencyBindings')
foreach($declaration in @(Declarations $CandidatePath $names)){. ([scriptblock]::Create($declaration))}
if($RecipeRoot){
 # New recipe SOURCE contracts use authored graph/dependency records and actual
 # frozen writer/source bytes. No build output, source-binding manifest, CMake,
 # generator, compiler or observer entry point is used. Delivered unexecuted.
 $root=[IO.Path]::GetFullPath($RecipeRoot);$build=Join-Path $root 'out/authored-observer-recipe';$configuration='Release'
 $recipes=@(GeneratedTestRecipes);$pins=@{};$physical=@{};$records=@();$pairs=@();$cases=[Collections.Generic.List[object]]::new();$failed=0
 $expectedCounts=@{'scheduler-continuation'=8;'scheduler-index'=0;'window-close-lifetime'=8;'independent-wheel'=18;'host-visibility'=11;'render-diagnostic'=3;'provider-deadline'=5;'frame-buffer-copy'=4}
 function FreezeRecipeInput([string]$path){$key=Norm $path;if($pins.ContainsKey($key)){return};$bytes=[IO.File]::ReadAllBytes($path);$pins[$key]=@{path=$path;bytes=$bytes.Length;sha256=(GeneratedTestHash $bytes)};$physical[$key]=$bytes}
 foreach($role in $recipes){
  FreezeRecipeInput (Join-Path $root ($role.directory+'/CMakeLists.txt'))
  if($null-ne$role.writer){FreezeRecipeInput (Join-Path $root ($role.directory+'/generate.cjs'))}
  foreach($source in $role.whole.Keys){FreezeRecipeInput (Join-Path $root ('third_party/sources/miniblink/'+$source))}
  foreach($part in $role.fragments){FreezeRecipeInput (Join-Path $root ('third_party/sources/miniblink/'+$part[0]))}
  $outputs=@($role.whole.Values)+@($role.fragments|ForEach-Object{$_[1]});$base=Join-Path $build ($role.directory+'/generated')
  foreach($unit in $role.units){
   $object=Join-Path $build ($role.directory+'/CMakeFiles/'+$role.target+'.dir/'+$configuration+'/'+$unit+'.obj')
   $isGenerated=$unit.StartsWith('generated/',[StringComparison]::Ordinal)
   $source=Join-Path $(if($isGenerated){$build}else{$root}) ($role.directory+'/'+$unit)
   $headers=@();if(-not$isGenerated){foreach($output in $outputs){if($output-notmatch'\.cpp$'){$headers+=@{resolved=(Join-Path $base $output)}}}}
   $records+=@{object=$object;status='VALID';headers=$headers};$pairs+=@{object=$object;source=$source}
  }
 }
 $reader={param($path)Need ($physical.ContainsKey((Norm $path))) 'Only authored frozen source reads';return ,$physical[(Norm $path)]}
 function CheckRecipe([string]$name,[scriptblock]$body){try{&$body|Out-Null;$cases.Add(@{name=$name;passed=$true})}catch{$script:failed++;$cases.Add(@{name=$name;passed=$false;error=[string]$_})}}
 function RejectRecipe([scriptblock]$body,[string]$reason){$message=$null;try{&$body|Out-Null}catch{$message=[string]$_};Need ($null-ne$message-and$message.Contains($reason,[StringComparison]::Ordinal)) ('Expected strict recipe rejection: '+$message)}
 function CloneRecipe($value){$value|ConvertTo-Json -Depth 100 -Compress|ConvertFrom-Json -AsHashtable}
 function RecipeRows($selected=$records,$bindings=$pairs,$frozen=$pins,[scriptblock]$read=$reader){@(GeneratedTestDependencies $root $build $configuration $frozen $selected $read $bindings)}
 $derived=@()
 CheckRecipe 'Eight frozen roles cover 57 exact paths and both standalone compiler sources' {
  $script:derived=RecipeRows;Need ($recipes.Count-eq8-and$derived.Count-eq57-and$records.Count-eq10) 'Frozen new-role dimensions'
  foreach($family in $expectedCounts.Keys){Need (@($derived|Where-Object{$_.writerFamily-ceq$family}).Count-eq$expectedCounts[$family]) ('Recipe output extent: '+$family)}
  foreach($suffix in @('/generated/cc/input/ScrollController.cpp','/generated/cc/trees/ScrollTreeSnapshot.cpp')){
   $row=@($derived|Where-Object{(Norm $_.pin.path).EndsWith((Norm $suffix),[StringComparison]::Ordinal)})
   Need ($row.Count-eq1-and$row[0].exactWholeFile-and$row[0].selectedObjects.Count-eq1-and(Norm $row[0].selectedObject).EndsWith(($suffix.Substring('/generated'.Length)+'.obj'),[StringComparison]::OrdinalIgnoreCase)) 'Real generated compiler source admission'
  }
 }
 CheckRecipe 'No selected recipe has no derived permission' {Need ((RecipeRows @() @()).Count-eq0) 'No selected graph'}
 CheckRecipe 'Validated compiler source/object pairs are required' {RejectRecipe {RecipeRows $records @()} 'Exact generated recipe compiler source/object binding'}
 foreach($role in $recipes){
  $targetPrefix=Norm (Join-Path $build ($role.directory+'/CMakeFiles/'+$role.target+'.dir/'+$configuration+'/'))
  $object=($records|Where-Object{(Norm $_.object).StartsWith($targetPrefix,[StringComparison]::Ordinal)}|Select-Object -First 1).object
  CheckRecipe ($role.kind+': mismatched compiler source') {$next=CloneRecipe $pairs;($next|Where-Object{(Norm $_.object)-ceq(Norm $object)}).source=Join-Path $root 'tests/native/unknown.cpp';RejectRecipe {RecipeRows $records $next} 'Exact generated recipe compiler source/object binding'}
  CheckRecipe ($role.kind+': stale dependency object') {$next=CloneRecipe $records;($next|Where-Object{(Norm $_.object)-ceq(Norm $object)}).status='STALE';RejectRecipe {RecipeRows $next} 'Unique selected generated recipe object binding'}
  CheckRecipe ($role.kind+': unknown output and manifest are not authority') {$next=CloneRecipe $records;($next|Where-Object{(Norm $_.object)-ceq(Norm $object)}).headers+=@{resolved=(Join-Path $build ($role.directory+'/generated/source-binding.json'))};RejectRecipe {RecipeRows $next} 'Unknown generated test dependency'}
  $writer=Join-Path $root ($role.directory+'/CMakeLists.txt')
  CheckRecipe ($role.kind+': absent frozen CMake writer') {$next=$pins.Clone();$next.Remove((Norm $writer));RejectRecipe {RecipeRows $records $pairs $next} 'Generated test source was not frozen'}
  if($null-ne$role.writer){
   $writer=Join-Path $root ($role.directory+'/generate.cjs')
   CheckRecipe ($role.kind+': modified generator cannot be blessed by new input pin') {
    $tamperedWriterKey=Norm $writer;$changed=$physical[$tamperedWriterKey]+[byte]10;$next=$pins.Clone();$next[$tamperedWriterKey]=@{path=$writer;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)}
    $read={param($path)if((Norm $path)-ceq$tamperedWriterKey){return ,$changed};return ,$physical[(Norm $path)]}
    RejectRecipe {RecipeRows $records $pairs $next $read} 'Unsupported frozen generated test writer'
   }
   $proof=@($derived|Where-Object{$_.writerFamily-ceq$role.kind-and-not$_.pin.path.EndsWith('.cpp',[StringComparison]::Ordinal)}|Select-Object -First 1)
   if($proof.Count){
    CheckRecipe ($role.kind+': missing complete generated dependency') {$next=CloneRecipe $records;foreach($record in $next){$record.headers=@($record.headers|Where-Object{(Norm $_.resolved)-cne(Norm $proof[0].pin.path)})};RejectRecipe {RecipeRows $next} 'Complete selected generated test dependency set required'}
    CheckRecipe ($role.kind+': duplicate header in one object') {$next=CloneRecipe $records;$record=$next|Where-Object{@($_.headers|Where-Object{(Norm $_.resolved)-ceq(Norm $proof[0].pin.path)}).Count};$record.headers+=@{resolved=$proof[0].pin.path};RejectRecipe {RecipeRows $next} 'Duplicate selected generated test dependency'}
    CheckRecipe ($role.kind+': generated path cannot masquerade as frozen') {$next=$pins.Clone();$next[(Norm $proof[0].pin.path)]=$proof[0].pin;RejectRecipe {RecipeRows $records $pairs $next} 'Generated test output cannot masquerade'}
    CheckRecipe ($role.kind+': mutated actual output remains rejected') {$raw=[byte[]]$proof[0].expectedBytes.Clone();$raw[0]=$raw[0]-bxor1;RejectRecipe {GeneratedTestActual $proof[0] $proof[0].pin $raw} 'differs from frozen exact source bytes'}
   }
  }
 }
 $wheel=$recipes|Where-Object{$_.kind-ceq'independent-wheel'};$wheelPrefix=Norm (Join-Path $build ($wheel.directory+'/CMakeFiles/'+$wheel.target+'.dir/'+$configuration+'/'))
 $cppObject=($records|Where-Object{(Norm $_.object).StartsWith($wheelPrefix,[StringComparison]::Ordinal)-and(Norm $_.object).EndsWith('scrollcontroller.cpp.obj',[StringComparison]::Ordinal)}).object
 CheckRecipe 'Missing standalone generated compiler unit cannot inherit fixture admission' {$next=@($records|Where-Object{(Norm $_.object)-cne(Norm $cppObject)});RejectRecipe {RecipeRows $next} 'Complete selected generated recipe compiler units'}
 CheckRecipe 'Extra compiler unit cannot hide inside a known target' {$next=CloneRecipe $records;$next+=@{object=(Join-Path $build ($wheel.directory+'/CMakeFiles/'+$wheel.target+'.dir/'+$configuration+'/generated/unknown.cpp.obj'));status='VALID';headers=@()};RejectRecipe {RecipeRows $next} 'Complete selected generated recipe compiler units'}
 CheckRecipe 'Known generated source cannot authorize a second unrecognized compiler object' {$next=CloneRecipe $pairs;$copy=CloneRecipe ($pairs|Where-Object{(Norm $_.object)-ceq(Norm $cppObject)});$copy.object=Join-Path $build 'unknown/generated.cpp.obj';$next+=@($copy);RejectRecipe {RecipeRows $records $next} 'cannot authorize another object'}
 CheckRecipe 'Shared generated header records both actual compiler users once' {
  $next=CloneRecipe $records;$header=Join-Path $build ($wheel.directory+'/generated/cc/input/ScrollController.h');($next|Where-Object{(Norm $_.object)-ceq(Norm $cppObject)}).headers+=@{resolved=$header}
  $rows=RecipeRows $next;$proof=@($rows|Where-Object{(Norm $_.pin.path)-ceq(Norm $header)});Need ($rows.Count-eq57-and$proof.Count-eq1-and$proof[0].selectedObjects.Count-eq2) 'Per-object usage is retained without duplicate path permission'
 }
 CheckRecipe 'First following end marker matches writer semantics and preserves raw encoding bytes' {
  $raw=[byte[]](66,10,65,13,10,0,129,10,90,10,90,10);$range=GeneratedTestRecipeMarker $raw 'A' 'Z'
  Need ($range.startByte-eq2-and$range.endByteExclusive-eq8-and[BitConverter]::ToString($range.bytes)-ceq'41-0D-0A-00-81-0A') 'Raw byte slice, not decoded/reformatted source'
  RejectRecipe {GeneratedTestRecipeMarker ([Text.Encoding]::ASCII.GetBytes('A A Z')) 'A' 'Z'} 'one unique begin marker'
  RejectRecipe {GeneratedTestRecipeMarker ([Text.Encoding]::ASCII.GetBytes('Z A')) 'A' 'Z'} 'following end marker'
 }
 CheckRecipe 'Declared EOF suffix preserves exact frozen bytes' {
  $raw=[byte[]](90,10,65,13,10,129,10);$range=GeneratedTestRecipeMarker $raw 'A' ''
  Need ($range.startByte-eq2-and$range.endByteExclusive-eq7-and[BitConverter]::ToString($range.bytes)-ceq'41-0D-0A-81-0A') 'Exact suffix extent'
  RejectRecipe {GeneratedTestRecipeMarker ([Text.Encoding]::ASCII.GetBytes('A A')) 'A' ''} 'one unique begin marker'
 }
 CheckRecipe 'Frame buffer role admits exactly the policy and three complete raw method ranges' {
  $rows=@($derived|Where-Object{$_.writerFamily-ceq'frame-buffer-copy'})
  Need ($rows.Count-eq4-and@($rows|Where-Object{$_.exactWholeFile}).Count-eq1) 'Frame buffer raw output extents'
  foreach($row in $rows){[void](GeneratedTestActual $row $row.pin $row.expectedBytes);Need ($row.selectedObjects.Count-eq1-and-not$row.preBuildFrozen) 'One actual compiler owner per derived frame buffer input'}
 }
 CheckRecipe 'Frame buffer strict writer preserves UTF-8 CRLF bytes and rejects malformed fragments or repeated end' {
  $raw=[byte[]](65,13,10,195,169,10,90,10);$range=GeneratedTestRecipeMarker $raw 'A' 'Z' $true
  Need ([BitConverter]::ToString($range.bytes)-ceq'41-0D-0A-C3-A9-0A') 'Strict decoding must preserve the raw interval'
  RejectRecipe {GeneratedTestRecipeMarker ([byte[]](65,10,129,10,90,10)) 'A' 'Z' $true} 'strict UTF-8 required'
  RejectRecipe {GeneratedTestRecipeMarker ([Text.Encoding]::ASCII.GetBytes('A Z Z')) 'A' 'Z' $true} 'one following end marker'
 }
 CheckRecipe 'Frame buffer whole policy cannot bypass strict UTF-8 with a changed source pin' {
  $source=Join-Path $root 'third_party/sources/miniblink/cc/trees/FrameBufferLeasePolicy.h';$sourceKey=Norm $source
  $changed=$physical[$sourceKey]+[byte]129;$next=$pins.Clone();$next[$sourceKey]=@{path=$source;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)}
  $read={param($path)if((Norm $path)-ceq$sourceKey){return ,$changed};return ,$physical[(Norm $path)]}
  RejectRecipe {RecipeRows $records $pairs $next $read} 'strict UTF-8 required'
 }
 # The current worker graph is distinct from the historical 26/12 capture below.
 # Its shutdown macro is a checked writer transform, never an exact source slice.
 $workerDirectory='tests/native/vendor_lifetime/worker_lifetime'
 foreach($relative in @('CMakeLists.txt','source-bytes.cmake','source-map.json')){FreezeRecipeInput (Join-Path $root ($workerDirectory+'/'+$relative))}
 $workerMap=[Text.UTF8Encoding]::new($false,$true).GetString($physical[(Norm (Join-Path $root ($workerDirectory+'/source-map.json')))])|ConvertFrom-Json
 foreach($workerInput in $workerMap.sources){FreezeRecipeInput (Join-Path $root $workerInput.path)}
 foreach($workerInput in $workerMap.payloads){FreezeRecipeInput (Join-Path $root ($workerDirectory+'/'+$workerInput.path))}
 $workerBase=Join-Path $build ($workerDirectory+'/generated')
 $macroPath=Join-Path $workerBase 'fragments/shutdown_fire_entrypoint.inc'
 $workerHeaders=@($workerMap.copies|ForEach-Object{@{resolved=(Join-Path $workerBase $_.output)}})+@(@{resolved=$macroPath})
 $workerRecords=@(@{object=(Join-Path $build ($workerDirectory+'/CMakeFiles/soui_worker_lifetime_regressions.dir/'+$configuration+'/fixture.cpp.obj'));status='VALID';headers=$workerHeaders})
 function WorkerRows($selected=$workerRecords,$frozen=$pins,[scriptblock]$read=$reader){@(GeneratedTestDependencies $root $build $configuration $frozen $selected $read)}
 $workerDerived=@();$macroProof=$null
 CheckRecipe 'Current worker admits 16 source slices and one separately identified shutdown macro' {
  $script:workerDerived=WorkerRows
  $macros=@($workerDerived|Where-Object{$_.derivationSchema-ceq'soui.worker-shutdown-entrypoint-macro/1'})
  Need ($workerDerived.Count-eq17-and$macros.Count-eq1-and@($workerDerived|Where-Object{$_.derivationSchema-ceq'soui.generated-test-source-bytes/1'}).Count-eq16) 'Current worker source and transform extents'
  $script:macroProof=$macros[0]
  Need ($null-eq$macroProof.startByte-and$null-eq$macroProof.endByteExclusive-and-not$macroProof.exactWholeFile-and-not$macroProof.preBuildFrozen) 'Macro must not masquerade as a production source range'
  Need ($macroProof.sourceProof.startByte-eq$workerMap.shutdownProof.startByte-and$macroProof.sourceProof.endByteExclusive-eq$workerMap.shutdownProof.endByteExclusive-and$macroProof.sourceProof.sha256-ceq$workerMap.shutdownProof.sha256) 'Original owner source proof remains separately bound'
  Need ($macroProof.sourceProof.entrypoint-ceq'fireForShutdown'-and[Text.Encoding]::ASCII.GetString($macroProof.expectedBytes)-ceq('#define FIXTURE_PRE_SHUTDOWN_FIRE_ENTRYPOINT fireForShutdown'+[char]10)) 'Current actual owner routes select the shutdown drain'
 }
 CheckRecipe 'Current worker missing macro cannot inherit whole-source permission' {$next=@(CloneRecipe $workerRecords);$next[0].headers=@($next[0].headers|Where-Object{(Norm $_.resolved)-cne(Norm $macroPath)});RejectRecipe {WorkerRows $next} 'Complete selected generated test dependency set required'}
 CheckRecipe 'Current worker duplicate macro remains rejected' {$next=@(CloneRecipe $workerRecords);$next[0].headers+=@{resolved=$macroPath};RejectRecipe {WorkerRows $next} 'Duplicate selected generated test dependency'}
 CheckRecipe 'Current worker unknown generated dependency remains rejected' {$next=@(CloneRecipe $workerRecords);$next[0].headers+=@{resolved=(Join-Path $workerBase 'fragments/unknown.inc')};RejectRecipe {WorkerRows $next} 'Unknown generated test dependency'}
 CheckRecipe 'Current worker macro cannot masquerade as a frozen input' {Need ($null-ne$macroProof) 'Current macro proof';$next=$pins.Clone();$next[(Norm $macroPath)]=$macroProof.pin;RejectRecipe {WorkerRows $workerRecords $next} 'Generated test output cannot masquerade'}
 CheckRecipe 'Current worker exact derived macro bytes are verified' {Need ($null-ne$macroProof) 'Current macro proof';[void](GeneratedTestActual $macroProof $macroProof.pin $macroProof.expectedBytes)}
 CheckRecipe 'Current worker forged macro pin cannot bless changed bytes' {Need ($null-ne$macroProof) 'Current macro proof';$raw=[byte[]]$macroProof.expectedBytes.Clone();$raw[0]=$raw[0]-bxor1;$pin=CloneRecipe $macroProof.pin;$pin.sha256=GeneratedTestHash $raw;RejectRecipe {GeneratedTestActual $macroProof $pin $raw} 'differs from frozen derived macro bytes'}
 function ShutdownTransform([string]$body){
  $raw=[Text.Encoding]::ASCII.GetBytes($body);$proof=@{source='third_party/sources/miniblink/content/web_impl_win/BlinkPlatformImpl.cpp';output='fragments/platform_shutdown.proof.cpp';startByte=0;endByteExclusive=$raw.Length;bytes=$raw.Length;sha256=(GeneratedTestHash $raw)}
  GeneratedTestWorkerShutdown $raw $proof
 }
 $shutdownRoute='((WebThreadImpl*)currentThread())->fireForShutdown();'+[char]10
 $shutdownCommit='((WebThreadImpl*)m_mainThread)->shutdown();'+[char]10
 $shutdownHead='void BlinkPlatformImpl::shutdown()'+[char]10+'{'+[char]10
 $shutdownTail='}'+[char]10
 CheckRecipe 'Shutdown transform mirrors both permitted writer route spellings' {
  foreach($entrypoint in @('fire','fireForShutdown')){
   $route=$shutdownRoute.Replace('fireForShutdown',$entrypoint);$token=ShutdownTransform ($shutdownHead+$route+$route+$route+$shutdownCommit+$shutdownTail)
   Need ([Text.Encoding]::ASCII.GetString($token.bytes)-ceq('#define FIXTURE_PRE_SHUTDOWN_FIRE_ENTRYPOINT '+$entrypoint+[char]10)) 'Exact writer macro bytes'
  }
 }
 CheckRecipe 'Shutdown transform rejects a route after shutdown commit' {RejectRecipe {ShutdownTransform ($shutdownHead+$shutdownRoute+$shutdownRoute+$shutdownCommit+$shutdownRoute+$shutdownTail)} 'exactly three pre-shutdown fire routes'}
 CheckRecipe 'Shutdown transform rejects a fourth route' {RejectRecipe {ShutdownTransform ($shutdownHead+$shutdownRoute+$shutdownRoute+$shutdownRoute+$shutdownRoute+$shutdownCommit+$shutdownTail)} 'exactly three pre-shutdown fire routes'}
 CheckRecipe 'Shutdown transform rejects disagreeing routes' {RejectRecipe {ShutdownTransform ($shutdownHead+$shutdownRoute+$shutdownRoute+$shutdownRoute.Replace('fireForShutdown','fire')+$shutdownCommit+$shutdownTail)} 'fire routes disagree'}
 CheckRecipe 'Shutdown transform rejects another lifecycle function' {RejectRecipe {ShutdownTransform ($shutdownHead.Replace('::shutdown()','::other()')+$shutdownRoute+$shutdownRoute+$shutdownRoute+$shutdownCommit+$shutdownTail)} 'actual lifecycle function'}
 CheckRecipe 'Shutdown proof cannot bless changed raw owner bytes' {
  $proof=$workerMap.shutdownProof;$source=$physical[(Norm (Join-Path $root $proof.source))];$changed=[byte[]]$source.Clone();$changed[$proof.startByte]=$changed[$proof.startByte]-bxor1
  RejectRecipe {GeneratedTestWorkerShutdown $changed $proof} 'source range differs from map'
 }
 $report=[ordered]@{schema='soui.generated-test-recipe-source-tests/1';sourceOnly=$true;graph='authored dependency/source-object contracts, not captured compilation';passed=($failed-eq0);counts=@{total=$cases.Count;failed=$failed;derived=$derived.Count;selectedObjects=$records.Count;workerDerived=$workerDerived.Count;workerSelectedObjects=$workerRecords.Count};cases=$cases.ToArray();actorsInvoked=0;qualificationPassed=$false;releaseReady=$false}
 $report|ConvertTo-Json -Depth 100
 if($failed){exit 1};return
}
Need ($BaselinePath.Length-gt0-and$FailureDirectory.Length-gt0) 'Historical captured mode requires BaselinePath and FailureDirectory; current SOURCE recipes use RecipeRoot'
foreach($declaration in @(Declarations $BaselinePath @('DependencyPin'))){. ([scriptblock]::Create($declaration))}
# Only AST-extracted functions run. The observer entry point and tools never run.
$cfg=Get-Content -LiteralPath (Join-Path $FailureDirectory 'llvm-observer-inputs.json') -Raw|ConvertFrom-Json
$root=$cfg.root;$build=$cfg.buildDirectory;$configuration=$cfg.configuration
$allowed=@{};foreach($pin in $cfg.dependencyPins){$allowed[(Norm $pin.path)]=$pin};$original=$allowed.Clone();$dependencyPins=@{}
$reader={param($path)return ,([IO.File]::ReadAllBytes($path))}
function MatchPin($pin){Need ($null-ne$pin) 'Known pin';$raw=[IO.File]::ReadAllBytes($pin.path);Need ($raw.Length-eq$pin.bytes-and(GeneratedTestHash $raw)-ceq$pin.sha256) 'Physical pin changed';$pin}
$rawDeps=[IO.File]::ReadAllText((Join-Path $FailureDirectory 'llvm-observer/ninja-dependencies.txt'))
$db=[IO.File]::ReadAllText((Join-Path $build 'compile_commands.json'))|ConvertFrom-Json
$commands=@($db|Where-Object{$_.output.Replace('\','/').Contains('/'+$configuration+'/',[StringComparison]::Ordinal)})
$bound=DependencyBindings $build $commands $rawDeps;$selected=$bound.selected
$missing=@{};foreach($record in $selected){foreach($header in $record.headers){if(-not$original.ContainsKey((Norm $header.resolved))){$missing[(Norm $header.resolved)]=$header.resolved}}}
$cases=[Collections.Generic.List[object]]::new();$failed=0
function Check([string]$name,[scriptblock]$body){try{&$body|Out-Null;$cases.Add(@{name=$name;passed=$true})}catch{$script:failed++;$cases.Add(@{name=$name;passed=$false;error=[string]$_})}}
function Reject([scriptblock]$body,[string]$reason){$rejected=$false;$message=$null;try{&$body|Out-Null}catch{$rejected=$true;$message=[string]$_};Need $rejected 'Expected rejection';Need ($message.Contains($reason,[StringComparison]::Ordinal)) ('Wrong rejection: '+$message)}
function Clone($value){$value|ConvertTo-Json -Depth 100 -Compress|ConvertFrom-Json -AsHashtable}
function Derive($records=$selected,$pins=$original,[scriptblock]$read=$reader){@(GeneratedTestDependencies $root $build $configuration $pins $records $read)}
function Complete($records){$rows=Derive $records;$pins=$original.Clone();foreach($row in $rows){$pins[(Norm $row.pin.path)]=$row.pin};foreach($path in $missing.Values){Need ($pins.ContainsKey((Norm $path))) 'Actual dependency was not frozen before Build';$row=@($rows|Where-Object{(Norm $_.pin.path)-ceq(Norm $path)});Need ($row.Count-eq1) 'Generated membership';$raw=[IO.File]::ReadAllBytes($path);[void](GeneratedTestActual $row[0] @{path=$path;bytes=$raw.Length;sha256=(GeneratedTestHash $raw)} $raw)};$rows}
Check 'Actual failed Release graph has 128 records and 26 unfrozen paths' {Need ($selected.Count-eq128-and$missing.Count-eq26-and$cfg.dependencyPins.Count-eq5845-and$null-eq$cfg.PSObject.Properties['withVendorTests']) 'Original facts'}
Check 'Old original DependencyPin RED for all 26 real paths' {foreach($path in $missing.Values){Reject {DependencyPin $path} 'Actual dependency was not frozen before Build'}}
$derived=Derive
Check 'New GREEN matches all 26 actual byte arrays' {$script:derived=Complete $selected;Need ($derived.Count-eq26-and@($derived|Where-Object{$_.writerFamily-ceq'endpoint'}).Count-eq1-and@($derived|Where-Object{$_.writerFamily-ceq'worker'}).Count-eq12-and@($derived|Where-Object{$_.writerFamily-ceq'request-close'}).Count-eq13) 'Three writer coverage'}
Check 'Derived is never pre-Build frozen' {foreach($row in $derived){Need (-not$row.preBuildFrozen-and-not$original.ContainsKey((Norm $row.pin.path))-and$row.writerPins.Count-gt0) 'Derived boundary'}}
Check 'Original DependencyPin GREEN only after exact admission' {$script:allowed=$original.Clone();foreach($row in $derived){$allowed[(Norm $row.pin.path)]=$row.pin};foreach($path in $missing.Values){[void](DependencyPin $path)};$script:allowed=$original.Clone()}
Check 'Unselected graph derives nothing' {Need ((Derive @()).Count-eq0) 'Selected only'}
foreach($family in @('endpoint','worker','request-close')){
 $proof=$derived|Where-Object{$_.writerFamily-ceq$family}|Select-Object -First 1;$object=$proof.selectedObject
 Check ($family+': unknown generated path') {$records=Clone $selected;$record=$records|Where-Object{(Norm $_.object)-ceq(Norm $object)};$base=Join-Path $build $(switch($family){endpoint{'tests/native/vendor_lifetime/generated'}worker{'tests/native/vendor_lifetime/worker_lifetime/generated'}default{'tests/native/request_close/generated'}});$record.headers+=@{resolved=(Join-Path $base 'unknown.inc')};Reject {Derive $records} 'Unknown generated test dependency'}
 Check ($family+': missing generated path') {$records=Clone $selected;$record=$records|Where-Object{(Norm $_.object)-ceq(Norm $object)};$record.headers=@($record.headers|Where-Object{(Norm $_.resolved)-cne(Norm $proof.pin.path)});Reject {Derive $records} 'Complete selected generated test dependency set required'}
 Check ($family+': duplicate generated path') {$records=Clone $selected;$record=$records|Where-Object{(Norm $_.object)-ceq(Norm $object)};$record.headers+=@(($record.headers|Where-Object{(Norm $_.resolved)-ceq(Norm $proof.pin.path)}));Reject {Derive $records} 'Duplicate selected generated test dependency'}
 Check ($family+': wrong object does not grant admission') {$records=Clone $selected;($records|Where-Object{(Norm $_.object)-ceq(Norm $object)}).object+='-wrong';Reject {Complete $records} 'Actual dependency was not frozen before Build'}
 Check ($family+': STALE rejected') {$records=Clone $selected;($records|Where-Object{(Norm $_.object)-ceq(Norm $object)}).status='STALE';Reject {Derive $records} 'Unique selected generated test object binding'}
 Check ($family+': duplicate object rejected') {$records=Clone $selected;$records+=@(($records|Where-Object{(Norm $_.object)-ceq(Norm $object)}));Reject {Derive $records} 'Unique selected generated test object binding'}
 Check ($family+': wrong generated directory rejected') {$records=Clone $selected;$record=$records|Where-Object{(Norm $_.object)-ceq(Norm $object)};foreach($header in $record.headers){if($missing.ContainsKey((Norm $header.resolved))){$header.resolved=$header.resolved.Replace('/generated/','/wrong/').Replace('\generated\','\wrong\')}};Reject {Derive $records} 'Complete selected generated test dependency set required'}
 Check ($family+': no forged frozen generated pin') {$pins=$original.Clone();$pins[(Norm $proof.pin.path)]=$proof.pin;Reject {Derive $selected $pins} 'Generated test output cannot masquerade'}
}
$writers=@('tests/native/vendor_lifetime/CMakeLists.txt','tests/native/vendor_lifetime/worker_lifetime/CMakeLists.txt','tests/native/vendor_lifetime/worker_lifetime/source-bytes.cmake','tests/native/vendor_lifetime/worker_lifetime/source-map.json','tests/native/request_close/CMakeLists.txt')
foreach($relative in $writers){$path=Join-Path $root $relative
 Check ($relative+': absent frozen writer') {$pins=$original.Clone();$pins.Remove((Norm $path));Reject {Derive $selected $pins} 'Generated test source was not frozen'}
 Check ($relative+': physical writer tamper') {$changed=[IO.File]::ReadAllBytes($path)+[byte]10;$read={param($value)if((Norm $value)-ceq(Norm $path)){return ,$changed};return ,([IO.File]::ReadAllBytes($value))};Reject {Derive $selected $original $read} 'Frozen generated test input changed'}
 Check ($relative+': changed frozen SourceSet cannot bless changed writer') {$changed=[IO.File]::ReadAllBytes($path)+[byte]10;$pins=$original.Clone();$pins[(Norm $path)]=@{path=$path;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)};$read={param($value)if((Norm $value)-ceq(Norm $path)){return ,$changed};return ,([IO.File]::ReadAllBytes($value))};Reject {Derive $selected $pins $read} 'Unsupported frozen generated test writer'}
}
foreach($path in @($derived.source.path|Sort-Object -Unique)){Check ('Absent exact source: '+[IO.Path]::GetFileName($path)) {$pins=$original.Clone();$pins.Remove((Norm $path));Reject {Derive $selected $pins} 'Generated test source was not frozen'}}
$path=Join-Path $root 'third_party/sources/miniblink/wke/wke.cpp'
foreach($field in @('bytes','sha256')){Check ('Source pin '+$field+' tamper') {$pins=$original.Clone();$pins[(Norm $path)]=Clone $pins[(Norm $path)];if($field-ceq'bytes'){$pins[(Norm $path)].bytes++}else{$pins[(Norm $path)].sha256='0'*64};Reject {Derive $selected $pins} 'Frozen generated test input changed'}}
Check 'Duplicate endpoint marker cannot be blessed by changed SourceSet' {$changed=[IO.File]::ReadAllBytes($path)+[Text.Encoding]::UTF8.GetBytes('struct InteractionEndpointImpl {');$pins=$original.Clone();$pins[(Norm $path)]=@{path=$path;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)};$read={param($value)if((Norm $value)-ceq(Norm $path)){return ,$changed};return ,([IO.File]::ReadAllBytes($value))};Reject {Derive $selected $pins $read} 'two unique source markers'}
Check 'Worker whole-source map pin survives no source rewrite' {$path=Join-Path $root 'third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.h';$changed=[IO.File]::ReadAllBytes($path)+[byte]10;$pins=$original.Clone();$pins[(Norm $path)]=@{path=$path;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)};$read={param($value)if((Norm $value)-ceq(Norm $path)){return ,$changed};return ,([IO.File]::ReadAllBytes($value))};Reject {Derive $selected $pins $read} 'Worker map whole-source pin differs'}
Check 'Unknown configuration rejected' {Reject {GeneratedTestDependencies $root $build 'Unknown' $original $selected $reader} 'Selected generated test configuration'}
Check 'Whole-file copy preserves CRLF and dollar' {$raw=[byte[]](65,13,10,36);$copy=GeneratedTestInterval $raw 0 $raw.Length $true;Need ([BitConverter]::ToString($raw)-ceq[BitConverter]::ToString($copy)) 'Raw copy'}
foreach($bad in @([byte[]](65,0,10),[byte[]](65,13,10),[byte[]](65),[byte[]](65,10,66))){Check ('Invalid fragment '+[BitConverter]::ToString($bad)) {Reject {GeneratedTestInterval $bad 0 $bad.Length $false} 'terminal LF and no CR/NUL'}}
Check 'Partial whole-file interval rejected' {Reject {GeneratedTestInterval ([byte[]](65,10)) 1 2 $true} 'whole-file copy interval'}
foreach($bad in @('../x','/x','a\b','C:x','a:b','a;z','a//b','a/./b','a/../b','a.','a ',('a'+[char]0))){Check ('Invalid portable path '+$bad.Replace([string][char]0,'<NUL>')) {Reject {GeneratedTestRelative $bad} 'Exact portable generated test path'}}
Check 'Malformed original dependency count remains rejected' {Reject {NinjaDependencyBlocks "a.obj: #deps 2, deps mtime 1 (VALID)`n    a.h`n"} 'Ninja header count differs'}
Check 'Original STALE dependency still rejected' {Reject {DependencyBindings $build @(@{output='a.obj'}) "a.obj: #deps 1, deps mtime 1 (STALE)`n    a.h`n"} 'Current required Ninja dependencies'}
foreach($family in @('endpoint','worker','request-close')){
 $proof=$derived|Where-Object{$_.writerFamily-ceq$family}|Select-Object -First 1;$raw=[IO.File]::ReadAllBytes($proof.pin.path)
 Check ($family+': actual file body tamper rejected') {$changed=[byte[]]$raw.Clone();$changed[0]=$changed[0]-bxor1;Reject {GeneratedTestActual $proof @{path=$proof.pin.path;bytes=$changed.Length;sha256=(GeneratedTestHash $changed)} $changed} 'differs from frozen exact source bytes'}
 Check ($family+': forged pin cannot hide actual raw bytes') {$changed=[byte[]]$raw.Clone();$changed[0]=$changed[0]-bxor1;Reject {GeneratedTestActual $proof $proof.pin $changed} 'differs from frozen exact source bytes'}
 Check ($family+': wrong actual path rejected') {$pin=Clone $proof.pin;$pin.path+='-wrong';Reject {GeneratedTestActual $proof $pin $raw} 'differs from frozen exact source bytes'}
 Check ($family+': wrong actual size rejected') {$pin=Clone $proof.pin;$pin.bytes++;Reject {GeneratedTestActual $proof $pin $raw} 'differs from frozen exact source bytes'}
 Check ($family+': wrong actual SHA rejected') {$pin=Clone $proof.pin;$pin.sha256='0'*64;Reject {GeneratedTestActual $proof $pin $raw} 'differs from frozen exact source bytes'}
}
Check 'Derived root and build relocate from relative roles without guessed absolute paths' {
 $newRoot='E:/public/relocated-sdk';$newBuild='E:/public/relocated-sdk/out/isolated';$pins=@{};$physical=@{}
 foreach($pin in $original.Values){$key=Norm $pin.path;if($key.StartsWith((Norm $root).TrimEnd('/')+'/')){$relative=$key.Substring((Norm $root).Length+1);$newPath=Join-Path $newRoot $relative;$next=Clone $pin;$next.path=$newPath;$pins[(Norm $newPath)]=$next;$physical[(Norm $newPath)]=$pin.path}}
 $records=Clone $selected;foreach($record in $records){$record.object=Join-Path $newBuild ((Norm $record.object).Substring((Norm $build).Length+1));foreach($header in $record.headers){$key=Norm $header.resolved;if($key.StartsWith((Norm $build).TrimEnd('/')+'/')){$header.resolved=Join-Path $newBuild $key.Substring((Norm $build).Length+1)}}}
 $relocatedRead={param($value)Need ($physical.ContainsKey((Norm $value))) 'Known relocated frozen pin';return ,([IO.File]::ReadAllBytes($physical[(Norm $value)]))}
 $relocated=@(GeneratedTestDependencies $newRoot $newBuild $configuration $pins $records $relocatedRead);Need ($relocated.Count-eq26) 'Relocated full graph'
 foreach($row in $relocated){$relative=(Norm $row.pin.path).Substring((Norm $newBuild).Length+1);$old=@($derived|Where-Object{(Norm $_.pin.path)-ceq(Norm (Join-Path $build $relative))});Need ($old.Count-eq1-and$row.pin.bytes-eq$old[0].pin.bytes-and$row.pin.sha256-ceq$old[0].pin.sha256-and[BitConverter]::ToString($row.expectedBytes)-ceq[BitConverter]::ToString($old[0].expectedBytes)) 'Relocated exact bytes'}
}
Check 'Writer text CRLF normalization preserves raw outputs where no payload raw pin conflicts' {
 $pins=$original.Clone();$rewritten=@{}
 foreach($relative in @($writers[0],$writers[3],$writers[4])){$path=Join-Path $root $relative;$text=[IO.File]::ReadAllText($path).Replace("`r`n","`n").Replace("`n","`r`n");$bytes=[Text.Encoding]::UTF8.GetBytes($text);$rewritten[(Norm $path)]=$bytes;$pins[(Norm $path)]=@{path=$path;bytes=$bytes.Length;sha256=(GeneratedTestHash $bytes)}}
 $read={param($value)if($rewritten.ContainsKey((Norm $value))){return ,$rewritten[(Norm $value)]};return ,([IO.File]::ReadAllBytes($value))}
 $rows=Derive $selected $pins $read;Need ($rows.Count-eq26) 'Checkout normalized writer set';foreach($row in $rows){$old=@($derived|Where-Object{(Norm $_.pin.path)-ceq(Norm $row.pin.path)});Need ($old.Count-eq1-and[BitConverter]::ToString($row.expectedBytes)-ceq[BitConverter]::ToString($old[0].expectedBytes)) 'Never normalize generated raw bytes'}
}
foreach($relative in @($writers[1],$writers[2])){Check ($relative+': normalized writer cannot bypass payload raw-byte pin') {$path=Join-Path $root $relative;$changedWriterBytes=[Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($path).Replace("`r`n","`n").Replace("`n","`r`n"));$pins=$original.Clone();$pins[(Norm $path)]=@{path=$path;bytes=$changedWriterBytes.Length;sha256=(GeneratedTestHash $changedWriterBytes)};$read={param($value)if((Norm $value)-ceq(Norm $path)){return ,$changedWriterBytes};return ,([IO.File]::ReadAllBytes($value))};Reject {Derive $selected $pins $read} 'Worker map payload pin differs'}}
$report=[ordered]@{schema='soui.generated-test-dependency-pure-tests/1';sourceOnly=$true;passed=($failed-eq0);counts=@{total=$cases.Count;failed=$failed;selected=$selected.Count;originalMissing=$missing.Count;derived=$derived.Count};cases=$cases.ToArray();generated=@($derived|ForEach-Object{[ordered]@{pin=$_.pin;writerFamily=$_.writerFamily;selectedObject=$_.selectedObject;source=$_.source;writerPins=$_.writerPins;startByte=$_.startByte;endByteExclusive=$_.endByteExclusive;exactWholeFile=$_.exactWholeFile;preBuildFrozen=$_.preBuildFrozen}});actorsInvoked=0;actualSdkAccepted=$false;qualificationPassed=$false;releaseReady=$false}
$report|ConvertTo-Json -Depth 100
if($failed-ne0){exit 1}
