[CmdletBinding()]
param([Parameter(Mandatory)][string]$CandidatePath,[Parameter(Mandatory)][string]$BaselinePath,[Parameter(Mandatory)][string]$FailureDirectory)
$ErrorActionPreference='Stop'
function Need([bool]$value,[string]$message){if(-not$value){throw $message}}
function Norm([string]$value){$value.Replace('\','/').ToLowerInvariant()}
function Full([string]$value,[string]$base){if([IO.Path]::IsPathRooted($value)){return [IO.Path]::GetFullPath($value)};[IO.Path]::GetFullPath((Join-Path $base $value))}
function Declarations([string]$path,[string[]]$names){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors);Need ($errors.Count-eq0) 'PowerShell syntax'
 foreach($name in $names){$nodes=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true));Need ($nodes.Count-eq1) ('Unique pure declaration: '+$name);$nodes[0].Extent.Text}
}
$names=@('GeneratedTestHash','GeneratedTestRelative','GeneratedTestFrozen','GeneratedTestWriter','GeneratedTestInterval','GeneratedTestMarker','GeneratedTestActual','GeneratedTestDependencies','NinjaDependencyBlocks','DependencyBindings')
foreach($declaration in @(Declarations $CandidatePath $names)){. ([scriptblock]::Create($declaration))}
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
