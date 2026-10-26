[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$InputFile,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$InputSha,
 [Parameter(Mandatory)][string]$ObservationPath
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
function Need([bool]$value,[string]$message){if(-not$value){throw $message}}
function Norm([string]$value){$value.Replace('\','/').ToLowerInvariant()}
function Full([string]$value,[string]$base){if([IO.Path]::IsPathRooted($value)){return [IO.Path]::GetFullPath($value)};[IO.Path]::GetFullPath((Join-Path $base $value))}
function Plain([string]$value){
 Need ($value-match'^[a-zA-Z]:[\\/]'-and$value-notmatch'[\x00-\x1f]') 'An absolute local path is required'
 $full=[IO.Path]::GetFullPath($value);$item=Get-Item -LiteralPath $full -Force
 Need (-not($item.Attributes-band[IO.FileAttributes]::ReparsePoint)) ('Linked observation path: '+$full)
 if($item.PSIsContainer){$parent=$item.Parent}else{$parent=$item.Directory}
 while($null-ne$parent){Need (-not($parent.Attributes-band[IO.FileAttributes]::ReparsePoint)) ('Linked observation ancestor: '+$parent.FullName);$parent=$parent.Parent}
 return $item
}
function ObsPin([string]$value){$item=Plain $value;Need (-not$item.PSIsContainer) 'A plain observation file is required';[ordered]@{path=$value;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $value -Algorithm SHA256).Hash.ToLowerInvariant()}}
function MatchPin($pin){$actual=ObsPin $pin.path;Need ($actual.bytes-eq$pin.bytes-and$actual.sha256-ceq$pin.sha256) ('Pinned input changed: '+$pin.path);return $actual}
function RawText([string]$value){
 $pin=ObsPin $value;Need ($pin.bytes-gt0-and$pin.bytes-le67108864) 'Bounded nonempty text observation required'
 $raw=[IO.File]::ReadAllBytes($value)
 if($raw.Length-ge2-and$raw[0]-eq255-and$raw[1]-eq254){return [Text.UnicodeEncoding]::new($false,$true,$true).GetString($raw,2,$raw.Length-2)}
 $offset=0;if($raw.Length-ge3-and$raw[0]-eq239-and$raw[1]-eq187-and$raw[2]-eq191){$offset=3}
 return [Text.UTF8Encoding]::new($false,$true).GetString($raw,$offset,$raw.Length-$offset)
}
function SameSet([string[]]$left,[string[]]$right){$a=[string[]]@($left);$b=[string[]]@($right);[Array]::Sort($a,[StringComparer]::Ordinal);[Array]::Sort($b,[StringComparer]::Ordinal);return (($a-join"`n")-ceq($b-join"`n"))}
function NormalSdkSourceCommands([string]$sourceRoot,[string]$buildRoot,[string]$configuration,[bool]$sdkOnly,$frozenPins,$graphCommands){
 $sourcePrefix=(Norm (Full 'sdk/src' $sourceRoot)).TrimEnd('/')+'/'
 $expected=[string[]]@($frozenPins|Where-Object{(Norm $_.path).StartsWith($sourcePrefix,[StringComparison]::Ordinal)-and$_.path.EndsWith('.cpp',[StringComparison]::Ordinal)}|ForEach-Object{Norm $_.path})
 $sdkCommands=@($graphCommands|Where-Object{(Norm $_.file).StartsWith($sourcePrefix,[StringComparison]::Ordinal)})
 $allSources=[string[]]@($sdkCommands|ForEach-Object{Norm $_.file}|Sort-Object -Unique)
 Need ($expected.Count-gt0-and(SameSet $expected $allSources)) 'All frozen normal SDK C++ sources must have selected default graph coverage'
 $objectRelative=$(if($sdkOnly){''}else{'sdk/'})+'CMakeFiles/soui_sdk.dir/'+$configuration+'/'
 $objectPrefix=(Norm (Full $objectRelative $buildRoot)).TrimEnd('/')+'/'
 $production=@($sdkCommands|Where-Object{(Norm (Full $_.output $buildRoot)).StartsWith($objectPrefix,[StringComparison]::Ordinal)})
 Need (SameSet $expected ([string[]]@($production|ForEach-Object{Norm $_.file}))) 'Exactly one production SDK object binding per frozen C++ source is required'
 return $production
}

function CoffMachine([byte[]]$raw,[int]$offset,[int]$length){
 Need ($offset-ge0-and$length-ge20-and$offset-le$raw.Length-$length) 'Truncated bounded COFF object'
 if([BitConverter]::ToUInt16($raw,$offset)-eq0-and[BitConverter]::ToUInt16($raw,$offset+2)-eq65535){
  Need ($length-ge56-and[BitConverter]::ToUInt16($raw,$offset+4)-ge2) 'Import/unknown object cannot replace a compiled COFF object'
  return [int][BitConverter]::ToUInt16($raw,$offset+6)
 }
 return [int][BitConverter]::ToUInt16($raw,$offset)
}
function PeMachine([string]$value){
 $pin=ObsPin $value;Need ($pin.bytes-ge88-and$pin.bytes-le268435456) 'Bounded PE extent'
 $raw=[IO.File]::ReadAllBytes($value);Need ([BitConverter]::ToUInt16($raw,0)-eq23117) 'Actual output DOS signature'
 $offset=[long][BitConverter]::ToUInt32($raw,60);Need ($offset-ge64-and$offset-le$raw.Length-26-and[BitConverter]::ToUInt32($raw,[int]$offset)-eq17744) 'Actual output PE extent/signature'
 [ordered]@{pin=$pin;machine=[int][BitConverter]::ToUInt16($raw,[int]$offset+4)}
}
function ArchiveMembers([string]$value){
 $pin=ObsPin $value;Need ($pin.bytes-ge8-and$pin.bytes-le536870912) 'Bounded actual archive required'
 $raw=[IO.File]::ReadAllBytes($value);Need ([Text.Encoding]::ASCII.GetString($raw,0,8)-ceq"!<arch>`n") 'Actual COFF archive magic'
 $offset=8;$longNames=$null;$members=[Collections.Generic.List[object]]::new();$hash=[Security.Cryptography.SHA256]::Create()
 try{while($offset-lt$raw.Length){
  Need ($offset-le$raw.Length-60-and$raw[$offset+58]-eq96-and$raw[$offset+59]-eq10) 'Archive member header extent/signature'
  $name=[Text.Encoding]::ASCII.GetString($raw,$offset,16).Trim();$sizeText=[Text.Encoding]::ASCII.GetString($raw,$offset+48,10).Trim()
  Need ($sizeText-cmatch'^(0|[1-9][0-9]*)$') 'Archive member decimal length';$size=[long]::Parse($sizeText);$body=$offset+60
  Need ($size-le[long]($raw.Length-$body)) 'Archive member body extent'
  if($name-ceq'//'){$longNames=[byte[]]::new([int]$size);[Array]::Copy($raw,$body,$longNames,0,[int]$size)}
  elseif($name-cne'/'){
   if($name-cmatch'^/([0-9]+)$'){
    Need ($null-ne$longNames) 'Archive long-name table missing';$at=[int]::Parse($Matches[1]);Need ($at-lt$longNames.Length) 'Archive long-name offset'
    $end=$at;while($end-lt$longNames.Length-and$longNames[$end]-ne0-and-not($longNames[$end]-eq47-and$end+1-lt$longNames.Length-and$longNames[$end+1]-eq10)){$end++}
    Need ($end-gt$at-and$end-lt$longNames.Length) 'Archive long-name terminator';$name=[Text.UTF8Encoding]::new($false,$true).GetString($longNames,$at,$end-$at)
   }else{Need ($name-notmatch'^#1/'-and$name.EndsWith('/')) 'Unsupported archive member name';$name=$name.Substring(0,$name.Length-1)}
   $machine=CoffMachine $raw $body ([int]$size);$digest=[BitConverter]::ToString($hash.ComputeHash($raw,$body,[int]$size)).Replace('-','').ToLowerInvariant()
   $members.Add([ordered]@{name=$name;bytes=$size;sha256=$digest;machine=$machine})
  }
  $offset=$body+[int]$size;if(($size-band1)-eq1){Need ($offset-lt$raw.Length-and$raw[$offset]-eq10) 'Archive padding extent';$offset++}
 };Need ($offset-eq$raw.Length-and$members.Count-gt0) 'Exact archive end and object members'}finally{$hash.Dispose()}
 [ordered]@{pin=$pin;members=$members.ToArray()}
}

function NinjaLogicalLines([string]$text){
 $physical=$text -split '\r?\n';$result=[Collections.Generic.List[object]]::new()
 $pending='';$first=0;$continued=$false
 for($i=0;$i -lt $physical.Count;$i++){
  $line=$physical[$i]
  Need (-not ($continued -and $i -eq $physical.Count-1 -and $line -ceq '')) 'Unterminated Ninja continuation'
  if(-not $continued){$first=$i+1;$pending=$line}else{$pending+=$line.TrimStart([char[]]@(' ',[char]9))}
  $dollars=0
  for($j=$line.Length-1;$j -ge 0 -and $line[$j] -ceq '$';$j--){$dollars++}
  if(($dollars%2) -eq 1){$pending=$pending.Substring(0,$pending.Length-1);$continued=$true;continue}
  $result.Add([ordered]@{line=$first;text=$pending});$continued=$false;$pending=''
 }
 Need (-not $continued) 'Unterminated Ninja continuation'
 return $result.ToArray()
}

function NinjaLiteralValue([string]$text){
 $value=[Text.StringBuilder]::new()
 for($i=0;$i -lt $text.Length;$i++){
  if($text[$i] -cne '$'){[void]$value.Append($text[$i]);continue}
  $i++;Need ($i -lt $text.Length -and ($text[$i] -ceq '$' -or $text[$i] -ceq ':' -or $text[$i] -ceq ' ')) 'Unsupported Ninja variable/escape in workdir definition'
  [void]$value.Append($text[$i])
 }
 return $value.ToString()
}
function BoundNinjaWorkdir([string]$build,[string]$value){
 Need ($build -cmatch '^[A-Za-z]:[\\/]' -and $value -cmatch '^[A-Za-z]:[\\/]' -and $value -cmatch '[\\/]$' -and $value -notmatch '[\x00-\x1f]' -and (Norm (Full $value $build)).TrimEnd('/') -ceq (Norm (Full $build $build)).TrimEnd('/')) 'Ninja workdir differs from bound build root'
 return $value
}
function NinjaWorkdirScope([string]$impl,[string]$common,[string]$rules,[string]$build){
 $texts=[ordered]@{impl=@(NinjaLogicalLines $impl);common=@(NinjaLogicalLines $common);rules=@(NinjaLogicalLines $rules)}
 $definitions=@();$routes=@{};$uses=@{}
 foreach($scope in @('impl','common','rules')){
  $routes[$scope]=@();$uses[$scope]=@()
  for($i=0;$i -lt $texts[$scope].Count;$i++){
   $raw=$texts[$scope][$i].text
   if($raw -cmatch '^[ \t]*cmake_ninja_workdir[ \t]*='){$definitions+=,[ordered]@{scope=$scope;index=$i;raw=$raw}}
   if($raw -cmatch '^[ \t]*(include|subninja)[ \t]+(.+)$'){$routes[$scope]+=,[ordered]@{kind=$Matches[1];value=$Matches[2].Replace('\','/');index=$i;raw=$raw}}
   if($raw -cmatch '\$(?:\{cmake_ninja_workdir\}|cmake_ninja_workdir\b)'){$uses[$scope]+=$i}
  }
 }
 Need ($definitions.Count -eq 1 -and $definitions[0].scope -ceq 'common' -and $definitions[0].raw.StartsWith('cmake_ninja_workdir',[StringComparison]::Ordinal)) 'Unique top-level common Ninja workdir definition required; shadow is forbidden'
 Need ($routes.impl.Count -eq 1 -and $routes.impl[0].raw.StartsWith('include ',[StringComparison]::Ordinal) -and $routes.impl[0].value -ceq 'CMakeFiles/common.ninja' -and $routes.common.Count -eq 1 -and $routes.common[0].raw.StartsWith('include ',[StringComparison]::Ordinal) -and $routes.common[0].value -ceq 'CMakeFiles/rules.ninja' -and $routes.rules.Count -eq 0) 'Foreign Ninja include/subninja scope'
 Need ($uses.rules.Count -eq 0 -and @($uses.impl|Where-Object{$_ -le $routes.impl[0].index}).Count -eq 0 -and @($uses.common|Where-Object{$_ -le $definitions[0].index}).Count -eq 0 -and $definitions[0].index -gt $routes.common[0].index) 'Ninja workdir used before its declared include scope'
 Need ($definitions[0].raw -cmatch '^cmake_ninja_workdir[ \t]*=[ \t]*(.*)$') 'Ninja workdir declaration syntax'
 $value=BoundNinjaWorkdir $build (NinjaLiteralValue $Matches[1])
 return [ordered]@{buildRoot=$build;value=$value;definition=$definitions[0].raw;definitionLine=$definitions[0].index+1}
}

function NinjaWords([string]$text,[string]$build,[string]$workdir){
 if($PSBoundParameters.ContainsKey('workdir')){[void](BoundNinjaWorkdir $build $workdir)}
 $tokens=[Collections.Generic.List[object]]::new();$word=[Text.StringBuilder]::new()
 for($i=0;$i -lt $text.Length;$i++){
  $c=$text[$i]
  if($c -ceq '$'){
   Need ($i+1 -lt $text.Length) 'Unterminated Ninja escape'
   $i++;$next=$text[$i]
   if($next -ceq '$' -or $next -ceq ':' -or $next -ceq ' '){[void]$word.Append($next);continue}
   if($next -ceq '{'){
    $end=$text.IndexOf('}',$i+1);Need ($end -ge 0) 'Unterminated Ninja variable';$name=$text.Substring($i+1,$end-$i-1);$i=$end
   }else{
    Need ($text.Substring($i) -cmatch '^[A-Za-z0-9_.-]+') 'Unsupported Ninja variable/escape';$name=$Matches[0];$i+=$name.Length-1
   }
   Need ($name -ceq 'cmake_ninja_workdir' -and $PSBoundParameters.ContainsKey('workdir')) 'Unsupported Ninja variable/escape'
   # Expanded paths are literal content, never a second token stream.
   [void]$word.Append($workdir);continue
  }
  if($c -ceq ' ' -or $c -eq [char]9 -or $c -ceq ':' -or $c -ceq '|'){
   if($word.Length){$tokens.Add([ordered]@{kind='path';value=$word.ToString()});[void]$word.Clear()}
   if($c -ceq ':'){$tokens.Add([ordered]@{kind='colon';value=':'})}
   elseif($c -ceq '|'){
    $separator='|'
    if($i+1 -lt $text.Length -and ($text[$i+1] -ceq '|' -or $text[$i+1] -ceq '@')){$i++;$separator+=$text[$i]}
    $tokens.Add([ordered]@{kind='separator';value=$separator})
   }
   continue
  }
  [void]$word.Append($c)
 }
 if($word.Length){$tokens.Add([ordered]@{kind='path';value=$word.ToString()})}
 return $tokens.ToArray()
}

function NinjaBuildEdges([string]$text,[string]$build,[string]$workdir){
 $edges=[Collections.Generic.List[object]]::new()
 foreach($line in @(NinjaLogicalLines $text)){
  if(-not $line.text.StartsWith('build ',[StringComparison]::Ordinal)){continue}
  if($PSBoundParameters.ContainsKey('workdir')){$tokens=@(NinjaWords $line.text.Substring(6) $build $workdir)}else{$tokens=@(NinjaWords $line.text.Substring(6))};$colon=@()
  for($i=0;$i -lt $tokens.Count;$i++){if($tokens[$i].kind -ceq 'colon'){$colon+=$i}}
  Need ($colon.Count -eq 1 -and $colon[0] -gt 0 -and $colon[0]+1 -lt $tokens.Count) 'Malformed Ninja build edge'
  $explicitOutputs=@();$implicitOutputs=@();$implicit=$false
  for($i=0;$i -lt $colon[0];$i++){
   $token=$tokens[$i]
   if($token.kind -ceq 'separator'){
    Need ($token.value -ceq '|' -and -not $implicit -and $explicitOutputs.Count -gt 0 -and $i+1 -lt $colon[0]) 'Malformed Ninja output separator'
    $implicit=$true;continue
   }
   Need ($token.kind -ceq 'path') 'Unsupported Ninja output token'
   if($implicit){$implicitOutputs+=$token.value}else{$explicitOutputs+=$token.value}
  }
  $rule=$tokens[$colon[0]+1]
  Need ($rule.kind -ceq 'path' -and $rule.value -cmatch '^[A-Za-z0-9_.-]+$') 'Unsupported Ninja rule'
  $inputs=@(@(),@(),@(),@());$state=0;$lastSeparator=$false
  for($i=$colon[0]+2;$i -lt $tokens.Count;$i++){
   $token=$tokens[$i]
   if($token.kind -ceq 'separator'){
    Need (-not $lastSeparator) 'Empty Ninja input separator section'
    $next= switch -CaseSensitive ($token.value){'|'{1};'||'{2};'|@'{3};default{throw 'Unsupported Ninja input separator'}}
    Need ($next -gt $state) 'Duplicate/out-of-order Ninja input separator'
    $state=$next;$lastSeparator=$true;continue
   }
   Need ($token.kind -ceq 'path') 'Unsupported Ninja input token'
   $inputs[$state]+=$token.value;$lastSeparator=$false
  }
  Need (-not $lastSeparator) 'Empty final Ninja input section'
  $edges.Add([ordered]@{line=$line.line;rawLogical=$line.text;outputs=$explicitOutputs;implicitOutputs=$implicitOutputs;rule=$rule.value;inputs=$inputs[0];implicitInputs=$inputs[1];orderOnlyInputs=$inputs[2];validations=$inputs[3]})
 }
 return $edges.ToArray()
}

function NinjaDependencyBlocks([string]$text){
 $blocks=[Collections.Generic.List[object]]::new();$current=$null;$lines=$text -split '\r?\n'
 for($i=0;$i -lt $lines.Count;$i++){
  $line=$lines[$i]
  if($line -ceq ''){continue}
  if($line -cmatch '^(?<object>.+): #deps (?<count>0|[1-9][0-9]*), deps mtime (?<mtime>[0-9]+) \((?<status>VALID|STALE)\)$'){
   if($null -ne $current){Need ($current.headers.Count -eq $current.count) 'Ninja header count differs';$blocks.Add($current)}
   $count=[uint64]::Parse($Matches['count']);Need ($count -le $lines.Count) 'Ninja header count extent'
   $current=[ordered]@{line=$i+1;rawHeader=$line;object=$Matches['object'];count=$count;mtime=$Matches['mtime'];status=$Matches['status'];headers=@()};continue
  }
  Need ($null -ne $current -and $line.StartsWith('    ',[StringComparison]::Ordinal) -and $line.Length -gt 4 -and -not [char]::IsWhiteSpace($line[4])) 'Unsupported Ninja dependency line'
  $current.headers+=$line.Substring(4)
  Need ($current.headers.Count -le $current.count) 'Excess Ninja dependency header'
 }
 if($null -ne $current){Need ($current.headers.Count -eq $current.count) 'Ninja header count differs';$blocks.Add($current)}
 return $blocks.ToArray()
}

function CompileBindings([string]$build,$commands,$edges){
 $wanted=@{};foreach($command in $commands){
  Need ((Norm (Full $command.directory $build))-ceq(Norm $build)-and[IO.Path]::IsPathRooted($command.file)) 'Actual compile database cwd/source'
  $object=Full $command.output $build;$key=Norm $object
  Need ($key.StartsWith(((Norm $build).TrimEnd('/')+'/'),[StringComparison]::Ordinal)-and-not$wanted.ContainsKey($key)) 'Unique current build object required';$wanted[$key]=$command
 }
 $seen=@{};$records=[Collections.Generic.List[object]]::new()
 foreach($edge in $edges){if($edge.rule-cnotmatch'^(C|CXX)_COMPILER(?:_|$)'){continue}
  Need ($edge.outputs.Count-eq1-and$edge.implicitOutputs.Count-eq0-and$edge.inputs.Count-eq1-and$edge.validations.Count-eq0) 'Compiler edge shape'
  $object=Full $edge.outputs[0] $build;$key=Norm $object
  Need ($wanted.ContainsKey($key)-and-not$seen.ContainsKey($key)) 'Selected Ninja compiler object missing/duplicate in database'
  $command=$wanted[$key];$source=Full $edge.inputs[0] $build;Need ((Norm $source)-ceq(Norm (Full $command.file $build))) 'Actual edge source/object pair'
  $cxx=[IO.Path]::GetExtension($source)-cmatch'^\.(cpp|cxx|cc|C)$'
  Need (($cxx-and$edge.rule.StartsWith('CXX_COMPILER',[StringComparison]::Ordinal))-or(-not$cxx-and$edge.rule.StartsWith('C_COMPILER',[StringComparison]::Ordinal))) 'Actual compiler language'
  $seen[$key]=$true;$records.Add([ordered]@{line=$edge.line;rawLogical=$edge.rawLogical;source=$source;object=$object;rule=$edge.rule})
 };Need ($seen.Count-eq$wanted.Count) 'Exact compiler edges/database membership';$records.ToArray()
}
function DependencyBindings([string]$build,$commands,[string]$text){
 $wanted=@{};foreach($command in $commands){$key=Norm (Full $command.output $build);Need (-not$wanted.ContainsKey($key)) 'Unique dependency object';$wanted[$key]=$true}
 $seen=@{};$records=[Collections.Generic.List[object]]::new();$other=[Collections.Generic.List[object]]::new()
 foreach($block in @(NinjaDependencyBlocks $text)){
  $object=Full $block.object $build;$key=Norm $object;Need (-not$seen.ContainsKey($key)) 'Duplicate actual Ninja dependency block';$seen[$key]=$true
  if(-not$wanted.ContainsKey($key)){$other.Add([ordered]@{object=$object;status=$block.status;scope='outside selected configuration/default graph; not accepted as proof'});continue}
  Need ($block.status-ceq'VALID') 'Current required Ninja dependencies must be VALID with the exact declared count'
  $headers=@();foreach($header in $block.headers){$headers+=,[ordered]@{raw=$header;resolved=(Full $header $build)}}
  $records.Add([ordered]@{object=$object;line=$block.line;rawHeader=$block.rawHeader;status=$block.status;count=$block.count;mtime=$block.mtime;headers=$headers})
 };Need ($records.Count-eq$wanted.Count) 'Every current compiler object needs a real dependency block';[ordered]@{selected=$records.ToArray();outsideScope=$other.ToArray()}
}

# Exact generated compiler inputs are derived from frozen test writers and bytes.
# These helpers never run CMake or a tool; selected Ninja bindings choose roles.
function GeneratedTestHash([byte[]]$bytes){$hash=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}}
function GeneratedTestRelative([string]$value){Need ($value.Length-gt0-and$value.Length-le1024-and$value-notmatch'[\\:;\x00-\x1f]'-and-not$value.StartsWith('/')-and($value-split'/').Where({$_-ceq''-or$_-ceq'.'-or$_-ceq'..'-or$_.EndsWith('.')-or$_.EndsWith(' ')}).Count-eq0) 'Exact portable generated test path';return $value}
function GeneratedTestFrozen([string]$value,$allowed,[scriptblock]$readBytes){
 $key=Norm $value;Need ($allowed.ContainsKey($key)) ('Generated test source was not frozen: '+$value);$pin=$allowed[$key]
 [byte[]]$bytes=& $readBytes $value;Need ($bytes.Length-eq$pin.bytes-and(GeneratedTestHash $bytes)-ceq$pin.sha256) ('Frozen generated test input changed: '+$value);return ,$bytes
}
function GeneratedTestWriter([string]$root,[string]$relative,[string]$expected,$allowed,[scriptblock]$readBytes){
 $value=Join-Path $root $relative;$bytes=GeneratedTestFrozen $value $allowed $readBytes
 $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
 # Only checkout CRLF is normalized for the locked writer text, never fragments.
 $text=$text.Replace([string]([char]13)+[char]10,[string][char]10)
 Need ((GeneratedTestHash ([Text.Encoding]::UTF8.GetBytes($text)))-ceq$expected) ('Unsupported frozen generated test writer: '+$relative);return $text
}
function GeneratedTestInterval([byte[]]$source,[int]$start,[int]$end,[bool]$whole){
 Need ($start-ge0-and$end-gt$start-and$end-le$source.Length) 'Generated test source interval bounds'
 [byte[]]$bytes=[byte[]]::new($end-$start);[Array]::Copy($source,$start,$bytes,0,$bytes.Length)
 if($whole){Need ($start-eq0-and$end-eq$source.Length) 'Generated test whole-file copy interval'}else{Need ($bytes[-1]-eq10-and-not($bytes-ccontains[byte]0)-and-not($bytes-ccontains[byte]13)) 'Generated test fragment requires terminal LF and no CR/NUL'}
 return ,$bytes
}
function GeneratedTestMarker([byte[]]$source,[string]$begin,[string]$end){
 $hex=[BitConverter]::ToString($source).Replace('-','').ToLowerInvariant();$a=[BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($begin)).Replace('-','').ToLowerInvariant();$b=[BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($end)).Replace('-','').ToLowerInvariant()
 Need ([regex]::Matches($hex,$a).Count-eq1-and[regex]::Matches($hex,$b).Count-eq1) 'Generated test requires two unique source markers';$start=$hex.IndexOf($a,[StringComparison]::Ordinal);$stop=$hex.IndexOf($b,[StringComparison]::Ordinal)
 Need ($start%2-eq0-and$stop%2-eq0-and$stop-gt$start) 'Generated test marker byte alignment/order';return [ordered]@{startByte=$start/2;endByteExclusive=$stop/2;bytes=(GeneratedTestInterval $source ($start/2) ($stop/2) $false)}
}
function GeneratedTestActual($derived,$actual,[byte[]]$bytes){
 Need ((Norm $actual.path)-ceq(Norm $derived.pin.path)-and$actual.bytes-eq$derived.pin.bytes-and$actual.sha256-ceq$derived.pin.sha256-and$bytes.Length-eq$derived.pin.bytes-and(GeneratedTestHash $bytes)-ceq$derived.pin.sha256-and[BitConverter]::ToString($bytes)-ceq[BitConverter]::ToString($derived.expectedBytes)) 'Generated test compiler dependency differs from frozen exact source bytes'
 return $actual
}
function GeneratedTestRecipes {
 # These are literal recipes for the hash-locked writers below, never commands
 # or post-build source-binding manifests. Whole files preserve every raw byte.
 @(
  [ordered]@{kind='scheduler-continuation';directory='tests/native/scheduler_continuation';target='scheduler_continuation_contract';units=@('fixture.cpp');cmake='d261554a2ed23ff9fc0b115ea1e3b50134ae6e6b2ca5d4d9fc91058a60619e8a';writer='8d679c914c63ab5237d2ff724933414ace82b7e02be7c0b48d1f6f2e49599de6';whole=[ordered]@{
   'content/browser/SharedTimerWin.h'='SharedTimerWin.h';'content/web_impl_win/TaskSchedulingPolicy.h'='TaskSchedulingPolicy.h';'content/web_impl_win/TaskSchedulingIndex.h'='TaskSchedulingIndex.h'};fragments=@(
   @('content/web_impl_win/WebThreadImpl.cpp','budgets.inc','static const double maxDurationOfFiringTimers','namespace {'),
   @('content/web_impl_win/WebThreadImpl.cpp','scheduler.inc','void WebThreadImpl::schedulerTasks()','std::vector<WebTimerBase*>& WebThreadImpl::timerHeap()'),
   @('content/web_impl_win/WebThreadImpl.cpp','enable.inc','void WebThreadImpl::enableScheduler()','void WebThreadImpl::fire()'),
   @('content/web_impl_win/WebThreadImpl.cpp','retire.inc','void WebThreadImpl::retireCompletedTasksForOwner()','void WebThreadImpl::deleteTimersOnExit()'))},
  [ordered]@{kind='scheduler-index';directory='tests/native/scheduler_index';target='scheduler_index_contract';units=@('fixture.cpp');cmake='c3c0b9d272a043930f3e6b427cfbd3f883bca948267deef8a51352c57adae570';writer=$null;whole=[ordered]@{};fragments=@()},
  [ordered]@{kind='window-close-lifetime';directory='tests/native/window_close_lifetime';target='soui_window_close_lifetime_source_tests';units=@('fixture.cpp');cmake='57dc321e2eed41bb94ac6720f159d0662aa1a9ed9d0e145d4e43c90ec374dfa9';writer='fda11eda8a53a34d7eb12abf7fcb597a50ac2a557dd36cd8095ab6030227c16c';whole=[ordered]@{'content/common/PaintLifetime.h'='PaintLifetime.h'};fragments=@(
   @('wke/wkeWebWindow.cpp','window_helpers.inc',('namespace {'+[char]10+'static bool isOwnedCloseWindow'),'CWebWindow::CWebWindow(COLORREF c)'),
   @('wke/wkeWebWindow.cpp','native_close.inc','bool CWebWindow::_destroyWindow()','void CWebWindow::_initCallbacks()'),
   @('wke/wkeWebWindow.cpp','native_receipt.inc','    case WM_NCDESTROY:','    case WM_TIMER:'),
   @('wke/wkeWebView.cpp','callback_once.inc','bool CWebView::takeWindowDestroyCallback()','void CWebView::quiescePaintCallbacks()'),
   @('wke/wkeWebWindow.cpp','window_visibility.inc','bool CWebWindow::_syncHostVisibility(','void CWebWindow::enable('),
   @('wke/wkeWebWindow.cpp','visibility_messages.inc','    case WM_SHOWWINDOW:','    case WM_CREATE:'),
   @('wke/wkeWebWindow.cpp','resize_visibility.inc','    case WM_SIZE: {','    case WM_DROPFILES:'))},
  [ordered]@{kind='independent-wheel';directory='tests/native/independent_wheel_source';target='soui_independent_wheel_source_tests';units=@('fixture.cpp','generated/cc/input/ScrollController.cpp','generated/cc/trees/ScrollTreeSnapshot.cpp');cmake='8820ff306a52ae7f6c0d25e01d49e94bfa7cb15b3723f91b269daf98d8d8fce2';writer='f41e63a3d81a0db7acf9f908f755a52f972a2c29c94cd7ba938e4eed1ea64cdb';whole=[ordered]@{
   'content/common/InputFrameTrace.h'='content/common/InputFrameTrace.h';'content/common/PaintLifetime.h'='content/common/PaintLifetime.h';
   'cc/input/ScrollController.h'='cc/input/ScrollController.h';'cc/input/ScrollController.cpp'='cc/input/ScrollController.cpp';
   'cc/trees/ScrollTreeSnapshot.h'='cc/trees/ScrollTreeSnapshot.h';'cc/trees/ScrollTreeSnapshot.cpp'='cc/trees/ScrollTreeSnapshot.cpp';
   'cc/trees/InputFrameCoverage.h'='cc/trees/InputFrameCoverage.h';'cc/trees/IndependentInputTrace.h'='cc/trees/IndependentInputTrace.h';
   'cc/trees/DrawFrameWakePolicy.h'='cc/trees/DrawFrameWakePolicy.h';'cc/trees/RenderDiagnosticTrace.h'='cc/trees/RenderDiagnosticTrace.h';
   'cc/trees/LegacyPaintRetryPolicy.h'='cc/trees/LegacyPaintRetryPolicy.h';'cc/trees/ResourceNotificationPolicy.h'='cc/trees/ResourceNotificationPolicy.h'};fragments=@(
   @('cc/trees/LayerTreeHost.cpp','retry_state.inc','struct UiPaintRetryState {','// The queue owns this object.'),
   @('cc/trees/LayerTreeHost.cpp','draw_task.inc','class DrawFrameTask final','static void queueResourceProgress('),
   @('cc/trees/LayerTreeHost.cpp','input_payload.inc','struct CompositorScrollInput {','static CompositorScrollState* findCompositorScrollState('),
   @('cc/trees/LayerTreeHost.cpp','draw_wake.inc','void LayerTreeHost::requestDrawFrameToRunIntoCompositeThread()','bool LayerTreeHost::addCompositorAnimation('),
   @('cc/trees/LayerTreeHost.cpp','owner_retry.inc','void LayerTreeHost::retryPendingOwnerWork()','void LayerTreeHost::beginRecordActions('),
   @('cc/trees/LayerTreeHost.cpp','wheel.inc','bool LayerTreeHost::handleCompositorWheelInput(','bool LayerTreeHost::captureCompositorScrollBarrier('))},
  [ordered]@{kind='host-visibility';directory='tests/native/host_visibility_source';target='soui_host_visibility_source_tests';units=@('fixture.cpp');cmake='c98b1844ce847eeea2f8c18c57259ab252e3a9b4632a8ad12948c04eee4f0ae7';writer='a7a1326fdbc1bfd039bf11fa852decde9d731dd7c8b9c163618a9d979e8accb2';whole=[ordered]@{
   'content/browser/HostVisibilityPolicy.h'='content/browser/HostVisibilityPolicy.h';'cc/trees/DrawFrameWakePolicy.h'='cc/trees/DrawFrameWakePolicy.h';
   'content/common/PaintLifetime.h'='content/common/PaintLifetime.h';'content/common/InputFrameTrace.h'='content/common/InputFrameTrace.h'};fragments=@(
   @('content/browser/WebPageImpl.cpp','visibility_state.inc','struct HostVisibilityWakeState {','int64_t WebPageImpl::m_firstFrameId'),
   @('content/browser/WebPageImpl.cpp','auto_record.inc','class AutoRecordActions {','#if (defined ENABLE_WKE)'),
   @('content/browser/WebPageImpl.cpp','commit_task.inc','class CommitTask :','class VisibilityRestoreTask final'),
   @('content/browser/WebPageImpl.cpp','visibility_owner.inc','class VisibilityRestoreTask final','void WebPageImpl::setNeedsCommitAndNotLayout()'),
   @('content/browser/WebPageImpl.cpp','main_frame.inc','void WebPageImpl::setNeedsCommitAndNotLayout()','void WebPageImpl::onLayerTreeDirty()'),
   @('cc/trees/LayerTreeHost.cpp','host_visible.inc','void LayerTreeHost::setVisible(bool visible)','// Sets the current page scale factor'))},
  [ordered]@{kind='render-diagnostic';directory='tests/native/render_diagnostic_source';target='soui_render_diagnostic_source_tests';units=@('fixture.cpp');cmake='2f6a02940e8954f3f2512cf2ba74692723f0a3d6e2cc7cc547e2e97367cf00d6';writer='174e6b972f02be0f14d8921630e8c81e3f1b6a6e579380bb326d34b690cbce3f';whole=[ordered]@{
   'content/common/InputFrameTrace.h'='content/common/InputFrameTrace.h';'cc/trees/RenderDiagnosticTrace.h'='cc/trees/RenderDiagnosticTrace.h'};fragments=@(
   ,@('cc/trees/GpuSkiaSurface.cpp','gpu_present.inc','bool GpuSkiaSurface::present()','void GpuSkiaSurface::recordPresented()'))}
 )
}
function GeneratedTestRecipeMarker([byte[]]$source,[string]$begin,[string]$end){
 # The locked JS writers require a unique begin and the first following end.
 # Slice bytes directly: unlike the older CMake writers, these accept raw GBK,
 # CRLF and NUL bytes without decoding or normalizing a generated fragment.
 $hex=[BitConverter]::ToString($source).Replace('-','').ToLowerInvariant();$a=[BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($begin)).Replace('-','').ToLowerInvariant();$b=[BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($end)).Replace('-','').ToLowerInvariant()
 Need ($a.Length-gt0-and$b.Length-gt0-and[regex]::Matches($hex,$a).Count-eq1) 'Generated recipe requires one unique begin marker'
 $start=$hex.IndexOf($a,[StringComparison]::Ordinal);$stop=$hex.IndexOf($b,$start+$a.Length,[StringComparison]::Ordinal)
 Need ($start%2-eq0-and$stop-gt$start-and$stop%2-eq0) 'Generated recipe following end marker/alignment'
 $first=[int]($start/2);$last=[int]($stop/2);$bytes=[byte[]]::new($last-$first);[Array]::Copy($source,$first,$bytes,0,$bytes.Length)
 return [ordered]@{startByte=$first;endByteExclusive=$last;bytes=$bytes}
}
function GeneratedTestRecipeDependencies($role,[string]$root,[string]$build,[string]$configuration,$allowed,$selected,[scriptblock]$readBytes,$bindings){
 $targetBase=Full ($role.directory+'/CMakeFiles/'+$role.target+'.dir/'+$configuration) $build
 $selectedRole=@($selected|Where-Object{(Norm $_.object).StartsWith(((Norm $targetBase).TrimEnd('/')+'/'),[StringComparison]::Ordinal)})
 if(-not$selectedRole.Count){return @()}
 Need ($selectedRole.Count-eq$role.units.Count) 'Complete selected generated recipe compiler units'
 $base=Full ($role.directory+'/generated') $build;$records=[Collections.Generic.List[object]]::new()
 foreach($unit in $role.units){
  $object=Full ($unit+'.obj') $targetBase;$matches=@($selectedRole|Where-Object{(Norm $_.object)-ceq(Norm $object)})
  Need ($matches.Count-eq1-and$matches[0].status-ceq'VALID') 'Unique selected generated recipe object binding'
  $source=Full ($role.directory+'/'+$unit) $(if($unit.StartsWith('generated/',[StringComparison]::Ordinal)){$build}else{$root})
  $pair=@($bindings|Where-Object{(Norm $_.object)-ceq(Norm $object)})
  Need ($pair.Count-eq1-and(Norm $pair[0].source)-ceq(Norm $source)) 'Exact generated recipe compiler source/object binding'
  $records.Add([ordered]@{object=$object;source=$source;record=$matches[0];generated=$unit.StartsWith('generated/',[StringComparison]::Ordinal)})
 }
 foreach($pair in $bindings){if((Norm $pair.source).StartsWith(((Norm $base).TrimEnd('/')+'/'),[StringComparison]::Ordinal)){
  Need (@($records|Where-Object{$_.generated-and(Norm $_.object)-ceq(Norm $pair.object)-and(Norm $_.source)-ceq(Norm $pair.source)}).Count-eq1) 'Generated recipe compiler source cannot authorize another object'
 }}
 $support=[Collections.Generic.List[object]]::new();$candidates=@{};$prefix='third_party/sources/miniblink/'
 [void](GeneratedTestWriter $root ($role.directory+'/CMakeLists.txt') $role.cmake $allowed $readBytes);$support.Add($allowed[(Norm (Join-Path $root ($role.directory+'/CMakeLists.txt')))])
 if($null-ne$role.writer){[void](GeneratedTestWriter $root ($role.directory+'/generate.cjs') $role.writer $allowed $readBytes);$support.Add($allowed[(Norm (Join-Path $root ($role.directory+'/generate.cjs')))])}
 foreach($source in $role.whole.Keys){
  $value=Join-Path $root ($prefix+(GeneratedTestRelative $source));$raw=GeneratedTestFrozen $value $allowed $readBytes;$output=GeneratedTestRelative $role.whole[$source];$key=Norm (Join-Path $base $output)
  Need (-not$candidates.ContainsKey($key)) 'Unique generated recipe output';$support.Add($allowed[(Norm $value)])
  $candidates[$key]=[ordered]@{source=$allowed[(Norm $value)];range=[ordered]@{startByte=0;endByteExclusive=$raw.Length;bytes=$raw};output=$output;exactWholeFile=$true}
 }
 foreach($part in $role.fragments){
  $value=Join-Path $root ($prefix+(GeneratedTestRelative $part[0]));$raw=GeneratedTestFrozen $value $allowed $readBytes;$range=GeneratedTestRecipeMarker $raw $part[2] $part[3];$output=GeneratedTestRelative $part[1];$key=Norm (Join-Path $base $output)
  Need (-not$candidates.ContainsKey($key)) 'Unique generated recipe output';$support.Add($allowed[(Norm $value)])
  $candidates[$key]=[ordered]@{source=$allowed[(Norm $value)];range=$range;output=$output;exactWholeFile=$false}
 }
 $seen=@{}
 foreach($record in $records){
  $used=@{};$headersSeen=@{}
  if($record.generated){$key=Norm $record.source;Need ($candidates.ContainsKey($key)) 'Unknown generated recipe compiler source';$used[$key]=$true}
  foreach($header in $record.record.headers){$key=Norm $header.resolved;if(-not$key.StartsWith(((Norm $base).TrimEnd('/')+'/'),[StringComparison]::Ordinal)){continue}
   Need ($candidates.ContainsKey($key)) ('Unknown generated test dependency: '+$header.resolved);Need (-not$headersSeen.ContainsKey($key)) 'Duplicate selected generated test dependency';$headersSeen[$key]=$true;$used[$key]=$true
  }
  foreach($key in $used.Keys){Need (-not$allowed.ContainsKey($key)) 'Generated test output cannot masquerade as pre-Build frozen input';if(-not$seen.ContainsKey($key)){$seen[$key]=[Collections.Generic.List[string]]::new()};$seen[$key].Add($record.object)}
 }
 Need ($seen.Count-eq$candidates.Count) 'Complete selected generated test dependency set required'
 foreach($key in $seen.Keys){$entry=$candidates[$key];$bytes=$entry.range.bytes;$objects=$seen[$key].ToArray()
  [ordered]@{pin=[ordered]@{path=(Join-Path $base $entry.output);bytes=$bytes.Length;sha256=(GeneratedTestHash $bytes)};kind='exact test source bytes from frozen writer';derivationSchema='soui.generated-test-source-bytes/1';preBuildFrozen=$false;writerFamily=$role.kind;selectedObject=$objects[0];selectedObjects=$objects;source=$entry.source;writerPins=$support.ToArray();startByte=$entry.range.startByte;endByteExclusive=$entry.range.endByteExclusive;exactWholeFile=$entry.exactWholeFile;expectedBytes=$bytes}
 }
}
function GeneratedTestDependencies([string]$root,[string]$build,[string]$configuration,$allowed,$selected,[scriptblock]$readBytes,$bindings=@()){
 Need ($configuration-cin@('Debug','Release','RelWithDebInfo')) 'Selected generated test configuration'
 $roles=@(
  [ordered]@{kind='endpoint';directory='tests/native/vendor_lifetime';target='soui_vendor_endpoint_lifetime_tests';unit='endpoint_tests.cpp'},
  [ordered]@{kind='worker';directory='tests/native/vendor_lifetime/worker_lifetime';target='soui_worker_lifetime_regressions';unit='fixture.cpp'},
  [ordered]@{kind='request-close';directory='tests/native/request_close';target='soui_request_close_regressions';unit='request_close_tests.cpp'})+@(GeneratedTestRecipes)
 $result=[Collections.Generic.List[object]]::new()
 foreach($role in $roles){
  if($role.Contains('cmake')){foreach($row in @(GeneratedTestRecipeDependencies $role $root $build $configuration $allowed $selected $readBytes $bindings)){$result.Add($row)};continue}
  $object=Full ($role.directory+'/CMakeFiles/'+$role.target+'.dir/'+$configuration+'/'+$role.unit+'.obj') $build;$records=@($selected|Where-Object{(Norm $_.object)-ceq(Norm $object)})
  if($records.Count-eq0){continue};Need ($records.Count-eq1-and$records[0].status-ceq'VALID') 'Unique selected generated test object binding'
  $base=Full ($role.directory+'/generated') $build;$candidates=@{};$support=[Collections.Generic.List[object]]::new();$writerPath=Join-Path $root ($role.directory+'/CMakeLists.txt')
  if($role.kind-ceq'endpoint'){
   [void](GeneratedTestWriter $root ($role.directory+'/CMakeLists.txt') '9be46c7881da9a5a2c4222a216a87d7604abef0355ddc1158b04eb664c22be63' $allowed $readBytes);$support.Add($allowed[(Norm $writerPath)])
   $sourcePath=Join-Path $root 'third_party/sources/miniblink/wke/wke.cpp';$sourceBytes=GeneratedTestFrozen $sourcePath $allowed $readBytes;$range=GeneratedTestMarker $sourceBytes 'struct InteractionEndpointImpl {' 'void WKE_CALL_TYPE retainInteractionEndpoint('
   $candidates[(Norm (Join-Path $base 'interaction_endpoint_source.inc'))]=[ordered]@{source=$allowed[(Norm $sourcePath)];range=$range;output='interaction_endpoint_source.inc'}
  }elseif($role.kind-ceq'worker'){
   [void](GeneratedTestWriter $root ($role.directory+'/CMakeLists.txt') '11383ab178986c14403e83d3c5222de5ca9166614750092b6e0de339de72f9ae' $allowed $readBytes)
   [void](GeneratedTestWriter $root ($role.directory+'/source-bytes.cmake') '73f4a5ce9cf44d321deb7523b4ea4100a596f8806c9777fe077afa9803ee3d45' $allowed $readBytes)
   $mapText=GeneratedTestWriter $root ($role.directory+'/source-map.json') '5988addabac8130d74e0a9b160a03f7154a5ee6927f42c7622249984296f83fd' $allowed $readBytes;$map=$mapText|ConvertFrom-Json
   Need ($map.schema-ceq'worker-lifetime-source-map/1'-and$map.sources.Count-eq14-and$map.copies.Count-eq13-and$map.payloads.Count-eq22) 'Exact frozen worker source map'
   foreach($relative in @('CMakeLists.txt','source-bytes.cmake','source-map.json')){$support.Add($allowed[(Norm (Join-Path $root ($role.directory+'/'+$relative)))])}
   $sources=@{};foreach($p in $map.sources){$relative=GeneratedTestRelative $p.path;$value=Join-Path $root $relative;Need (-not$sources.ContainsKey((Norm $value))) 'Unique frozen worker source';$raw=GeneratedTestFrozen $value $allowed $readBytes;Need ($raw.Length-eq$p.bytes-and(GeneratedTestHash $raw)-ceq$p.sha256) 'Worker map whole-source pin differs';$sources[(Norm $value)]=$raw;$support.Add($allowed[(Norm $value)])}
   foreach($p in $map.payloads){$relative=GeneratedTestRelative $p.path;$value=Join-Path $root ($role.directory+'/'+$relative);$raw=GeneratedTestFrozen $value $allowed $readBytes;Need ($raw.Length-eq$p.bytes-and(GeneratedTestHash $raw)-ceq$p.sha256) 'Worker map payload pin differs';$support.Add($allowed[(Norm $value)])}
   foreach($copy in $map.copies){$relative=GeneratedTestRelative $copy.source;$output=GeneratedTestRelative $copy.output;$value=Join-Path $root $relative;Need ($sources.ContainsKey((Norm $value))-and$copy.exactWholeFile-is[bool]) 'Worker copy needs exact frozen source and boolean kind';$raw=$sources[(Norm $value)];Need ($copy.startByte-is[long]-or$copy.startByte-is[int]) 'Worker start-byte integer';Need ($copy.endByteExclusive-is[long]-or$copy.endByteExclusive-is[int]) 'Worker end-byte integer';$bytes=GeneratedTestInterval $raw $copy.startByte $copy.endByteExclusive $copy.exactWholeFile
    Need ($bytes.Length-eq$copy.bytes-and(GeneratedTestHash $bytes)-ceq$copy.sha256) 'Worker exact source range differs from map';$key=Norm (Join-Path $base $output);Need (-not$candidates.ContainsKey($key)) 'Unique worker generated output';$candidates[$key]=[ordered]@{source=$allowed[(Norm $value)];range=[ordered]@{startByte=$copy.startByte;endByteExclusive=$copy.endByteExclusive;bytes=$bytes};output=$output;exactWholeFile=$copy.exactWholeFile}
   }
  }else{
   $text=GeneratedTestWriter $root ($role.directory+'/CMakeLists.txt') '4c74467ea0349b1a2a5efb0f8893fa5c972ad770023c8c861ee2e0d08318f1a6' $allowed $readBytes;$support.Add($allowed[(Norm $writerPath)])
   $matches=[regex]::Matches($text,'(?m)^close_extract\(([a-z0-9-]+) "\$\{(close_source|close_header)\}" "([^"\r\n]+)" "([^"\r\n]+)"\)$');Need ($matches.Count-eq13) 'Exact request-close extraction declarations'
   foreach($match in $matches){$extension=$(if($match.Groups[2].Value-ceq'close_source'){'.cpp'}else{'.h'});$value=Join-Path $root ('third_party/sources/miniblink/wke/CursorDescriptorBroker'+$extension);$raw=GeneratedTestFrozen $value $allowed $readBytes;$range=GeneratedTestMarker $raw $match.Groups[3].Value $match.Groups[4].Value;$output=$match.Groups[1].Value+'.inc';$key=Norm (Join-Path $base $output);Need (-not$candidates.ContainsKey($key)) 'Unique request-close output';$candidates[$key]=[ordered]@{source=$allowed[(Norm $value)];range=$range;output=$output}}
  }
  $seen=@{};foreach($header in $records[0].headers){$key=Norm $header.resolved;if(-not$key.StartsWith(((Norm $base).TrimEnd('/')+'/'),[StringComparison]::Ordinal)){continue}
   Need ($candidates.ContainsKey($key)) ('Unknown generated test dependency: '+$header.resolved);Need (-not$allowed.ContainsKey($key)) 'Generated test output cannot masquerade as pre-Build frozen input';Need (-not$seen.ContainsKey($key)) 'Duplicate selected generated test dependency';$seen[$key]=$true;$entry=$candidates[$key];$bytes=$entry.range.bytes
   $result.Add([ordered]@{pin=[ordered]@{path=(Join-Path $base $entry.output);bytes=$bytes.Length;sha256=(GeneratedTestHash $bytes)};kind='exact test source bytes from frozen writer';derivationSchema='soui.generated-test-source-bytes/1';preBuildFrozen=$false;writerFamily=$role.kind;selectedObject=$object;source=$entry.source;writerPins=$support.ToArray();startByte=$entry.range.startByte;endByteExclusive=$entry.range.endByteExclusive;exactWholeFile=$(if($entry.Contains('exactWholeFile')){$entry.exactWholeFile}else{$false});expectedBytes=$bytes})
  };Need ($seen.Count-eq$candidates.Count) 'Complete selected generated test dependency set required'
 }
 return $result.ToArray()
}

function InstalledPinSame($left,$right){return ((Norm $left.path)-ceq(Norm $right.path)-and$left.bytes-eq$right.bytes-and$left.sha256-ceq$right.sha256)}
function InstalledPaths([string]$owner,$contextInput){
 $value=$contextInput.installedConsumer
 Need ($null-ne$value-and(SameSet ([string[]]@($value.PSObject.Properties.Name)) ([string[]]@('schema','suiteDirectory','pending','requestFile','sdkAccepted')))-and$value.schema-ceq'soui.installed-acceptance-llvm-context/v1') 'Exact installed acceptance context required'
 Need ($contextInput.sdkOnly-is[bool]-and-not$contextInput.sdkOnly-and$contextInput.withWebView2-is[bool]-and-not$contextInput.withWebView2-and$null-eq$contextInput.previousObservation) 'Installed acceptance requires its own fresh ordinary consumer graph'
 $suite=[IO.Path]::GetFullPath($value.suiteDirectory);$parent=Join-Path $owner 'out/acceptance'
 Need ((Norm ([IO.Path]::GetDirectoryName($suite)))-ceq(Norm $parent)-and[IO.Path]::GetFileName($suite)-cmatch'^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') 'Exact ordinary acceptance suite ownership'
 $paths=[ordered]@{suite=$suite;source=(Join-Path $suite 'consumer-source');build=(Join-Path $suite 'consumer');install=(Join-Path $suite 'install');evidence=(Join-Path $suite 'consumer-llvm-observer');nativeLog=(Join-Path $suite 'compile.log');pending=(Join-Path $suite 'pending.json');consumerRequest=(Join-Path $suite ('consumer/llvm-profile-request-'+$contextInput.configuration+'.json'));executable=(Join-Path $suite ('consumer/bin/'+$contextInput.configuration+'/SouiAcceptance.exe'));sdkBuild=(Join-Path $owner ('out/sdk-'+$contextInput.architecture));sdkSource=(Join-Path $owner 'sdk');originalSource=(Join-Path $owner 'tests/acceptance-app')}
 $paths.sdkReport=Join-Path $paths.sdkBuild ('build-'+$contextInput.configuration+'.json');$paths.sdkRequest=Join-Path $paths.sdkBuild ('llvm-profile-request-'+$contextInput.configuration+'.json');$paths.sdkAccepted=Join-Path $paths.sdkBuild ('standard-llvm-'+$contextInput.configuration+'.json')
 $paths.sdkArchive=Join-Path $paths.sdkBuild ($contextInput.configuration+'/soui_sdk.lib');$paths.installedArchive=Join-Path $paths.install ('lib/'+$contextInput.configuration+'/soui_sdk.lib')
 foreach($pair in @(@($contextInput.buildDirectory,$paths.build),@($contextInput.evidenceDirectory,$paths.evidence),@($contextInput.nativeLog.path,$paths.nativeLog),@($contextInput.buildReport.path,$paths.sdkReport),@($contextInput.requestFile.path,$paths.sdkRequest),@($value.pending.path,$paths.pending),@($value.requestFile.path,$paths.consumerRequest),@($value.sdkAccepted.path,$paths.sdkAccepted))){Need ((Norm ([IO.Path]::GetFullPath($pair[0])))-ceq(Norm $pair[1])) 'Installed context path differs from its exact owned role'}
 return $paths
}
function RememberInstalled($pin){
 $actual=MatchPin $pin;$key=Norm $actual.path
 if($installedPins.ContainsKey($key)){Need (InstalledPinSame $installedPins[$key] $actual) 'Conflicting installed evidence pin'}else{$installedPins[$key]=$actual}
 return $actual
}
function InstalledTree([string]$directory){
 [void](Plain $directory);$files=@()
 foreach($entry in @(Get-ChildItem -LiteralPath $directory -Recurse -Force)){[void](Plain $entry.FullName);if(-not$entry.PSIsContainer){$files+=,$entry.FullName}}
 return [string[]]$files
}
function NaturalImportedCapture($capture){
 foreach($name in @('raw','context','ready','finished')){[void](RememberInstalled $capture.$name)}
 $text=RawText $capture.raw.path;Need ($text.EndsWith("`n")) 'Complete imported SDK Owned stream required'
 $rows=@($text.TrimEnd([char]10).Split([char]10)|ForEach-Object{$_|ConvertFrom-Json});$begin=$rows[0];$end=$rows[-1]
 $ctx=RawText $capture.context.path|ConvertFrom-Json;$published=RawText $capture.ready.path|ConvertFrom-Json;$done=RawText $capture.finished.path|ConvertFrom-Json
 Need ($begin.event-ceq'start'-and$end.event-ceq'terminal'-and@($rows|Where-Object{$_.event-ceq'start'}).Count-eq1-and@($rows|Where-Object{$_.event-ceq'terminal'}).Count-eq1-and@($rows|Where-Object{$_.event-ceq'root-exit'}).Count-eq1-and$begin.creationFileTime-is[string]-and$begin.creationFileTime-cmatch'^[1-9][0-9]{0,19}$'-and$begin.pid-eq$published.pid-and$begin.creationFileTime-ceq$published.creationFileTime-and$begin.pid-eq$done.pid-and$begin.creationFileTime-ceq$done.creationFileTime-and$begin.jobAssignedBeforeResume-eq$true-and$begin.breakawayAllowed-eq$false) 'Imported SDK actual Owned subject identity'
 Need ($done.passed-eq$true-and$done.exitCode-eq0-and$done.supervisorExitCode-eq0-and$done.closureConfirmed-eq$true-and$null-eq$done.signal-and$null-eq$done.spawnError-and$null-eq$done.reportError-and$done.stopRequested-eq$false-and$done.fallbackUsed-eq$false-and$end.reason-ceq'completed'-and$end.rootExited-eq$true-and$end.rootExitCode-eq0-and$end.passed-eq$true-and$end.activeBeforeCleanup-eq0-and$end.activeProcesses-eq0-and$end.terminatedOwnedJob-eq$false-and$end.limitTerminatedProcesses-eq0-and$end.closureConfirmed-eq$true-and$done.reportSha256-ceq$capture.raw.sha256) 'Imported SDK acceptance must have natural whole Owned closure'
 Need ($ctx.timeoutMs-eq$begin.timeoutMs-and$ctx.drainMs-eq2000-and$ctx.drainMs-eq$begin.drainMs-and(Norm $ctx.cwd)-ceq(Norm $root)-and$ctx.executableSha256-ceq$done.executableSha256-and$ctx.runnerSha256-ceq$done.runnerSha256) 'Imported SDK original Owned budget/tool/cwd binding'
 foreach($row in $rows){if($row.event-ceq'owned-member'-and$null-ne$row.image){Need ([IO.Path]::GetFileName($row.image).ToLowerInvariant()-cnotin@('cl.exe','link.exe','lib.exe')) 'Foreign MSVC tool in imported LLVM proof';foreach($role in $roleNames){if([IO.Path]::GetFileName($row.image)-ieq[IO.Path]::GetFileName($profile.ToolPins.$role.path)){[void](ToolIdentity $row.image $role)}}}}
 return $begin
}
function ReadInstalledSdk($paths){
 $value=$cfg.installedConsumer;$acceptedPin=RememberInstalled $value.sdkAccepted;$accepted=RawText $acceptedPin.path|ConvertFrom-Json
 $importedDebug=$accepted.schema-ceq'soui.standard-llvm-accepted-build/v2'
 Need (($importedDebug-or$accepted.schema-ceq'soui.standard-llvm-accepted-build/v1')-and$accepted.architecture-ceq$cfg.architecture-and$accepted.configuration-ceq$cfg.configuration-and$accepted.sdkOnly-eq$true-and$accepted.qualificationPassed-eq$false-and$accepted.releaseReady-eq$false-and$accepted.buildId-ceq$sourceBuild.buildId-and$accepted.sourceSha256-ceq$sourceBuild.source.sha256) 'Current independently accepted SDK build identity'
 $attempt=[IO.Path]::GetFullPath($accepted.attemptDirectory);Need ((Norm ([IO.Path]::GetDirectoryName($attempt)))-ceq(Norm (Join-Path $root 'out/standard-build'))-and[IO.Path]::GetFileName($attempt)-cmatch'^attempt-[0-9a-z-]+$') 'Owned accepted SDK attempt directory'
 foreach($name in @('observation','inputs','observed','requestFile','buildReportSnapshot')){[void](RememberInstalled $accepted.$name);if($name-cne'requestFile'){Need ((Norm ([IO.Path]::GetFullPath($accepted.$name.path))).StartsWith(((Norm $attempt).TrimEnd('/')+'/'),[StringComparison]::Ordinal)) 'SDK accepted proof must belong to its actual attempt'}}
 Need (InstalledPinSame $accepted.requestFile $requestPin) 'SDK accepted request differs from current own request'
 Need ($accepted.buildReportSnapshot.bytes-eq$sourceBuildPin.bytes-and$accepted.buildReportSnapshot.sha256-ceq$sourceBuildPin.sha256) 'Current SDK report differs from accepted raw snapshot'
 $resultPin=RememberInstalled (ObsPin (Join-Path $attempt 'result.json'));$result=RawText $resultPin.path|ConvertFrom-Json
 Need ($result.accepted-eq$true-and$result.genericSupervisionPassed-eq$true-and$result.compilerTailAcceptance-eq$false) 'Imported SDK original standard acceptance failed'
 $sdk=RawText $accepted.observation.path|ConvertFrom-Json;$frozen=RawText $accepted.inputs.path|ConvertFrom-Json
 Need ($(if($importedDebug){$sdk.schema-ceq'soui.standard-llvm-build-observation/v2'-and$frozen.schema-ceq'soui.standard-build-inputs/v2'}else{$sdk.schema-ceq'soui.standard-llvm-build-observation/v1'-and$frozen.schema-ceq'soui.standard-build-inputs/v1'})-and$sdk.compilerVerified-eq$true-and$sdk.profile-ceq$cfg.profile-and$sdk.sdkOnly-eq$true-and(Norm $sdk.root)-ceq(Norm $root)-and$sdk.architecture-ceq$cfg.architecture-and$sdk.configuration-ceq$cfg.configuration-and$sdk.inputIdentity-ceq$profile.InputIdentity-and$sdk.bridgeIdentity-ceq$profile.BridgeIdentity-and$sdk.qualificationPassed-eq$false-and$sdk.releaseReady-eq$false-and$sdk.buildReport.sha256-ceq$sourceBuildPin.sha256-and(InstalledPinSame $sdk.requestFile $requestPin)-and$frozen.architecture-ceq$cfg.architecture-and$frozen.configuration-ceq$cfg.configuration-and$frozen.sdkOnly-eq$true-and$frozen.compilerTail-eq$false-and$frozen.nativeToolchain-ceq'LLVM22MSVC142'-and$frozen.llvm.inputIdentity-ceq$profile.InputIdentity-and$frozen.llvm.bridgeIdentity-ceq$profile.BridgeIdentity) 'Imported SDK observation/profile/source-build binding'
 $subject=NaturalImportedCapture $accepted.buildCapture;[void](NaturalImportedCapture $accepted.observerCapture)
 $importedOuter=if($importedDebug){$sdk.outerObserverCapture}else{$sdk.buildCapture}
 Need ($importedOuter.pid-eq$subject.pid-and$importedOuter.creationFileTime-ceq$subject.creationFileTime-and(InstalledPinSame $importedOuter.raw $accepted.buildCapture.raw)-and(InstalledPinSame $importedOuter.context $accepted.buildCapture.context)-and(InstalledPinSame $importedOuter.ready $accepted.buildCapture.ready)-and(InstalledPinSame $importedOuter.finished $accepted.buildCapture.finished)-and$importedOuter.wholeJobClosed-eq$true-and$importedOuter.forced-eq$false) 'Imported SDK observer must bind the actual closed build'
 if($importedDebug){
  foreach($pin in @($sdk.debugCapture.input,$sdk.debugCapture.raw,$sdk.debugCapture.prepared,$sdk.debugCapture.observer)){[void](RememberInstalled $pin)}
  Need ((ConvertTo-Json -InputObject $sdk.debugCapture -Depth 20 -Compress)-ceq(ConvertTo-Json -InputObject $frozen.llvm.debugBinding -Depth 20 -Compress)) 'Imported dual-root original binding differs'
  $native=RawText $sdk.debugCapture.input.path|ConvertFrom-Json;$text=RawText $sdk.debugCapture.raw.path;Need ($text.EndsWith("`n")-and$text-notmatch'\r') 'Imported complete native stream'
  $rows=@($text.TrimEnd([char]10).Split([char]10)|ForEach-Object{$_|ConvertFrom-Json});Need ($rows.Count-gt1-and$rows.Count-le100000) 'Imported finite native stream'
  for($i=0;$i-lt$rows.Count;$i++){Need ($rows[$i].schema-ceq'soui.host64-build-debug-addendum/2'-and$rows[$i].sequence-eq$i+1-and$rows[$i].nonce-ceq$native.nonce-and$rows[$i].inputSha256-ceq$sdk.debugCapture.input.sha256) 'Imported raw nonce/sequence/input SHA'}
  $begin=$rows[0];$end=$rows[-1];$roots=@($rows|Where-Object{$_.event-ceq'create-paused'-and$_.buildRoot-eq$true});$rootQueries=@($rows|Where-Object{$_.event-ceq'commandline-paused'-and$_.buildRoot-eq$true})
  Need ($native.schema-ceq'soui.host64-build-debug-inputs/2'-and$native.sdkArchitecture-ceq$cfg.architecture-and$native.configuration-ceq$cfg.configuration-and(Norm $native.root)-ceq(Norm $root)-and$begin.observerPid-eq$subject.pid-and$begin.observerCreationFileTime-ceq$subject.creationFileTime-and$end.event-ceq'terminal'-and$end.passed-eq$true-and$end.forced-eq$false-and$end.allMembersSignaled-eq$true-and$end.allPausedQueriesClosed-eq$true-and$end.allLoaderSequences-eq$true-and$end.debugProtocolValid-eq$true-and$end.activeProcesses-eq0-and$end.limitTerminatedProcesses-eq0-and$end.rootExitCode-eq0-and$roots.Count-eq1-and$rootQueries.Count-eq1-and$roots[0].pid-eq$end.rootPid-and$roots[0].creationFileTime-ceq$end.rootCreationFileTime-and$sdk.buildCapture.pid-eq$end.rootPid-and$sdk.buildCapture.creationFileTime-ceq$end.rootCreationFileTime-and$sdk.buildCapture.role-ceq'inner-build'-and(InstalledPinSame $sdk.buildCapture.raw $sdk.debugCapture.raw)-and(InstalledPinSame $sdk.buildCapture.input $sdk.debugCapture.input)-and$sdk.fastToolEvidence.actualDebugRawSha256-ceq$sdk.debugCapture.raw.sha256-and$sdk.fastToolEvidence.experimentalLifecycleObserved-eq$true) 'Imported actual inner CREATE/whole-argv/EXIT binding'
 }
 $rawInputs=@{};foreach($pin in $sdk.frozenDependencyPins){$key=Norm $pin.path;Need (-not$rawInputs.ContainsKey($key)) 'Unique accepted SDK raw dependency pins';$rawInputs[$key]=$pin}
 foreach($row in $sourceBuild.source.files){$p=Full $row.path $root;$key=Norm $p;Need ($rawInputs.ContainsKey($key)) 'Every accepted SDK source member needs its original raw pin';[void](RememberInstalled $rawInputs[$key])}
 foreach($pin in $sdk.evidencePins){[void](RememberInstalled $pin)}
 $original=RememberInstalled (ObsPin $paths.sdkArchive);$installed=RememberInstalled (ObsPin $paths.installedArchive)
 Need ($sourceBuild.library.files.Count-eq1-and$sourceBuild.library.files[0].path-ceq($cfg.configuration+'/soui_sdk.lib')-and$original.bytes-eq$sourceBuild.library.files[0].bytes-and$original.sha256-ceq$sourceBuild.library.files[0].sha256-and$installed.bytes-eq$original.bytes-and$installed.sha256-ceq$original.sha256) 'Installed archive must equal the current accepted SDK archive'
 $archives=@($sdk.binaryRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $paths.sdkArchive)-and$_.role-ceq'ARCHIVER'})
 Need ($archives.Count-eq1-and(InstalledPinSame $archives[0].output $original)-and$archives[0].archiveMembers.Count-gt0-and$archives[0].machine-eq$expectedMachine) 'Accepted SDK actual archive/member proof required'
 $actual=ArchiveMembers $paths.sdkArchive
 Need (SameSet ([string[]]@($actual.members|ForEach-Object{$_.name+'|'+$_.bytes+'|'+$_.sha256+'|'+$_.machine})) ([string[]]@($archives[0].archiveMembers|ForEach-Object{$_.name+'|'+$_.bytes+'|'+$_.sha256+'|'+$_.machine}))) 'Current SDK archive members differ from accepted compiled objects'
 Need (@($actual.members|Where-Object{$_.machine-ne$expectedMachine}).Count-eq0) 'Imported SDK archive architecture differs'
 Need ($archives[0].inputObjects.Count-gt0-and$actual.members.Count-eq$archives[0].inputObjects.Count-and(SameSet ([string[]]@($actual.members|ForEach-Object{[string]$_.bytes+'|'+$_.sha256})) ([string[]]@($archives[0].inputObjects|ForEach-Object{[string]$_.bytes+'|'+$_.sha256})))) 'Imported SDK archive must still equal its actually compiled input objects'
 foreach($object in $archives[0].inputObjects){[void](RememberInstalled $object)}
 $headerRows=@($sourceBuild.source.files|Where-Object{$_.path.StartsWith('sdk/include/',[StringComparison]::Ordinal)});Need ($headerRows.Count-gt0) 'Accepted current SDK header inventory required';$headerPins=@();$sourceHeaderPins=@()
 foreach($row in $headerRows){$sourcePath=Full $row.path $root;$raw=RememberInstalled $rawInputs[(Norm $sourcePath)];$copy=RememberInstalled (ObsPin (Join-Path $paths.install $row.path.Substring(4)));Need ($copy.bytes-eq$raw.bytes-and$copy.sha256-ceq$raw.sha256) 'Installed SDK header differs from accepted current raw source';$headerPins+=,$copy;$sourceHeaderPins+=,$raw}
 # sdk/CMakeLists.txt installs this fixed public header in addition to sdk/include.
 # Bind its accepted original raw bytes; an arbitrary extra installed header is not admitted.
 $actionJsonRows=@($sourceBuild.source.files|Where-Object{$_.path-ceq'third_party/include/json/json.hpp'})
 Need ($actionJsonRows.Count-eq1) 'Accepted SDK ActionJson source member required'
 $actionJsonSource=Full $actionJsonRows[0].path $root;$actionJsonKey=Norm $actionJsonSource
 Need ($rawInputs.ContainsKey($actionJsonKey)) 'Accepted SDK ActionJson original raw pin required'
 $actionJsonRaw=RememberInstalled $rawInputs[$actionJsonKey]
 $actionJsonCopy=RememberInstalled (ObsPin (Join-Path $paths.install 'include/soui/action-json/json.hpp'))
 Need ($actionJsonCopy.bytes-eq$actionJsonRaw.bytes-and$actionJsonCopy.sha256-ceq$actionJsonRaw.sha256) 'Installed ActionJson header differs from accepted current raw source'
 $headerPins+=,$actionJsonCopy
 Need (SameSet ([string[]]@(InstalledTree (Join-Path $root 'sdk/include')|ForEach-Object{Norm $_})) ([string[]]@($headerRows|ForEach-Object{Norm (Full $_.path $root)}))) 'Current SDK header inventory drift'
 Need (SameSet ([string[]]@(InstalledTree (Join-Path $paths.install 'include')|ForEach-Object{Norm $_})) ([string[]]@($headerPins|ForEach-Object{Norm $_.path}))) 'Installed SDK header inventory drift'
 $pendingPin=RememberInstalled $value.pending;$pending=RawText $pendingPin.path|ConvertFrom-Json
 Need ($pending.architecture-ceq$cfg.architecture-and$pending.configuration-ceq$cfg.configuration-and$pending.buildId-ceq$sourceBuild.buildId-and$pending.sourceSha256-ceq$sourceBuild.source.sha256-and$pending.consumerSource.files.Count-gt0) 'Consumer pending record differs from current accepted SDK'
 $sourcePins=@();$originalSourcePins=@();foreach($row in $pending.consumerSource.files){Need ($row.path-is[string]-and$row.path-cmatch'^[A-Za-z0-9_./-]+$'-and$row.path-cnotmatch'(?:^|/)\.{1,2}(?:/|$)') 'Safe relative consumer source member';$copy=RememberInstalled ([ordered]@{path=(Join-Path $paths.source $row.path);bytes=$row.bytes;sha256=$row.sha256});$originalSource=RememberInstalled (ObsPin (Join-Path $paths.originalSource $row.path));Need ($copy.bytes-eq$originalSource.bytes-and$copy.sha256-ceq$originalSource.sha256) 'Copied consumer source differs from current ordinary fixture';$sourcePins+=,$copy;$originalSourcePins+=,$originalSource}
 foreach($directory in @($paths.source,$paths.originalSource)){Need (SameSet ([string[]]@(InstalledTree $directory|ForEach-Object{Norm $_.Substring($directory.Length+1)})) ([string[]]@($pending.consumerSource.files|ForEach-Object{Norm $_.path}))) 'Exact ordinary consumer source inventory required'}
 $consumerRequestPin=RememberInstalled $value.requestFile;$consumerRequest=RawText $consumerRequestPin.path|ConvertFrom-Json
 Need ($consumerRequest.schema-ceq'soui.build-toolchain-request/v1'-and$consumerRequest.profile-ceq$cfg.profile-and$consumerRequest.architecture-ceq$cfg.architecture-and(ConvertTo-Json $consumerRequest.discovery -Depth 30 -Compress)-ceq(ConvertTo-Json $request.discovery -Depth 30 -Compress)-and(ConvertTo-Json $consumerRequest.toolchain -Depth 10 -Compress)-ceq(ConvertTo-Json $request.toolchain -Depth 10 -Compress)) 'Consumer own request must equal current owner discovery/toolchain'
 return [ordered]@{schema='soui.installed-acceptance-llvm-context/v1';suiteDirectory=$paths.suite;sourceDirectory=$paths.source;buildDirectory=$paths.build;sdkAccepted=$acceptedPin;sdkBuildReport=$sourceBuildPin;sdkRequest=$requestPin;consumerRequest=$consumerRequestPin;pending=$pendingPin;originalSdkArchive=$original;installedSdkArchive=$installed;installedHeaders=$headerPins;originalSdkHeaders=$sourceHeaderPins;consumerSources=$sourcePins;originalConsumerSources=$originalSourcePins;sdkSourceSha256=$sourceBuild.source.sha256;sdkBuildId=$sourceBuild.buildId}
}

function ReleaseConsumerPaths([string]$owner,$contextInput){
 $value=$contextInput.releaseConsumer
 Need ($null-ne$value-and(SameSet ([string[]]@($value.PSObject.Properties.Name)) ([string[]]@('schema','suiteDirectory','pending','requestFile','sdkAccepted')))-and$value.schema-ceq'soui.installed-release-llvm-context/v1') 'Exact installed release consumer context required'
 Need ($contextInput.sdkOnly-is[bool]-and-not$contextInput.sdkOnly-and$contextInput.withWebView2-is[bool]-and-not$contextInput.withWebView2-and$null-eq$contextInput.previousObservation) 'Release consumer requires its own fresh CXX graph'
 $suite=[IO.Path]::GetFullPath($value.suiteDirectory);$parent=Join-Path $owner ('out/release-validation/'+$contextInput.architecture)
 Need ((Norm ([IO.Path]::GetDirectoryName($suite)))-ceq(Norm $parent)-and[IO.Path]::GetFileName($suite)-cmatch'^[0-9a-f]{32}$') 'Exact release consumer output ownership'
 $paths=[ordered]@{suite=$suite;source=(Join-Path $owner 'tests/package-consumer');build=(Join-Path $suite 'consumer');install=(Join-Path $owner ('out/install/'+$contextInput.architecture));evidence=(Join-Path $suite 'consumer-llvm-observer');nativeLog=(Join-Path $suite 'consumer-build.log');pending=(Join-Path $suite 'consumer-pending.json');consumerRequest=(Join-Path $suite ('consumer/llvm-profile-request-'+$contextInput.configuration+'.json'));executable=(Join-Path $suite ('consumer/'+$contextInput.configuration+'/soui_consumer.exe'));sdkBuild=(Join-Path $owner ('out/sdk-'+$contextInput.architecture));sdkSource=(Join-Path $owner 'sdk');originalSource=(Join-Path $owner 'tests/package-consumer')}
 $paths.sdkReport=Join-Path $paths.sdkBuild ('build-'+$contextInput.configuration+'.json');$paths.sdkRequest=Join-Path $paths.sdkBuild ('llvm-profile-request-'+$contextInput.configuration+'.json');$paths.sdkAccepted=Join-Path $paths.sdkBuild ('standard-llvm-'+$contextInput.configuration+'.json')
 $paths.sdkArchive=Join-Path $paths.sdkBuild ($contextInput.configuration+'/soui_sdk.lib');$paths.installedArchive=Join-Path $paths.install ('lib/'+$contextInput.configuration+'/soui_sdk.lib')
 foreach($pair in @(@($contextInput.buildDirectory,$paths.build),@($contextInput.evidenceDirectory,$paths.evidence),@($contextInput.nativeLog.path,$paths.nativeLog),@($contextInput.buildReport.path,$paths.sdkReport),@($contextInput.requestFile.path,$paths.sdkRequest),@($value.pending.path,$paths.pending),@($value.requestFile.path,$paths.consumerRequest),@($value.sdkAccepted.path,$paths.sdkAccepted))){Need ((Norm ([IO.Path]::GetFullPath($pair[0])))-ceq(Norm $pair[1])) 'Release context path differs from its exact owned role'}
 return $paths
}
function ReadReleaseSdk($paths){
 # A local data projection reuses the unchanged archive/header/actual SDK binder.
 # ReleaseConsumerPaths has already validated the distinct fixed release roles.
 $originalCfg=$cfg;$cfg=[ordered]@{};foreach($property in $originalCfg.PSObject.Properties){$cfg[$property.Name]=$property.Value}
 $cfg.installedConsumer=$originalCfg.releaseConsumer
 $binding=ReadInstalledSdk $paths;$binding.schema='soui.installed-release-llvm-context/v1';return $binding
}
$inputPin=ObsPin $InputFile;Need ($inputPin.sha256-ceq$InputSha) 'Observer input bytes differ'
$cfg=RawText $InputFile|ConvertFrom-Json
$debugMode=$cfg.schema-ceq'soui.standard-llvm-observer-inputs/v2'
Need (($debugMode-or$cfg.schema-ceq'soui.standard-llvm-observer-inputs/v1')-and$cfg.profile-ceq'LLVM22MSVC142'-and$cfg.architecture-cin@('x64','x86')-and$cfg.configuration-cin@('Debug','Release')-and$cfg.qualificationPassed-eq$false) 'Explicit current observer scope'
$root=[IO.Path]::GetFullPath($cfg.root);$build=[IO.Path]::GetFullPath($cfg.buildDirectory);$folder=[IO.Path]::GetFullPath($cfg.evidenceDirectory)
$installedMode=$cfg.PSObject.Properties.Name-ccontains'installedConsumer';$installedPaths=$null;$installedBinding=$null;$installedPins=@{}
$releaseMode=$cfg.PSObject.Properties.Name-ccontains'releaseConsumer';Need (-not($installedMode-and$releaseMode)) 'One explicit imported consumer context only'
$consumerDebug=$debugMode-and($installedMode-or$releaseMode)
if($installedMode){$installedPaths=InstalledPaths $root $cfg}elseif($releaseMode){$installedPaths=ReleaseConsumerPaths $root $cfg}else{
Need ((Norm $build)-ceq(Norm (Join-Path $root ('out/'+$(if($cfg.sdkOnly){'sdk-'}else{''})+$cfg.architecture)))) 'Actual owned SDK/application build directory'
}
Need ((Norm ([IO.Path]::GetFullPath($ObservationPath)))-ceq(Norm (Join-Path $folder 'llvm-observation.json'))-and-not(Test-Path -LiteralPath $ObservationPath)) 'Fresh exact observer output'
[void](Plain $root);[void](Plain $build);[void](Plain $folder)
$evidencePins=[Collections.Generic.List[object]]::new();foreach($pin in $cfg.controlPins){$evidencePins.Add((MatchPin $pin))}
$requestPin=MatchPin $cfg.requestFile;$request=RawText $cfg.requestFile.path|ConvertFrom-Json;$profile=$request.discovery
$sourceBuildPin=MatchPin $cfg.buildReport;$sourceBuild=RawText $cfg.buildReport.path|ConvertFrom-Json
if($installedMode-or$releaseMode){
 Need ($sourceBuild.schema-ceq'soui.source-build/v2'-and$sourceBuild.nativeBuild-ceq'passed'-and$sourceBuild.architecture-ceq$cfg.architecture-and$sourceBuild.configuration-ceq$cfg.configuration-and$sourceBuild.sdkOnly-eq$true-and$sourceBuild.releaseReady-eq$false-and$sourceBuild.nativeToolchain.requestFile.sha256-ceq$requestPin.sha256) 'Current SDK source-build report for installed consumer'
}else{
Need ($sourceBuild.schema-ceq'soui.source-build/v2'-and$sourceBuild.nativeBuild-ceq'passed'-and$sourceBuild.architecture-ceq$cfg.architecture-and$sourceBuild.configuration-ceq$cfg.configuration-and$sourceBuild.sdkOnly-eq$cfg.sdkOnly-and$sourceBuild.releaseReady-eq$false-and$sourceBuild.nativeToolchain.requestFile.sha256-ceq$requestPin.sha256) 'Actual source-build report scope/request'
}
Need ($request.schema-ceq'soui.build-toolchain-request/v1'-and$request.profile-ceq$cfg.profile-and$request.architecture-ceq$cfg.architecture-and(@('soui.llvm-profile-inputs/v2','soui.llvm-profile-inputs/v3')-ccontains$profile.InputIdentitySchema)-and$profile.InputIdentity-ceq$cfg.inputIdentity-and$profile.BridgeIdentity-ceq$cfg.bridgeIdentity-and$profile.GeneratorValidated-eq$false-and$profile.Qualified-eq$false-and$null-eq$profile.ActualCompilerVersion) 'Matching request is input, never compiler proof'
$roleNames=@('CLANG','LINKER','ARCHIVER','RC','MT','NINJA','CMD','POWERSHELL');Need (($profile.ToolPins.PSObject.Properties.Name-join',')-ceq($roleNames-join',')-and(($profile.InputIdentitySchema-ceq'soui.llvm-profile-inputs/v2'-and$profile.RequiredInputPins.Count-eq17)-or($profile.InputIdentitySchema-ceq'soui.llvm-profile-inputs/v3'-and$profile.RequiredInputPins.Count-eq18))-and$profile.BridgePins.Count-eq2) 'Exact fixed profile inputs'
$helper=Join-Path $PSScriptRoot 'Tool-FileIdentity.ps1';Need ((Norm $profile.CMakeDefinitions.SOUI_TOOL_IDENTITY_HELPER)-ceq(Norm $helper)) 'Fixed product helper only';[void](MatchPin (@($profile.BridgePins|Where-Object{(Norm $_.path)-ceq(Norm $helper)})[0]));. $helper
$toolIdentities=@{};$aliases=@{}
function LocalToolSpelling([string]$observed){
 # Translate only the Win32 local-drive final-path spelling. This is not
 # identity proof; the unchanged helper must still authenticate both handles.
 $checked=$observed
 if($observed.StartsWith('\\?\',[StringComparison]::Ordinal)){
  Need ($observed-cmatch'^\\\\\?\\[A-Za-z]:\\'-and-not$observed.Contains('/')) 'Only a Win32 extended local-drive tool spelling is supported'
  $checked=$observed.Substring(4)
 }
 Need ($checked-cmatch'^[A-Za-z]:[\\/]' -and $checked.Length-gt3 -and $checked-notmatch'[<>"|?*\x00-\x1f\x7f-\x9f]' -and -not$checked.Substring(2).Contains(':')) 'An ordinary absolute local tool spelling is required'
 foreach($part in ($checked.Substring(3)-split'[\\/]')){
  Need ($part.Length-gt0-and$part-cne'.'-and$part-cne'..'-and-not$part.EndsWith('.')-and-not$part.EndsWith(' ')-and$part-notmatch'^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') 'Malformed or device tool path component'
 }
 return $checked
}
function ToolIdentity([string]$observed,[string]$role){
 Need ($roleNames-ccontains$role) 'Fixed role only';$checked=LocalToolSpelling $observed;$key=$role+'|'+(Norm $observed)
 if(-not$toolIdentities.ContainsKey($key)){$receipt=Assert-ToolFileIdentity -ObservedPath $checked -ExpectedPin $profile.ToolPins.$role;$toolIdentities[$key]=$receipt;$aliases[(Norm $observed)]=Norm $profile.ToolPins.$role.path;$aliases[(Norm $checked)]=Norm $profile.ToolPins.$role.path}
 return $toolIdentities[$key]
}
foreach($role in $roleNames){[void](MatchPin $profile.ToolPins.$role)};foreach($pin in @($profile.RequiredInputPins)+@($profile.BridgePins)){[void](MatchPin $pin)}
if($installedMode){$expectedMachine=$(if($cfg.architecture-ceq'x64'){34404}else{332});$installedBinding=ReadInstalledSdk $installedPaths}
if($releaseMode){$expectedMachine=$(if($cfg.architecture-ceq'x64'){34404}else{332});$installedBinding=ReadReleaseSdk $installedPaths}
function ToolNorm([string]$text){
 # Only proven physical tool aliases and path separators are normalized. Keep
 # case-sensitive definitions/literal arguments intact in command comparisons.
 $value=$text.Trim().Replace('\','/');foreach($alias in @($aliases.Keys|Sort-Object Length -Descending)){
  $replacement=[string]$aliases[$alias]
  $value=[regex]::Replace($value,'(?<![a-z0-9_./-])'+[regex]::Escape($alias)+'(?![a-z0-9_./-])',[Text.RegularExpressions.MatchEvaluator]{param($m)$replacement},[Text.RegularExpressions.RegexOptions]::IgnoreCase)
 };return $value
}
function BinaryCommandTools([string]$command){
 # A configured command can name a physical 8.3 alias even when its tool does
 # not run in an incremental build. Classify only the known direct/CMake
 # wrapper shapes here. ToolIdentity separately checks held file identities;
 # this function never supplies an actual process row or an execution role.
 $body=$command.Trim();$cmd=$null
 $wrapper=[regex]::Match($body,'^(?:"(?<cmd>[^"\r\n]+)"|(?<cmd>[^ \t"\r\n]+))[ \t]+/C[ \t]+"cd \.[ \t]+&&[ \t]+(?<body>[^\r\n]+)[ \t]+&&[ \t]+cd \."$')
 if($wrapper.Success){$cmd=$wrapper.Groups['cmd'].Value;$body=$wrapper.Groups['body'].Value}
 try{
  $args=@(CompileArgv $body)
  if($args.Count-lt2-or@($args|Where-Object{$_-match'[&|<>]'}).Count){return @()}
  $first=LocalToolSpelling $args[0];$name=[IO.Path]::GetFileName($first)
  $tools=@()
  if($name-ieq'llvm-lib.exe'){$tools+=@{path=$first;role='ARCHIVER'}}
  elseif($name-ieq'lld-link.exe'){$tools+=@{path=$first;role='LINKER'}}
  elseif($cmd-and$name-ieq'cmake.exe'-and$args.Count-ge11-and$args[1]-ceq'-E'-and$args[2]-cin@('vs_link_exe','vs_link_dll')-and
    $args[3]-cmatch'^--msvc-ver=[0-9]{1,6}$'-and$args[4].StartsWith('--intdir=')-and$args[4].Length-gt9-and
    $args[5].StartsWith('--rc=')-and$args[6].StartsWith('--mt=')-and$args[7]-ceq'--manifests'-and$args[8]-ceq'--'){
   $linker=LocalToolSpelling $args[9]
   if([IO.Path]::GetFileName($linker)-ine'lld-link.exe'){return @()}
   $tools+=@{path=(LocalToolSpelling $args[5].Substring(5));role='RC'}
   $tools+=@{path=(LocalToolSpelling $args[6].Substring(5));role='MT'}
   $tools+=@{path=$linker;role='LINKER'}
  }else{return @()}
  if($cmd){
   $cmd=LocalToolSpelling $cmd
   if([IO.Path]::GetFileName($cmd)-ine'cmd.exe'){return @()}
   $tools+=@{path=$cmd;role='CMD'}
  }
  return $tools
 }catch{return @()}
}
function BinaryCommandIndex([string[]]$generated,[string[]]$verbose){
 # Authenticate configured tool spellings independently of current execution.
 # Historical compile/link records and actual role requirements remain intact.
 foreach($raw in @($generated)+@($verbose)){foreach($tool in @(BinaryCommandTools $raw)){[void](ToolIdentity $tool.path $tool.role)}}
 # Tool aliases are authenticated before this snapshot. Preserve every row,
 # including duplicates, while avoiding normalization for each binary edge.
 $commands=[Collections.Generic.List[object]]::new()
 foreach($raw in $generated){$canonical=ToolNorm $raw;$commands.Add([ordered]@{raw=$raw;canonical=$canonical;search=$canonical.ToLowerInvariant()})}
 $actual=[Collections.Generic.Dictionary[string,Collections.Generic.List[string]]]::new([StringComparer]::Ordinal)
 foreach($raw in $verbose){
  $canonical=ToolNorm $raw
  if(-not$actual.ContainsKey($canonical)){$actual[$canonical]=[Collections.Generic.List[string]]::new()}
  $actual[$canonical].Add($raw)
 }
 return [ordered]@{generated=$commands.ToArray();actual=$actual}
}
function CompileArgv([string]$text){
 Need ($text-notmatch'[\x00-\x08\x0a-\x1f]') 'Invalid compile command'
 $result=[Collections.Generic.List[string]]::new();$i=0;$program=[Text.StringBuilder]::new();$quoted=$false
 while($i-lt$text.Length-and($text[$i]-eq' '-or$text[$i]-eq[char]9)){$i++}
 # argv[0] is a quoted pathname; subsequent arguments use CRT backslash rules.
 while($i-lt$text.Length-and($quoted-or($text[$i]-ne' '-and$text[$i]-ne[char]9))){
  if($text[$i]-eq'"'){$quoted=-not$quoted}else{[void]$program.Append($text[$i])};$i++
 }
 Need ($program.Length-gt0-and-not$quoted) 'Invalid compile executable quote';$result.Add($program.ToString())
 while($i-lt$text.Length){
  while($i-lt$text.Length-and($text[$i]-eq' '-or$text[$i]-eq[char]9)){$i++};if($i-eq$text.Length){break}
  $value=[Text.StringBuilder]::new();$quoted=$false
  while($i-lt$text.Length-and($quoted-or($text[$i]-ne' '-and$text[$i]-ne[char]9))){
   $slashes=0;while($i-lt$text.Length-and$text[$i]-eq'\'){$slashes++;$i++}
   if($i-lt$text.Length-and$text[$i]-eq'"'){
    [void]$value.Append(('\'*[int][Math]::Floor($slashes/2)))
    if($slashes%2){[void]$value.Append('"');$i++}
    elseif($quoted-and$i+1-lt$text.Length-and$text[$i+1]-eq'"'){[void]$value.Append('"');$i+=2}
    else{$quoted=-not$quoted;$i++}
   }else{
    [void]$value.Append(('\'*$slashes))
    if($i-eq$text.Length-or(-not$quoted-and($text[$i]-eq' '-or$text[$i]-eq[char]9))){break}
    [void]$value.Append($text[$i]);$i++
   }
  }
  Need (-not$quoted) 'Unterminated compile command quote';$result.Add($value.ToString())
 }
 return $result.ToArray()
}
function Read-CmakeManifestMode([string]$command,[string]$cmakeVersion){
 # This narrow classifier follows the pinned CMake 4.2.3 cmVSLink path.
 # Unknown versions, shell shapes, response files and manifest inputs retain
 # the conservative requirement for actual mt.exe evidence.
 if($cmakeVersion-cne'4.2.3'){return $null}
 $wrapper=[regex]::Match($command,'^(?:"(?<cmd>[^"\r\n]+)"|(?<cmd>[^ \t"\r\n]+))[ \t]+/C[ \t]+"cd \.[ \t]+&&[ \t]+(?<body>[^\r\n]+)[ \t]+&&[ \t]+cd \."$')
 if(-not$wrapper.Success){return $null}
 try {$commandArgs=@(CompileArgv $wrapper.Groups['body'].Value)}catch{return $null}
 if($commandArgs.Count-lt11-or$commandArgs[1]-cne'-E'-or$commandArgs[2]-cne'vs_link_exe'-or$commandArgs[3]-cnotmatch'^--msvc-ver=[0-9]{1,6}$'-or
    -not$commandArgs[4].StartsWith('--intdir=')-or$commandArgs[4].Length-le9-or-not$commandArgs[5].StartsWith('--rc=')-or
    -not$commandArgs[6].StartsWith('--mt=')-or$commandArgs[7]-cne'--manifests'-or$commandArgs[8]-cne'--'){return $null}
 $toolPaths=@($wrapper.Groups['cmd'].Value,$commandArgs[0],$commandArgs[5].Substring(5),$commandArgs[6].Substring(5),$commandArgs[9])
 if(@($toolPaths|Where-Object{$_-notmatch'^[A-Za-z]:[\\/]'-or$_-match'[<>|?*\x00-\x1f]'}).Count){return $null}
 $linkArgs=@($commandArgs[10..($commandArgs.Count-1)])
 if(@($linkArgs|Where-Object{$_.StartsWith('@')-or$_-in@('&&','&','|','||','>','<')}).Count){return $null}
 $incremental=@($linkArgs|Where-Object{$_-imatch'^[/-]INCREMENTAL(?::(?:YES|NO))?$'})
 if($incremental.Count-ne1){return $null}
 $version=[uint32]$commandArgs[3].Substring(11);$nonincremental=$incremental[0]-imatch'^[/-]INCREMENTAL:NO$'
 return [ordered]@{schema='soui.cmake-manifest-mode/v1';cmakeVersion=$cmakeVersion;msvcVersion=$version;
   nonIncremental=$nonincremental;requiresManifestTool=(-not$nonincremental-or$version-le1600);
   cmd=$toolPaths[0];cmake=$toolPaths[1];rc=$toolPaths[2];mt=$toolPaths[3];linker=$toolPaths[4]}
}

function Test-CrtOption([string[]]$arguments,[ValidateSet('MD','MDd','MT')][string]$expectedCRT){
 # The SDK keeps its existing MD policy. The exact Doctor target alone requests MT.
 # A caller must name its policy; a substring cannot select a CRT library.
 $runtime=@($arguments|Where-Object{$_-imatch'^[/-](?:md|mdd|mt|mtd)$'})
 if($expectedCRT-ieq'MT'){
  $overrides=@($arguments|Where-Object{$_-imatch'^(?:[/-]clang:)?[/-]+(?:Xclang=[/-]*)?fms-runtime-lib(?:=|$)' -or $_-imatch'^[/-]clang:[/-](?:md|mdd|mt|mtd)$'})
  return ($runtime.Count-eq1-and$runtime[0]-cmatch'^[/-]MT$'-and$overrides.Count-eq0)
 }
 return ($runtime.Count-eq1-and[String]::Equals($runtime[0].Substring(1),$expectedCRT,[StringComparison]::OrdinalIgnoreCase))
}
function Test-RuntimeDoctorCompileBinding([string]$Source,[string]$Object,[string]$DistributionRoot,[string]$BuildRoot,[ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration){
 $sourcePath=Norm (Full $Source $BuildRoot);$objectPath=Norm (Full $Object $BuildRoot)
 $mainSource=Norm (Full 'tools/runtime-doctor/main.cpp' $DistributionRoot)
 $profileSource=Norm (Full 'sdk/src/host/runtime_profile.cpp' $DistributionRoot)
 $targetRoot=(Norm (Full 'tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/' $BuildRoot)).TrimEnd('/')+'/'
 $pairs=@(
  @($mainSource,(Norm (Full ('tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/'+$Configuration+'/main.cpp.obj') $BuildRoot))),
  @($profileSource,(Norm (Full ('tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/'+$Configuration+'/__/__/sdk/src/host/runtime_profile.cpp.obj') $BuildRoot)))
 )
 if($objectPath.StartsWith($targetRoot,[StringComparison]::Ordinal)-or$sourcePath-ceq$mainSource){
  $matches=@($pairs|Where-Object{$_[0]-ceq$sourcePath-and$_[1]-ceq$objectPath})
  Need ($matches.Count-eq1) 'RuntimeDoctor exact source/object/configuration pair'
  return $true
 }
 return $false
}
function Test-RuntimeDoctorCompileOptions([string[]]$Arguments,[ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration){
 if(-not(Test-CrtOption -Arguments $Arguments -ExpectedCRT MT)){return $false}
 $configurationFlags=@($Arguments|Where-Object{$_-cmatch'^[/-]DCMAKE_INTDIR(?:=|$)'})
 $configurationPattern='^[/-]DCMAKE_INTDIR=(?:"'+[regex]::Escape($Configuration)+'"|'+[regex]::Escape($Configuration)+')$'
 if($configurationFlags.Count-ne1-or$configurationFlags[0]-cnotmatch$configurationPattern){return $false}
 foreach($macro in @('UNICODE','_UNICODE','NOMINMAX','WINVER','_WIN32_WINNT')){
  $macroFlags=@($Arguments|Where-Object{$_-cmatch('^[/-][DU]'+[regex]::Escape($macro)+'(?:=|$)')})
  $wanted='^[/-]D'+[regex]::Escape($macro)+$(if($macro-cin@('WINVER','_WIN32_WINNT')){'=0x0601$'}else{'$'})
  if($macroFlags.Count-eq0-or@($macroFlags|Where-Object{$_-cnotmatch$wanted}).Count-ne0){return $false}
 }
 return $true
}

function CompileNorm([string]$text,[bool]$actual=$false){
 $head=[regex]::Match($text.Trim(),'^(?:"([^"\r\n]+)"|([^ \t"\r\n]+))(?=[ \t]|$)')
 if(-not$head.Success){return $null}
 $program=$(if($head.Groups[1].Success){$head.Groups[1].Value}else{$head.Groups[2].Value})
 if($program-inotmatch'(?:^|[\\/])clang-cl\.exe$'){return $null}
 [string[]]$parsed=@(CompileArgv $text)
 Need (@($parsed|Where-Object{$_-match'^@|^[/-]clang:@'-or$_-cin@('&&','||','&','|')}).Count-eq0) 'Direct compile command without response file required'
 $dependency=@($parsed|Where-Object{$_-imatch'^[/-]showincludes$'})
 Need ($(if($actual){$dependency.Count-eq1-and$dependency[0]-ceq'/showIncludes'}else{$dependency.Count-eq0})) 'Exact actual/database compile dependency flag contract'
 $parsed[0]=ToolNorm $parsed[0]
 [string[]]$canonical=@($parsed|Where-Object{$_-cne'/showIncludes'})
 return (ConvertTo-Json -InputObject $canonical -Compress)
}
function CompileVerboseNorm([string]$raw){
 $progress=[regex]::Match($raw,'^\[([1-9][0-9]*)/([1-9][0-9]*)\] (.+)$')
 if(-not$progress.Success){return $null}
 $current=[uint64]::Parse($progress.Groups[1].Value);$total=[uint64]::Parse($progress.Groups[2].Value)
 Need ($current-le$total) 'Invalid Ninja progress prefix'
 return (CompileNorm $progress.Groups[3].Value $true)
}
function CompileVerboseRecords([string]$text){
 foreach($raw in ($text-split'\r?\n')){
  $canonical=CompileVerboseNorm $raw
  if($null-ne$canonical){[ordered]@{canonical=$canonical;raw=($raw-replace'^\[[1-9][0-9]*/[1-9][0-9]*\] ','')}}
 }
}
function DatabaseCommand([string]$text){
 [string[]]$arguments=@(CompileArgv $text)
 Need (@($arguments|Where-Object{$_-match'^@|^[/-]clang:@'-or$_-cin@('&&','||','&','|')}).Count-eq0) 'Direct database command without response file or shell required'
 $kind=$(if($arguments[0]-imatch'(?:^|[\\/])clang-cl\.exe$'){'CLANG'}elseif($arguments[0]-imatch'(?:^|[\\/])rc\.exe$'){'RC'}else{$null})
 Need ($null-ne$kind) 'Selected database command requires a fixed direct CLANG or RC compiler'
 return [ordered]@{kind=$kind;arguments=$arguments}
}
function ResourceDatabaseArgs($command,[string]$build){
 $parsed=DatabaseCommand $command.command;Need ($parsed.kind-ceq'RC') 'Direct RC database command required'
 [string[]]$arguments=$parsed.arguments
 Need ((Norm (Full $command.directory $build))-ceq(Norm $build)-and$command.file-match'^[A-Za-z]:[\\/]'-and$command.file.EndsWith('.rc',[StringComparison]::OrdinalIgnoreCase)) 'RC database cwd/source required'
 $output=Full $command.output $build
 Need ((Norm $output).StartsWith(((Norm $build).TrimEnd('/')+'/'),[StringComparison]::Ordinal)-and$output.EndsWith('.res',[StringComparison]::OrdinalIgnoreCase)) 'RC output must belong to the current build'
 $fo=@(for($i=1;$i-lt$arguments.Count;$i++){if($arguments[$i]-imatch'^[/-]fo'){$i}})
 Need ($fo.Count-eq1-and$arguments[$fo[0]]-ceq'/fo'-and$fo[0]+1-lt$arguments.Count-1-and(Norm (Full $arguments[$fo[0]+1] $build))-ceq(Norm $output)-and(Norm (Full $arguments[-1] $build))-ceq(Norm (Full $command.file $build))) 'Exact RC database source/output operands required'
 $arguments[0]=ToolNorm $arguments[0];return ,$arguments
}
function ResourceQuery([string]$text){
 $head=[regex]::Match($text.Trim(),'^(?:"([^"\r\n]+)"|([^ \t"\r\n]+))(?=[ \t]|$)')
 if(-not$head.Success){return $null}
 $program=$(if($head.Groups[1].Success){$head.Groups[1].Value}else{$head.Groups[2].Value})
 if($program-inotmatch'(?:^|[\\/])(?:rc|cmcldeps)\.exe$'){return $null}
 [string[]]$arguments=@(CompileArgv $text)
 Need (@($arguments|Where-Object{$_-match'^@'-or$_-cin@('&&','||','&','|')}).Count-eq0) 'Direct resource command without response file or shell required'
 $wrapped=$program-imatch'(?:^|[\\/])cmcldeps\.exe$'
 if($wrapped){
  # Fixed CMake RC wrapper: launcher, RC, source, depfile, output, prefix,
  # clang preprocessor, rc compiler; the remaining vector is the DB RC args.
  Need ($arguments.Count-ge9-and$arguments[1]-ceq'RC'-and$arguments[5]-ceq'Note: including file: '-and$arguments[6]-imatch'(?:^|[\\/])clang-cl\.exe$'-and$arguments[7]-imatch'(?:^|[\\/])rc\.exe$') 'Fixed cmcldeps RC wrapper slots required'
  [string[]]$direct=$arguments[7..($arguments.Count-1)]
 }else{[string[]]$direct=$arguments}
 return [ordered]@{kind=$(if($wrapped){'cmcldeps-RC'}else{'direct-RC'});arguments=$arguments;directArguments=$direct;raw=$text}
}
function ResourceQueryNorm($query){
 [string[]]$arguments=$query.arguments.Clone();$arguments[0]=ToolNorm $arguments[0]
 if($query.kind-ceq'cmcldeps-RC'){$arguments[6]=ToolNorm $arguments[6];$arguments[7]=ToolNorm $arguments[7]}
 return (ConvertTo-Json -InputObject $arguments -Compress)
}
function ResourceBindings([string]$build,$commands,$edges,$queries){
 $wanted=@{};foreach($command in $commands){
  [string[]]$arguments=ResourceDatabaseArgs $command $build;$key=Norm (Full $command.output $build)
  Need (-not$wanted.ContainsKey($key)) 'Unique RC database output required'
  $wanted[$key]=[ordered]@{command=$command;canonical=(ConvertTo-Json -InputObject $arguments -Compress)}
 }
 $seen=@{};$records=[Collections.Generic.List[object]]::new()
 foreach($query in $queries){
  [string[]]$direct=$query.directArguments.Clone();$direct[0]=ToolNorm $direct[0]
  $canonical=ConvertTo-Json -InputObject $direct -Compress
  $matches=@($wanted.Keys|Where-Object{$wanted[$_].canonical-ceq$canonical})
  Need ($matches.Count-eq1) 'Complete RC query argv must bind exactly one database record'
  $key=$matches[0];Need (-not$seen.ContainsKey($key)) 'Unique generated RC command required';$entry=$wanted[$key];$command=$entry.command
  $bound=@($edges|Where-Object{$_.rule-cmatch'^RC_COMPILER(?:_|$)'-and$_.outputs.Count-eq1-and(Norm (Full $_.outputs[0] $build))-ceq$key})
  Need ($bound.Count-eq1) 'RC query requires one matching resource edge';$edge=$bound[0]
  Need ($edge.inputs.Count-eq1-and$edge.implicitOutputs.Count-eq0-and$edge.validations.Count-eq0-and(Norm (Full $edge.inputs[0] $build))-ceq(Norm (Full $command.file $build))) 'Exact RC edge source/output required'
  if($query.kind-ceq'cmcldeps-RC'){
   Need ((Norm (Full $query.arguments[2] $build))-ceq(Norm (Full $command.file $build))-and(Norm (Full $query.arguments[4] $build))-ceq$key-and(Norm (Full $query.arguments[3] $build))-ceq($key+'.d')) 'Exact cmcldeps RC source/output/depfile slots required'
  }
  $seen[$key]=$true;$records.Add([ordered]@{command=$command;edge=$edge;query=$query;databaseCanonical=$canonical;canonical=(ResourceQueryNorm $query)})
 }
 $outside=@($wanted.Keys|Where-Object{-not$seen.ContainsKey($_)}|ForEach-Object{[ordered]@{source=$wanted[$_].command.file;output=$wanted[$_].command.output;scope='RC database declaration outside selected default ALL graph; not compiled proof'}})
 return [ordered]@{selected=$records.ToArray();outsideScope=$outside}
}
function ResourceVerboseRecords([string]$text){
 foreach($raw in ($text-split'\r?\n')){
  $progress=[regex]::Match($raw,'^\[([1-9][0-9]*)/([1-9][0-9]*)\] (.+)$');if(-not$progress.Success){continue}
  Need ([uint64]::Parse($progress.Groups[1].Value)-le[uint64]::Parse($progress.Groups[2].Value)) 'Invalid Ninja progress prefix'
  $query=ResourceQuery $progress.Groups[3].Value;if($null-ne$query){[ordered]@{query=$query;raw=$progress.Groups[3].Value}}
 }
}
function ResourceActual($binding,$records){
 $matches=@($records|Where-Object{(ResourceQueryNorm $_.query)-ceq$binding.canonical})
 Need ($matches.Count-le1) 'Resource command observed repeatedly';return $matches
}
function ResourceInput([string]$path,$resources){
 $key=Norm $path;Need ($resources.ContainsKey($key)) 'Selected binary resource input requires a complete RC database/query binding'
 return $resources[$key]
}
function ResourceWrapperIdentity([string]$observed){
 Need ($cfg.PSObject.Properties.Name-ccontains'resourceWrapperPin'-and$null-ne$cfg.resourceWrapperPin) 'Frozen selected CMake resource wrapper pin required'
 $pin=$cfg.resourceWrapperPin;[void](MatchPin $pin)
 Need ([IO.Path]::GetFileName($pin.path)-ieq'cmcldeps.exe'-and@($cfg.dependencyPins|Where-Object{(Norm $_.path)-ceq(Norm $pin.path)-and$_.bytes-eq$pin.bytes-and$_.sha256-ceq$pin.sha256}).Count-eq1) 'Resource wrapper must be in the whole frozen dependency envelope'
 $checked=LocalToolSpelling $observed;$key='CMCLDEPS|'+(Norm $observed)
 if(-not$toolIdentities.ContainsKey($key)){$receipt=Assert-ToolFileIdentity -ObservedPath $checked -ExpectedPin $pin;$toolIdentities[$key]=$receipt;$aliases[(Norm $observed)]=Norm $pin.path;$aliases[(Norm $checked)]=Norm $pin.path}
 return $toolIdentities[$key]
}
function AuthenticateResource($query){
 if($query.kind-ceq'cmcldeps-RC'){[void](ResourceWrapperIdentity $query.arguments[0]);[void](ToolIdentity $query.arguments[6] 'CLANG')}
 [void](ToolIdentity $query.directArguments[0] 'RC')
}
function AuthenticateClang([string]$text){
 $head=[regex]::Match($text.Trim(),'^(?:"([^"\r\n]+)"|([^ \t"\r\n]+))(?=[ \t]|$)');if(-not$head.Success){return}
 $program=$(if($head.Groups[1].Success){$head.Groups[1].Value}else{$head.Groups[2].Value})
 if($program-imatch'(?:^|[\\/])clang-cl\.exe$'){[void](ToolIdentity $program 'CLANG')}
}
$buildRaw=MatchPin $cfg.buildCapture.raw;$contextPin=MatchPin $cfg.buildCapture.context;$finishedPin=MatchPin $cfg.buildCapture.finished;$readyPin=MatchPin $cfg.buildCapture.ready
$eventsText=RawText $cfg.buildCapture.raw.path;Need ($eventsText.EndsWith("`n")) 'Complete original owned stream';$events=@($eventsText.TrimEnd([char]10).Split([char]10)|ForEach-Object{$_|ConvertFrom-Json})
$context=RawText $cfg.buildCapture.context.path|ConvertFrom-Json;$finished=RawText $cfg.buildCapture.finished.path|ConvertFrom-Json;$ready=RawText $cfg.buildCapture.ready.path|ConvertFrom-Json;$start=$events[0];$terminal=$events[-1]
Need ($ready.pid-eq$start.pid-and$ready.creationFileTime-ceq$start.creationFileTime) 'Actual published Build ready identity differs'
Need ($start.event-ceq'start'-and$terminal.event-ceq'terminal'-and@($events|Where-Object{$_.event-ceq'start'}).Count-eq1-and@($events|Where-Object{$_.event-ceq'terminal'}).Count-eq1-and@($events|Where-Object{$_.event-ceq'root-exit'}).Count-eq1-and$start.pid-eq$finished.pid-and$start.creationFileTime-ceq$finished.creationFileTime-and$start.creationFileTime-is[string]-and$start.creationFileTime-cmatch'^[1-9][0-9]{0,19}$'-and$start.jobAssignedBeforeResume-eq$true-and$start.breakawayAllowed-eq$false) 'Actual build process identity'
Need ($finished.passed-eq$true-and$finished.exitCode-eq0-and$finished.supervisorExitCode-eq0-and$finished.closureConfirmed-eq$true-and$null-eq$finished.signal-and$null-eq$finished.spawnError-and$null-eq$finished.reportError-and$finished.stopRequested-eq$false-and$finished.fallbackUsed-eq$false-and$terminal.reason-ceq'completed'-and$terminal.rootExited-eq$true-and$terminal.rootExitCode-eq0-and$terminal.passed-eq$true-and$terminal.activeBeforeCleanup-eq0-and$terminal.activeProcesses-eq0-and$terminal.terminatedOwnedJob-eq$false-and$terminal.limitTerminatedProcesses-eq0-and$terminal.closureConfirmed-eq$true-and$finished.reportSha256-ceq$buildRaw.sha256) 'Build must first close naturally; tail exceptions are not supported'
Need ($context.timeoutMs-eq$cfg.buildCapture.timeoutMs-and$context.drainMs-eq$cfg.buildCapture.drainMs-and$start.timeoutMs-eq$context.timeoutMs-and$start.drainMs-eq$context.drainMs-and(Norm $context.cwd)-ceq(Norm $root)-and$context.executableSha256-ceq$finished.executableSha256-and$context.runnerSha256-ceq$finished.runnerSha256) 'Original build budgets/tools/cwd'
$currentToolRows=[Collections.Generic.List[object]]::new();$currentRoles=@{}
foreach($row in $events){if($row.event-cne'owned-member'-or$null-eq$row.image){continue}
 $name=[IO.Path]::GetFileName($row.image).ToLowerInvariant();Need ($name-cnotin@('cl.exe','link.exe','lib.exe')) 'A foreign MSVC compiler/linker/librarian executed in this LLVM build'
 foreach($role in $roleNames){if($name-ceq[IO.Path]::GetFileName($profile.ToolPins.$role.path).ToLowerInvariant()){
  $receipt=ToolIdentity $row.image $role;$currentRoles[$role]=$true;$currentToolRows.Add([ordered]@{role=$role;pid=$row.pid;creationFileTime=$row.creationFileTime;image=$row.image;identity=$receipt})
 }}
}

# Original polling events and actualToolRows remain unchanged. This separate
# input is a projection of the complete native debug stream, validated by the
# current pinned JS collector before launch and again after this reader exits.
$independentRoles=@{};$independentToolRows=[Collections.Generic.List[object]]::new();$debugRows=@();$innerBuild=$null
if($debugMode){
 $binding=$cfg.debugCapture;$fast=$cfg.fastToolEvidence
 foreach($p in @($binding.input,$binding.raw,$binding.prepared,$binding.observer)){$evidencePins.Add((MatchPin $p))}
 $nativeInput=RawText $binding.input.path|ConvertFrom-Json
 $nativeSchema=$(if($consumerDebug){'soui.host64-build-debug-inputs/3'}else{'soui.host64-build-debug-inputs/2'})
 $rootRole=$(if($consumerDebug){'CMAKE'}else{'POWERSHELL'})
 Need ($nativeInput.schema-ceq$nativeSchema-and$nativeInput.profile-ceq$cfg.profile-and$nativeInput.sdkArchitecture-ceq$cfg.architecture-and$nativeInput.configuration-ceq$cfg.configuration-and(Norm $nativeInput.root)-ceq(Norm $root)-and$nativeInput.nonce-ceq$fast.nonce-and$fast.originalPollingUnmodified-eq$true-and$fast.actualDebugRawSha256-ceq$binding.raw.sha256-and$fast.actualDebugRawBytes-eq$binding.raw.bytes-and$fast.experimentalLifecycleObserved-eq$true) 'Actual independent fast-tool raw/input identity'
 if($consumerDebug){
  $expectedKind=$(if($installedMode){'acceptance'}else{'release'})
  $expectedArgs=@('--build',$build,'--config',$cfg.configuration,'--parallel','2','--verbose')
  Need ($nativeInput.consumerKind-ceq$expectedKind-and(Norm $nativeInput.buildDirectory)-ceq(Norm $build)-and(ConvertTo-Json -InputObject @($nativeInput.rootArguments) -Compress)-ceq(ConvertTo-Json -InputObject $expectedArgs -Compress)) 'Exact owned consumer CMake vector'
 }
 Need ((Norm $context.executable)-ceq(Norm $binding.observer.path)-and$context.executableSha256-ceq$binding.observer.sha256-and$context.runnerSha256-ceq$nativeInput.outerRunner.sha256-and$context.args.Count-eq8-and$context.args[0]-ceq'--input'-and(Norm $context.args[1])-ceq(Norm $binding.input.path)-and$context.args[2]-ceq'--input-sha256'-and$context.args[3]-ceq$binding.input.sha256-and$context.args[4]-ceq'--report'-and(Norm $context.args[5])-ceq(Norm $binding.raw.path)-and$context.args[6]-ceq'--nonce'-and$context.args[7]-ceq$nativeInput.nonce) 'Original outer context must remain the real observer'
 $debugText=RawText $binding.raw.path;Need ($debugText.EndsWith("`n")-and$debugText-notmatch'\r') 'Complete native debug stream'
 $debugRows=@($debugText.TrimEnd([char]10).Split([char]10)|ForEach-Object{$_|ConvertFrom-Json});Need ($debugRows.Count-gt1-and$debugRows.Count-le100000) 'Bounded complete debug records'
 for($i=0;$i-lt$debugRows.Count;$i++){$r=$debugRows[$i];Need ($r.schema-ceq'soui.host64-build-debug-addendum/2'-and$r.sequence-eq$i+1-and$r.nonce-ceq$nativeInput.nonce-and$r.inputSha256-ceq$binding.input.sha256) 'Native debug row sequence/nonce/input hash'}
 $debugStart=$debugRows[0];$debugTerminal=$debugRows[-1]
 Need ($debugStart.event-ceq'observer-start'-and$debugStart.observerPid-eq$start.pid-and$debugStart.observerCreationFileTime-ceq$start.creationFileTime-and$debugTerminal.event-ceq'terminal'-and$debugTerminal.passed-eq$true-and$debugTerminal.forced-eq$false-and$debugTerminal.rootExitCode-eq0-and$debugTerminal.activeProcesses-eq0-and$debugTerminal.limitTerminatedProcesses-eq0-and$debugTerminal.allMembersSignaled-eq$true-and$debugTerminal.allPausedQueriesClosed-eq$true-and$debugTerminal.allLoaderSequences-eq$true-and$debugTerminal.debugProtocolValid-eq$true-and$terminal.totalProcesses-eq$debugTerminal.historicalMembers+1) 'Independent complete inner history and exact natural outer membership'
 $creates=@($debugRows|Where-Object{$_.event-ceq'create-paused'});$queries=@($debugRows|Where-Object{$_.event-ceq'commandline-paused'});$exits=@($debugRows|Where-Object{$_.event-ceq'member-exit'})
 Need ($creates.Count-eq$debugTerminal.historicalMembers-and$exits.Count-eq$creates.Count-and$queries.Count-eq$debugTerminal.queryCount-and(ConvertTo-Json -InputObject $creates -Depth 20 -Compress)-ceq(ConvertTo-Json -InputObject $fast.created -Depth 20 -Compress)-and(ConvertTo-Json -InputObject $queries -Depth 20 -Compress)-ceq(ConvertTo-Json -InputObject $fast.queries -Depth 20 -Compress)) 'Projection cannot replace actual native frames'
 $roots=@($creates|Where-Object{$_.buildRoot-eq$true});Need ($roots.Count-eq1-and$roots[0].role-ceq$rootRole-and$roots[0].pid-eq$fast.innerBuild.pid-and$roots[0].creationFileTime-ceq$fast.innerBuild.creationFileTime-and$roots[0].pid-eq$debugTerminal.rootPid-and$roots[0].creationFileTime-ceq$debugTerminal.rootCreationFileTime) 'Unique real inner Build CREATE identity'
 $rootQueries=@($queries|Where-Object{$_.buildRoot-eq$true});$rootImages=@($nativeInput.images|Where-Object{$_.role-ceq$rootRole})
 Need ($rootQueries.Count-eq1-and$rootImages.Count-eq1-and$rootQueries[0].pid-eq$roots[0].pid-and$rootQueries[0].creationFileTime-ceq$roots[0].creationFileTime-and$rootQueries[0].role-ceq$rootRole-and(ConvertTo-Json -InputObject @($rootQueries[0].wholeArgv) -Compress)-ceq(ConvertTo-Json -InputObject (@($rootImages[0].path)+@($nativeInput.rootArguments)) -Compress)) 'Actual complete inner root argv and identity'
 $innerBuild=$roots[0]
 foreach($r in $creates){
  if($roleNames-cnotcontains$r.role){continue};$receipt=ToolIdentity $r.imageFile.finalPath $r.role
  Need ($r.imageFile.sha256-ceq$profile.ToolPins.($r.role).sha256-and[uint64]::Parse($r.imageFile.bytes)-eq$profile.ToolPins.($r.role).bytes-and$r.imageFile.machine-eq34404) 'Independent current fixed-role physical image'
  $independentRoles[$r.role]=$true;$independentToolRows.Add([ordered]@{role=$r.role;pid=$r.pid;creationFileTime=$r.creationFileTime;image=$r.imageFile.finalPath;identity=$receipt;source='independent-debug-create'})
 }
}

$cache=@{};foreach($line in (RawText ($build+'/CMakeCache.txt')).Split([char]10)){
 if($line-match'^([^#/:][^:]*):[^=]+=(.*)$'){Need (-not$cache.ContainsKey($Matches[1])) 'Duplicate actual CMake cache key';$cache[$Matches[1]]=$Matches[2].TrimEnd([char]13)}
}
foreach($pair in @(@('CMAKE_GENERATOR','Ninja Multi-Config'),@('CMAKE_CONFIGURATION_TYPES','Debug;Release;RelWithDebInfo'),@('CMAKE_SYSTEM_VERSION','10.0.19041.0'),@('CMAKE_EXPORT_COMPILE_COMMANDS','ON'))){Need ($cache.ContainsKey($pair[0])-and$cache[$pair[0]]-ceq$pair[1]) ('Actual cache differs: '+$pair[0])}
if($installedMode){Need ((Norm $cache.CMAKE_HOME_DIRECTORY)-ceq(Norm $installedPaths.source)-and(Norm $cache.CMAKE_CACHEFILE_DIR)-ceq(Norm $build)) 'Actual installed consumer cache source/build ownership'}elseif($releaseMode){Need ((Norm $cache.CMAKE_HOME_DIRECTORY)-ceq(Norm $installedPaths.source)-and(Norm $cache.CMAKE_CACHEFILE_DIR)-ceq(Norm $build)) 'Actual release consumer cache source/build ownership'}else{
Need ((Norm $cache.CMAKE_HOME_DIRECTORY)-ceq(Norm $(if($cfg.sdkOnly){Join-Path $root 'sdk'}else{$root}))-and(Norm $cache.CMAKE_CACHEFILE_DIR)-ceq(Norm $build)) 'Actual cache source/build ownership'
}
foreach($definition in $profile.CMakeDefinitions.PSObject.Properties){Need ($cache.ContainsKey($definition.Name)-and$cache[$definition.Name]-ceq[string]$definition.Value) ('Actual profile definition differs: '+$definition.Name)}
foreach($pair in @(@('CMAKE_C_COMPILER','CLANG'),@('CMAKE_CXX_COMPILER','CLANG'),@('CMAKE_LINKER','LINKER'),@('CMAKE_LINKER_LLD','LINKER'),@('CMAKE_AR','ARCHIVER'),@('CMAKE_RC_COMPILER','RC'),@('CMAKE_MT','MT'),@('CMAKE_MAKE_PROGRAM','NINJA'))){if($installedMode-and$pair[0]-ceq'CMAKE_C_COMPILER'){continue};if($releaseMode-and$pair[0]-ceq'CMAKE_C_COMPILER'){continue};Need ($cache.ContainsKey($pair[0])) ('Actual tool slot missing: '+$pair[0]);[void](ToolIdentity $cache[$pair[0]] $pair[1])}
$configurationFiles=@(Get-ChildItem -LiteralPath ($build+'/CMakeFiles') -Directory|Where-Object{$_.Name-cmatch'^[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?$'})
Need ($configurationFiles.Count-eq1) 'Unique actual CMake version directory';$compilerFacts=[Collections.Generic.List[object]]::new();$expectedMachine=$(if($cfg.architecture-ceq'x64'){34404}else{332});$expectedWidth=$(if($cfg.architecture-ceq'x64'){8}else{4});$archId=$(if($cfg.architecture-ceq'x64'){'x64'}else{'X86'})
foreach($language in $(if($installedMode){@('CXX')}elseif($releaseMode){@('CXX')}else{@('C','CXX')})){
 $p=$configurationFiles[0].FullName+'/CMake'+$language+'Compiler.cmake';$text=RawText $p;$values=@{}
 foreach($line in $text.Split([char]10)){if($line-match'^set\((CMAKE_[A-Z0-9_]+) "([^"]*)"\)\r?$'){Need (-not$values.ContainsKey($Matches[1])) 'Duplicate actual compiler fact';$values[$Matches[1]]=$Matches[2]}}
 foreach($pair in @(@('COMPILER_ID','Clang'),@('COMPILER_VERSION','22.1.3'),@('COMPILER_FRONTEND_VARIANT','MSVC'),@('SIMULATE_ID','MSVC'),@('SIMULATE_VERSION','19.29'),@('COMPILER_ARCHITECTURE_ID',$archId),@('SIZEOF_DATA_PTR',[string]$expectedWidth))){Need ($values.ContainsKey('CMAKE_'+$language+'_'+$pair[0])-and$values['CMAKE_'+$language+'_'+$pair[0]]-ceq$pair[1]) ('Actual enabled language fact differs: '+$language+'.'+$pair[0])}
 # CMake compiler records persist the ABI observations, but their official
 # template does not persist COMPILER_TARGET. Bind that cache value plus every
 # selected actual compile command, rather than inventing a compiler-file key.
 Need ($cache.ContainsKey('CMAKE_'+$language+'_COMPILER_TARGET')-and$cache['CMAKE_'+$language+'_COMPILER_TARGET']-ceq$profile.Target-and$text.Contains(('set(CMAKE_'+$language+'_COMPILER_LOADED 1)'))-and$text.Contains(('set(CMAKE_'+$language+'_COMPILER_WORKS TRUE)'))) 'Actual loaded/working compiler and selected target differ'
 [void](ToolIdentity $values['CMAKE_'+$language+'_COMPILER'] 'CLANG');$compilerFacts.Add([ordered]@{language=$language;pointerBytes=$expectedWidth;target=$profile.Target;compilerId='Clang';frontend='MSVC';version='22.1.3';file=(ObsPin $p)})
}

$allowed=@{};foreach($pin in $cfg.dependencyPins){$key=Norm $pin.path;Need (-not$allowed.ContainsKey($key)) 'Duplicate frozen dependency path';$allowed[$key]=$pin}
$derivedDependencies=[Collections.Generic.List[object]]::new()
if($cfg.withWebView2){
 # New optional package/header outputs are derived from authenticated source
 # inputs. They are explicitly separate from inputs frozen before Build.
 $descriptorPath=Join-Path $root 'sdk/renderers/webview2/dependency.json'
 Need ($allowed.ContainsKey((Norm $descriptorPath))) 'Frozen WebView2 descriptor required'
 [void](MatchPin $allowed[(Norm $descriptorPath)]);$descriptor=RawText $descriptorPath|ConvertFrom-Json
 Need ($descriptor.schema-ceq'soui.optional-dependency/1'-and$descriptor.id-ceq'Microsoft.Web.WebView2'-and$descriptor.version-cmatch'^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'-and$descriptor.sha256-cmatch'^[a-f0-9]{64}$') 'Exact pinned optional dependency descriptor'
 $cacheRoot=Join-Path $root ('out/webview2-deps/'+$descriptor.version)
 $archivePath=Join-Path $cacheRoot ('microsoft.web.webview2.'+$descriptor.version+'.nupkg')
 $archivePin=ObsPin $archivePath;Need ($archivePin.sha256-ceq$descriptor.sha256) 'Optional dependency archive does not match frozen source'
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=[IO.Compression.ZipFile]::OpenRead($archivePath)
 try{foreach($relative in @('build/native/include/WebView2.h','build/native/include/WebView2EnvironmentOptions.h',('build/native/'+$cfg.architecture+'/WebView2LoaderStatic.lib'),'LICENSE.txt','NOTICE.txt')){
  $matches=@($zip.Entries|Where-Object{$_.FullName-ceq$relative});Need ($matches.Count-eq1) 'Unique authenticated optional dependency member'
  $entry=$matches[0];$stream=$entry.Open();$hash=[Security.Cryptography.SHA256]::Create()
  try{$expected=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$hash.Dispose()}
  $pin=ObsPin (Join-Path $cacheRoot ('package/'+$relative));Need ($pin.bytes-eq$entry.Length-and$pin.sha256-ceq$expected) 'Optional dependency extracted member changed'
  $allowed[(Norm $pin.path)]=$pin;$derivedDependencies.Add([ordered]@{pin=$pin;kind='authenticated archive entry';archive=$archivePin;entry=$relative})
 }}finally{$zip.Dispose()};Need ((ObsPin $archivePath).sha256-ceq$archivePin.sha256) 'Optional archive changed while checking members'
 $templatePath=Join-Path $root 'sdk/renderers/webview2/webview2_preload.h.in';$scriptPath=Join-Path $root 'sdk/renderers/webview2/native-preload.js'
 foreach($p in @($templatePath,$scriptPath)){Need ($allowed.ContainsKey((Norm $p))) 'Frozen preload source required';[void](MatchPin $allowed[(Norm $p)])}
 $expected=(RawText $templatePath).Replace('@SOUI_WEBVIEW2_PRELOAD@',(RawText $scriptPath)).Replace("`r`n","`n")
 $generated=Join-Path $build ($(if($cfg.sdkOnly){''}else{'sdk/'})+'renderers/webview2/webview2_preload.h')
 Need ((RawText $generated).Replace("`r`n","`n")-ceq$expected) 'Generated preload header differs from frozen template/script'
 $pin=ObsPin $generated;$allowed[(Norm $pin.path)]=$pin;$derivedDependencies.Add([ordered]@{pin=$pin;kind='configure_file frozen template and script';template=$allowed[(Norm $templatePath)];script=$allowed[(Norm $scriptPath)];newlineNormalizationOnly=$true})
}
$dependencyPins=@{}
function DependencyPin([string]$value){$key=Norm $value;Need ($allowed.ContainsKey($key)) ('Actual dependency was not frozen before Build: '+$value);if($dependencyPins.ContainsKey($key)){return $dependencyPins[$key]};$pin=MatchPin $allowed[$key];$dependencyPins[$key]=$pin;return $pin}
foreach($pin in $cfg.dependencyPins){[void](MatchPin $pin)}
$allCommands=@((RawText ($build+'/compile_commands.json'))|ConvertFrom-Json);Need ($allCommands.Count-gt0) 'Actual compile database is empty'
$selectedCommands=@($allCommands|Where-Object{(Norm $_.output)-match('/'+$cfg.configuration.ToLowerInvariant()+'/')})
Need ($selectedCommands.Count-gt0) 'No compile database objects for selected configuration'
$nativeText=RawText $cfg.nativeLog.path;[void](MatchPin $cfg.nativeLog);$verbose=[string[]]@($nativeText-split'\r?\n'|ForEach-Object{$_-replace'^\[[0-9]+/[0-9]+\] ',''})
$impl=$build+'/CMakeFiles/impl-'+$cfg.configuration+'.ninja';$common=$build+'/CMakeFiles/common.ninja';$rules=$build+'/CMakeFiles/rules.ninja'
$ninjaScopePins=[ordered]@{ninja=(ObsPin $impl);common=(ObsPin $common);rules=(ObsPin $rules)}
$ninjaWorkdirBinding=NinjaWorkdirScope (RawText $impl) (RawText $common) (RawText $rules) $build
foreach($pin in $ninjaScopePins.Values){[void](MatchPin $pin)}
$edges=@(NinjaBuildEdges (RawText $impl) $build $ninjaWorkdirBinding.value);$queryFile=Join-Path $folder 'ninja-commands.txt';$depsFile=Join-Path $folder 'ninja-dependencies.txt'
foreach($output in @($queryFile,$depsFile)){Need (-not(Test-Path -LiteralPath $output)) 'Fresh actual Ninja readout required'}
Push-Location $build
try{
 & $profile.ToolPins.NINJA.path -f ('build-'+$cfg.configuration+'.ninja') -t commands *> $queryFile;Need ($LASTEXITCODE-eq0) 'Actual Ninja commands query failed'
 & $profile.ToolPins.NINJA.path -f ('build-'+$cfg.configuration+'.ninja') -t deps *> $depsFile;Need ($LASTEXITCODE-eq0) 'Actual Ninja dependency query failed'
}finally{Pop-Location}
$clangCommands=@();$rcCommands=@();foreach($command in $selectedCommands){$direct=DatabaseCommand $command.command;[void](ToolIdentity $direct.arguments[0] $direct.kind);if($direct.kind-ceq'CLANG'){$clangCommands+=,$command}else{$rcCommands+=,$command}}
$generated=[string[]]@((RawText $queryFile)-split'\r?\n'|Where-Object{$_-cne''});$resourceQueries=@();foreach($text in $generated){$query=ResourceQuery $text;if($null-ne$query){AuthenticateResource $query;$resourceQueries+=,$query}else{AuthenticateClang $text}}
$resourceVerbose=@(ResourceVerboseRecords $nativeText);foreach($entry in $resourceVerbose){AuthenticateResource $entry.query}
foreach($raw in ($nativeText-split'\r?\n')){$progress=[regex]::Match($raw,'^\[([1-9][0-9]*)/([1-9][0-9]*)\] (.+)$');if($progress.Success){AuthenticateClang $progress.Groups[3].Value}}
$normalizedGenerated=@($generated|ForEach-Object{CompileNorm $_ $true});$normalizedVerbose=@(CompileVerboseRecords $nativeText)
$graphCommands=@();$outsideGraph=@();foreach($command in $clangCommands){$needle=CompileNorm $command.command;Need ($null-ne$needle) 'Selected clang database record must be a direct clang command';$matches=@($normalizedGenerated|Where-Object{$_-ceq$needle});Need ($matches.Count-le1) 'Ambiguous generated compile command';if($matches.Count){$graphCommands+=,$command}else{$outsideGraph+=,[ordered]@{source=$command.file;output=$command.output;scope='compile database declaration outside selected default ALL graph; not compiled proof'}}}
$resourceBindings=ResourceBindings $build $rcCommands $edges $resourceQueries;$outsideGraph+=@($resourceBindings.outsideScope)
Need ($graphCommands.Count-gt0) 'No default graph compiler commands'
$selectedObjects=@{};foreach($command in $graphCommands){$selectedObjects[(Norm (Full $command.output $build))]=$true}
$graphEdges=@($edges|Where-Object{$_.rule-cmatch'^(C|CXX)_COMPILER(?:_|$)'-and$_.outputs.Count-eq1-and$selectedObjects.ContainsKey((Norm (Full $_.outputs[0] $build)))})
$bindings=@(CompileBindings $build $graphCommands $graphEdges);$deps=DependencyBindings $build $graphCommands (RawText $depsFile)
# Only validated selected compiler/Ninja dependency bindings may select a role.
$readGeneratedSource={param($value)[void](MatchPin $allowed[(Norm $value)]);return ,([IO.File]::ReadAllBytes($value))}
foreach($derived in @(GeneratedTestDependencies $root $build $cfg.configuration $allowed $deps.selected $readGeneratedSource $bindings)){
 $actual=ObsPin $derived.pin.path;$actualBytes=[IO.File]::ReadAllBytes($derived.pin.path)
 [void](GeneratedTestActual $derived $actual $actualBytes)
 [void](MatchPin $derived.source);foreach($writer in $derived.writerPins){[void](MatchPin $writer)}
 [void]$derived.Remove('expectedBytes');$allowed[(Norm $derived.pin.path)]=$actual;$derivedDependencies.Add($derived)
}

$previous=$null;if($null-ne$cfg.previousObservation){[void](MatchPin $cfg.previousObservation);$previous=RawText $cfg.previousObservation.path|ConvertFrom-Json;Need ($previous.schema-cin@('soui.standard-llvm-build-observation/v1','soui.standard-llvm-build-observation/v2')-and$previous.compilerVerified-eq$true-and$previous.architecture-ceq$cfg.architecture-and$previous.configuration-ceq$cfg.configuration-and$previous.inputIdentity-ceq$cfg.inputIdentity-and$previous.bridgeIdentity-ceq$cfg.bridgeIdentity) 'Prior observation scope/inputs differ'}
$compileRecords=[Collections.Generic.List[object]]::new();$objects=@{};$neededRoles=@{NINJA=$true};$currentCompileCount=0
foreach($command in $graphCommands){
 $source=Full $command.file $build;$sourcePin=DependencyPin $source;$object=Full $command.output $build;$objectPin=ObsPin $object;$bytes=[IO.File]::ReadAllBytes($object);$machine=CoffMachine $bytes 0 $bytes.Length;Need ($machine-eq$expectedMachine) 'Actual compiler object architecture'
 $needle=CompileNorm $command.command;[string[]]$flagArgs=@(CompileArgv $command.command);$flagArgs[0]=ToolNorm $flagArgs[0];$flags=Norm ($flagArgs-join' ');Need ($flags.Contains((Norm $profile.ToolPins.CLANG.path))-and$flags.Contains('--no-default-config')-and$flags.Contains('--target='+$profile.Target)-and$flags-match'(?<!\S)/x(?!\S)'-and$flags.Contains('-fms-compatibility-version=19.29.30159')-and$flags-notmatch'(?<!\S)/gl(?!\S)') 'Actual compiler profile command differs'
 foreach($include in $profile.IncludeDirectories){Need ($flags.Contains((Norm $include))) 'Actual compiler include root missing'}
 $isSdk=$source.StartsWith((Join-Path $root 'sdk/src/').Replace('\','/'),[StringComparison]::OrdinalIgnoreCase)-or(Norm $source).StartsWith((Norm (Join-Path $root 'sdk/src/')),[StringComparison]::Ordinal)
 $isRuntimeDoctor=Test-RuntimeDoctorCompileBinding -Source $source -Object $object -DistributionRoot $root -BuildRoot $build -Configuration $cfg.configuration
 if($isRuntimeDoctor){Need (Test-RuntimeDoctorCompileOptions -Arguments $flagArgs -Configuration $cfg.configuration) 'RuntimeDoctor static CRT/Unicode Win7 macro contract'}
 elseif($isSdk){Need ((Test-CrtOption -Arguments $flagArgs -ExpectedCRT MD)-and$flags-match'(?<!\S)/u_debug(?!\S)'-and$flags-match'(?<!\S)[/-]dwinver=0x0601(?!\S)'-and$flags-match'(?<!\S)[/-]d_win32_winnt=0x0601(?!\S)') 'SDK target CRT/Unicode OS macro contract'}
 if($installedMode){Need ((Test-CrtOption -Arguments $flagArgs -ExpectedCRT MD)-and$flags-match'(?<!\S)/u_debug(?!\S)') 'Installed acceptance target retains its actual MD/U_DEBUG contract'}
 if($releaseMode){Need ((Test-CrtOption -Arguments $flagArgs -ExpectedCRT MD)-and$flags-match'(?<!\S)/u_debug(?!\S)') 'Installed release target retains its actual MD/U_DEBUG contract'}
 $dependency=@($deps.selected|Where-Object{(Norm $_.object)-ceq(Norm $object)})[0];$headers=@();foreach($header in $dependency.headers){$headers+=,(DependencyPin $header.resolved)}
 $actual=@($normalizedVerbose|Where-Object{$_.canonical-ceq$needle}|ForEach-Object{$_.raw});Need ($actual.Count-le1) 'Compiler command was observed repeatedly'
 $reuse=$null;if(-not$actual.Count){
  Need ($null-ne$previous) ('No actual compile command or authenticated prior observation: '+$source)
  $old=@($previous.compileRecords|Where-Object{(Norm $_.object.path)-ceq(Norm $object)})
  Need ($old.Count-eq1-and$old[0].normalizedCommand-ceq$needle-and$old[0].source.sha256-ceq$sourcePin.sha256-and$old[0].source.bytes-eq$sourcePin.bytes-and$old[0].object.sha256-ceq$objectPin.sha256-and$old[0].object.bytes-eq$objectPin.bytes) 'Incremental object/source/command provenance differs'
  Need (SameSet ([string[]]@($headers|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256})) ([string[]]@($old[0].headers|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256}))) 'Incremental header provenance differs'
  $reuse=$cfg.previousObservation
 }else{$currentCompileCount++;$neededRoles.CLANG=$true}
 $record=[ordered]@{source=$sourcePin;object=$objectPin;machine=$machine;databaseRaw=$command.command;normalizedCommand=$needle;actualVerbose=$(if($actual.Count){$actual[0]}else{$null});reusedFrom=$reuse;headers=$headers}
 $compileRecords.Add($record);$objects[(Norm $object)]=$record
}
if($installedMode){
 Need ($graphCommands.Count-eq1-and$bindings.Count-eq1-and(Norm $graphCommands[0].file)-ceq(Norm (Join-Path $installedPaths.source 'main.cpp'))-and$bindings[0].rule-cmatch'^CXX_COMPILER(?:_|$)') 'Exact current installed ordinary CXX consumer default graph'
}elseif($releaseMode){
 Need ($graphCommands.Count-eq1-and$bindings.Count-eq1-and(Norm $graphCommands[0].file)-ceq(Norm (Join-Path $installedPaths.source 'main.cpp'))-and$bindings[0].rule-cmatch'^CXX_COMPILER(?:_|$)') 'Exact current installed release CXX consumer default graph'
}else{
$normalSdkCommands=@(NormalSdkSourceCommands $root $build $cfg.configuration ([bool]$cfg.sdkOnly) $cfg.dependencyPins $graphCommands)
}

$resourceRecords=[Collections.Generic.List[object]]::new();$resources=@{}
foreach($binding in $resourceBindings.selected){
 $command=$binding.command;$output=Full $command.output $build;$source=Full $command.file $build
 $sourcePin=DependencyPin $source;$outputPin=ObsPin $output;$needle=$binding.canonical
 $actual=@(ResourceActual $binding $resourceVerbose);$reuse=$null
 if($actual.Count){$neededRoles.RC=$true}else{
  Need ($null-ne$previous) 'Resource output lacks current command/prior natural proof'
  $old=@($previous.resourceRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $output)})
   Need ($old.Count-eq1-and$old[0].normalizedCommand-ceq$needle-and$old[0].databaseNormalizedCommand-ceq$binding.databaseCanonical-and$old[0].source.sha256-ceq$sourcePin.sha256-and$old[0].source.bytes-eq$sourcePin.bytes-and$old[0].output.sha256-ceq$outputPin.sha256-and$old[0].output.bytes-eq$outputPin.bytes) 'Incremental resource proof differs'
  # RC does not publish Ninja deps. Require every pre-frozen source/tool input
  # unchanged for reuse instead of claiming a reconstructed header-read trace.
  Need (SameSet ([string[]]@($cfg.dependencyPins|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256})) ([string[]]@($previous.frozenDependencyPins|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256}))) 'Incremental resource dependency inventory differs'
  $reuse=$cfg.previousObservation
 }
  $record=[ordered]@{source=$sourcePin;output=$outputPin;databaseRaw=$command.command;databaseNormalizedCommand=$binding.databaseCanonical;normalizedCommand=$needle;generatedCommand=$binding.query.raw;actualVerbose=$(if($actual.Count){$actual[0].raw}else{$null});reusedFrom=$reuse;dependencyReadTraceAvailable=$false}
 $resourceRecords.Add($record);$resources[(Norm $output)]=$outputPin
}
$binaryCommands=BinaryCommandIndex $generated $verbose
$binaryRecords=[Collections.Generic.List[object]]::new();$requiredLibraryKeys=@{};$currentBinaryCount=0
foreach($edge in $edges){
 if($edge.rule-cnotmatch'^(C|CXX)_(STATIC_LIBRARY|EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER(?:_|$)'){continue}
 Need ($edge.outputs.Count-eq1) 'One archive/link output per selected edge';$output=Full $edge.outputs[0] $build
 if(-not(Test-Path -LiteralPath $output)){continue} # Declared non-ALL targets are recorded above, never called built.
 $role=$(if($edge.rule-match'_STATIC_LIBRARY_'){'ARCHIVER'}else{'LINKER'});$needleToken=Norm $profile.ToolPins.$role.path
 $relativeOutput=Norm $edge.outputs[0];$absoluteOutput=Norm $output
 $candidates=@($binaryCommands.generated|Where-Object{$_.search.Contains($needleToken)-and($_.search.Contains($relativeOutput)-or$_.search.Contains($absoluteOutput))})
 if(-not$candidates.Count){continue};Need ($candidates.Count-eq1) 'One generated archive/link command for output';$needle=$candidates[0].canonical
 Need ($needle-notmatch'(?<!\S)/(gl|ltcg)(?::[^\s]+)?(?!\S)') 'No mixed MSVC GL/LTCG in LLVM profile'
 if($role-ceq'LINKER'){foreach($directory in $profile.LibraryDirectories){Need ((Norm $needle).Contains((Norm $directory))) 'Selected CRT/SDK linker search root absent'}}
 $actual=@();if($binaryCommands.actual.ContainsKey($needle)){$actual=$binaryCommands.actual[$needle].ToArray()};Need ($actual.Count-le1) 'Repeated archive/link command';$outputPin=ObsPin $output;$reuse=$null
 $inputObjects=@();$inputLibraries=@();$inputResources=@();foreach($relative in @($edge.inputs)+@($edge.implicitInputs)){
  $p=Full $relative $build;$key=Norm $p
  if($objects.ContainsKey($key)){$inputObjects+=,$objects[$key].object}
  elseif($p.EndsWith('.res',[StringComparison]::OrdinalIgnoreCase)){$inputResources+=,(ResourceInput $p $resources)}
  elseif($p.EndsWith('.lib',[StringComparison]::OrdinalIgnoreCase)){
   if($key.StartsWith(((Norm $build).TrimEnd('/')+'/'),[StringComparison]::Ordinal)){$inputLibraries+=,(ObsPin $p);$requiredLibraryKeys[$key]=$true}else{$inputLibraries+=,(DependencyPin $p)}
  }
 }
 if(-not$actual.Count){
  Need ($null-ne$previous) ('No actual link/archive command or authenticated prior output: '+$output)
  $old=@($previous.binaryRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $output)})
  Need ($old.Count-eq1-and$old[0].normalizedCommand-ceq$needle-and$old[0].output.sha256-ceq$outputPin.sha256-and$old[0].output.bytes-eq$outputPin.bytes-and(SameSet ([string[]]@($inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})))-and(SameSet ([string[]]@($inputLibraries|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputLibraries|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})))-and(SameSet ([string[]]@($inputResources|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputResources|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})))) 'Incremental link/archive inputs/output provenance differs';$reuse=$cfg.previousObservation
 }else{
  $currentBinaryCount++;$neededRoles[$role]=$true
  if((Norm $needle).Contains((Norm $profile.ToolPins.MT.path))){
   $requiresManifestTool=$true
   if($consumerDebug-and$role-ceq'LINKER'){
    $mode=Read-CmakeManifestMode $candidates[0].raw $configurationFiles[0].Name
    if($null-ne$mode-and$mode.requiresManifestTool-eq$false){
     $cmakePins=@($nativeInput.images|Where-Object{$_.role-ceq'CMAKE'});Need ($cmakePins.Count-eq1) 'Unique native CMake image required for manifest mode'
     [void](MatchPin $cmakePins[0]);[void](Assert-ToolFileIdentity -ObservedPath $mode.cmake -ExpectedPin $cmakePins[0])
     Need ([Diagnostics.FileVersionInfo]::GetVersionInfo($cmakePins[0].path).ProductVersion-ceq$mode.cmakeVersion) 'Actual pinned CMake version must match the recognized manifest implementation'
     foreach($pair in @(@($mode.cmd,'CMD'),@($mode.rc,'RC'),@($mode.mt,'MT'),@($mode.linker,'LINKER'))){[void](ToolIdentity $pair[0] $pair[1])}
     # This is a configured-tool receipt, never a fabricated MT process row.
     $requiresManifestTool=$false
    }
   }
   if($requiresManifestTool){$neededRoles.MT=$true}
  }
 }
 $members=$null;$machine=$expectedMachine
 if($role-ceq'ARCHIVER'){
  Need ($inputObjects.Count-gt0) 'Built archive has no current object inputs';$archive=ArchiveMembers $output;$members=$archive.members
  Need ($members.Count-eq$inputObjects.Count-and@($members|Where-Object{$_.machine-ne$expectedMachine}).Count-eq0-and(SameSet ([string[]]@($members|ForEach-Object{$_.sha256+'|'+$_.bytes})) ([string[]]@($inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})))) 'Archive members must equal actual object bodies exactly'
 }else{$pe=PeMachine $output;$machine=$pe.machine;Need ($machine-eq$expectedMachine) 'Actual linked PE target architecture'}
 $binaryRecords.Add([ordered]@{output=$outputPin;role=$role;machine=$machine;edge=$edge;generatedCommand=$candidates[0].raw;normalizedCommand=$needle;actualVerbose=$(if($actual.Count){$actual[0]}else{$null});reusedFrom=$reuse;inputObjects=$inputObjects;inputLibraries=$inputLibraries;inputResources=$inputResources;archiveMembers=$members})
}
Need ($binaryRecords.Count-gt0) 'No actual SDK archive/application link proof'
if($installedMode){
 Need ($binaryRecords.Count-eq1-and$binaryRecords[0].role-ceq'LINKER'-and(Norm $binaryRecords[0].output.path)-ceq(Norm $installedPaths.executable)-and@($binaryRecords[0].inputLibraries|Where-Object{InstalledPinSame $_ $installedBinding.installedSdkArchive}).Count-eq1) 'Actual acceptance EXE must link the exact independently accepted installed SDK archive'
}elseif($releaseMode){
 Need ($binaryRecords.Count-eq1-and$binaryRecords[0].role-ceq'LINKER'-and(Norm $binaryRecords[0].output.path)-ceq(Norm $installedPaths.executable)-and@($binaryRecords[0].inputLibraries|Where-Object{InstalledPinSame $_ $installedBinding.installedSdkArchive}).Count-eq1) 'Actual release EXE must link the exact independently accepted installed SDK archive'
}else{
$sdkArchive=Join-Path $build ($(if($cfg.sdkOnly){''}else{'sdk/'})+$cfg.configuration+'/soui_sdk.lib')
$normalSdkArchive=@($binaryRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $sdkArchive)-and$_.role-ceq'ARCHIVER'})
Need ($normalSdkArchive.Count-eq1) 'Actual normal SDK archive missing from proof'
Need (SameSet ([string[]]@($normalSdkCommands|ForEach-Object{Norm (Full $_.output $build)})) ([string[]]@($normalSdkArchive[0].inputObjects|ForEach-Object{Norm $_.path}))) 'Production SDK archive inputs must exactly equal all frozen-source production objects'
}
foreach($key in $requiredLibraryKeys.Keys){Need (@($binaryRecords|Where-Object{(Norm $_.output.path)-ceq$key}).Count-eq1) 'Generated library dependency lacks an actual archive binding'}
foreach($role in $neededRoles.Keys){Need ($currentRoles.ContainsKey($role)-or($debugMode-and$independentRoles.ContainsKey($role))) ('Actual owned or independent debug tool process was not observed: '+$role)}
. (Join-Path $root 'tools/internal/Capture-CMakeConfigureLog.ps1')
$configureLogCapture=Copy-SouiCMakeConfigureLog -BuildDirectory $build -EvidenceDirectory $folder
$evidencePins.Add($configureLogCapture.snapshot)
foreach($p in @(($build+'/CMakeCache.txt'),($build+'/compile_commands.json'),$impl,$common,$rules,$queryFile,$depsFile,$cfg.nativeLog.path)){$evidencePins.Add((ObsPin $p))}
foreach($pin in $cfg.controlPins){[void](MatchPin $pin)};foreach($pin in $cfg.dependencyPins){[void](MatchPin $pin)};Need ((ObsPin $InputFile).sha256-ceq$InputSha) 'Observer input changed'
foreach($pin in $ninjaScopePins.Values){[void](MatchPin $pin)}
$record=[ordered]@{ninjaWorkdirBinding=$ninjaWorkdirBinding;ninjaScopePins=$ninjaScopePins;schema='soui.standard-llvm-build-observation/v1';profile=$cfg.profile;root=$root;architecture=$cfg.architecture;configuration=$cfg.configuration;sdkOnly=$cfg.sdkOnly;inputIdentity=$cfg.inputIdentity;bridgeIdentity=$cfg.bridgeIdentity;requestFile=$requestPin;buildReport=$cfg.buildReport;buildCapture=[ordered]@{raw=$buildRaw;context=$contextPin;ready=$readyPin;finished=$finishedPin;pid=$start.pid;creationFileTime=$start.creationFileTime;naturalRootExitCode=0;naturalSupervisorExitCode=0;wholeJobClosed=$true;forced=$false};compilerVerified=$true;observedCompilerId='Clang';observedFrontend='MSVC';observedVersion='22.1.3';observedPointerBytes=$expectedWidth;compilerFacts=$compilerFacts.ToArray();ninjaCompileEdges=$bindings;ninjaDependencyRecords=$deps.selected;outsideSelectedDependencyScope=$deps.outsideScope;outsideDefaultGraph=$outsideGraph;compileRecords=$compileRecords.ToArray();resourceRecords=$resourceRecords.ToArray();frozenDependencyPins=$cfg.dependencyPins;derivedDependencies=$derivedDependencies.ToArray();binaryRecords=$binaryRecords.ToArray();actualToolRows=$currentToolRows.ToArray();toolFileIdentities=@($toolIdentities.Values);currentCompileCount=$currentCompileCount;currentBinaryCount=$currentBinaryCount;dependencyPins=@($dependencyPins.Values);evidencePins=$evidencePins.ToArray();scope='Actual selected configuration/default ALL graph; current commands or hash-bound prior natural compile records. No CTest/runtime/OS qualification or filesystem library-read trace.';completeToolchainProvenance=$false;kernelQualified=$false;abiQualified=$false;win7Qualified=$false;qualificationPassed=$false;releaseReady=$false}
$record.configureLogCapture=$configureLogCapture
$record.resourceWrapperPin=$(if($cfg.PSObject.Properties.Name-ccontains'resourceWrapperPin'){$cfg.resourceWrapperPin}else{$null})
if($debugMode){
 foreach($pin in @($cfg.debugCapture.input,$cfg.debugCapture.raw,$cfg.debugCapture.prepared,$cfg.debugCapture.observer)){[void](MatchPin $pin)}
 $record.schema='soui.standard-llvm-build-observation/v2'
 $record.outerObserverCapture=$record.buildCapture
 $record.buildCapture=[ordered]@{role='inner-build';raw=$cfg.debugCapture.raw;input=$cfg.debugCapture.input;pid=$innerBuild.pid;creationFileTime=$innerBuild.creationFileTime;naturalRootExitCode=0;wholeJobClosed=$true;forced=$false}
 $record.debugCapture=$cfg.debugCapture;$record.fastToolEvidence=$cfg.fastToolEvidence
 $record.independentActualToolRows=$independentToolRows.ToArray()
 $record.originalPollingCapture=$cfg.buildCapture
}
if($installedMode){
  foreach($pin in $installedPins.Values){[void](MatchPin $pin)}
 foreach($role in $roleNames){[void](MatchPin $profile.ToolPins.$role)};foreach($pin in @($profile.RequiredInputPins)+@($profile.BridgePins)){[void](MatchPin $pin)}
 foreach($pair in @(@($installedPaths.source,$installedBinding.consumerSources),@((Join-Path $installedPaths.install 'include'),$installedBinding.installedHeaders),@((Join-Path $root 'sdk/include'),$installedBinding.originalSdkHeaders),@($installedPaths.originalSource,$installedBinding.originalConsumerSources))){Need (SameSet ([string[]]@(InstalledTree $pair[0]|ForEach-Object{Norm $_})) ([string[]]@($pair[1]|ForEach-Object{Norm $_.path}))) 'Installed input inventory changed during observation'}
 $record.schema='soui.installed-acceptance-llvm-build-observation/'+$(if($debugMode){'v2'}else{'v1'});$record.installedConsumer=$installedBinding;$record.installedEvidencePins=@($installedPins.Values)
 $record.scope='Actual fresh ordinary installed CXX acceptance consumer compile/link and imported separately accepted SDK archive. No current SDK archive reclassification, runtime or release qualification.'
}
if($releaseMode){
 foreach($pin in $installedPins.Values){[void](MatchPin $pin)}
 foreach($role in $roleNames){[void](MatchPin $profile.ToolPins.$role)};foreach($pin in @($profile.RequiredInputPins)+@($profile.BridgePins)){[void](MatchPin $pin)}
 foreach($pair in @(@($installedPaths.source,$installedBinding.consumerSources),@((Join-Path $installedPaths.install 'include'),$installedBinding.installedHeaders),@((Join-Path $root 'sdk/include'),$installedBinding.originalSdkHeaders),@($installedPaths.originalSource,$installedBinding.originalConsumerSources))){Need (SameSet ([string[]]@(InstalledTree $pair[0]|ForEach-Object{Norm $_})) ([string[]]@($pair[1]|ForEach-Object{Norm $_.path}))) 'Release input inventory changed during observation'}
 $record.schema='soui.installed-release-llvm-build-observation/'+$(if($debugMode){'v2'}else{'v1'});$record.releaseConsumer=$installedBinding;$record.installedEvidencePins=@($installedPins.Values)
 $record.scope='Actual fresh installed package CXX consumer compile/link and imported separately accepted SDK archive. No current SDK archive reclassification, runtime or release qualification.'
}
[void](MatchPin $configureLogCapture.live);[void](MatchPin $configureLogCapture.snapshot)
$body=[Text.UTF8Encoding]::new($false).GetBytes(($record|ConvertTo-Json -Depth 30));$stream=[IO.File]::Open($ObservationPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($body,0,$body.Length);$stream.Flush($true)}finally{$stream.Dispose()}
