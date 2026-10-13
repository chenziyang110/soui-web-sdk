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

function NinjaWords([string]$text){
 $tokens=[Collections.Generic.List[object]]::new();$word=[Text.StringBuilder]::new()
 for($i=0;$i -lt $text.Length;$i++){
  $c=$text[$i]
  if($c -ceq '$'){
   Need ($i+1 -lt $text.Length) 'Unterminated Ninja escape'
   $i++;$next=$text[$i]
   Need ($next -ceq '$' -or $next -ceq ':' -or $next -ceq ' ') 'Unsupported Ninja variable/escape'
   [void]$word.Append($next);continue
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

function NinjaBuildEdges([string]$text){
 $edges=[Collections.Generic.List[object]]::new()
 foreach($line in @(NinjaLogicalLines $text)){
  if(-not $line.text.StartsWith('build ',[StringComparison]::Ordinal)){continue}
  $tokens=@(NinjaWords $line.text.Substring(6));$colon=@()
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

$inputPin=ObsPin $InputFile;Need ($inputPin.sha256-ceq$InputSha) 'Observer input bytes differ'
$cfg=RawText $InputFile|ConvertFrom-Json
Need ($cfg.schema-ceq'soui.standard-llvm-observer-inputs/v1'-and$cfg.profile-ceq'LLVM22MSVC142'-and$cfg.architecture-cin@('x64','x86')-and$cfg.configuration-cin@('Debug','Release')-and$cfg.qualificationPassed-eq$false) 'Explicit current observer scope'
$root=[IO.Path]::GetFullPath($cfg.root);$build=[IO.Path]::GetFullPath($cfg.buildDirectory);$folder=[IO.Path]::GetFullPath($cfg.evidenceDirectory)
Need ((Norm $build)-ceq(Norm (Join-Path $root ('out/'+$(if($cfg.sdkOnly){'sdk-'}else{''})+$cfg.architecture)))) 'Actual owned SDK/application build directory'
Need ((Norm ([IO.Path]::GetFullPath($ObservationPath)))-ceq(Norm (Join-Path $folder 'llvm-observation.json'))-and-not(Test-Path -LiteralPath $ObservationPath)) 'Fresh exact observer output'
[void](Plain $root);[void](Plain $build);[void](Plain $folder)
$evidencePins=[Collections.Generic.List[object]]::new();foreach($pin in $cfg.controlPins){$evidencePins.Add((MatchPin $pin))}
$requestPin=MatchPin $cfg.requestFile;$request=RawText $cfg.requestFile.path|ConvertFrom-Json;$profile=$request.discovery
$sourceBuildPin=MatchPin $cfg.buildReport;$sourceBuild=RawText $cfg.buildReport.path|ConvertFrom-Json
Need ($sourceBuild.schema-ceq'soui.source-build/v2'-and$sourceBuild.nativeBuild-ceq'passed'-and$sourceBuild.architecture-ceq$cfg.architecture-and$sourceBuild.configuration-ceq$cfg.configuration-and$sourceBuild.sdkOnly-eq$cfg.sdkOnly-and$sourceBuild.releaseReady-eq$false-and$sourceBuild.nativeToolchain.requestFile.sha256-ceq$requestPin.sha256) 'Actual source-build report scope/request'
Need ($request.schema-ceq'soui.build-toolchain-request/v1'-and$request.profile-ceq$cfg.profile-and$request.architecture-ceq$cfg.architecture-and(@('soui.llvm-profile-inputs/v2','soui.llvm-profile-inputs/v3')-ccontains$profile.InputIdentitySchema)-and$profile.InputIdentity-ceq$cfg.inputIdentity-and$profile.BridgeIdentity-ceq$cfg.bridgeIdentity-and$profile.GeneratorValidated-eq$false-and$profile.Qualified-eq$false-and$null-eq$profile.ActualCompilerVersion) 'Matching request is input, never compiler proof'
$roleNames=@('CLANG','LINKER','ARCHIVER','RC','MT','NINJA','CMD','POWERSHELL');Need (($profile.ToolPins.PSObject.Properties.Name-join',')-ceq($roleNames-join',')-and(($profile.InputIdentitySchema-ceq'soui.llvm-profile-inputs/v2'-and$profile.RequiredInputPins.Count-eq17)-or($profile.InputIdentitySchema-ceq'soui.llvm-profile-inputs/v3'-and$profile.RequiredInputPins.Count-eq18))-and$profile.BridgePins.Count-eq2) 'Exact fixed profile inputs'
$helper=Join-Path $PSScriptRoot 'Tool-FileIdentity.ps1';Need ((Norm $profile.CMakeDefinitions.SOUI_TOOL_IDENTITY_HELPER)-ceq(Norm $helper)) 'Fixed product helper only';[void](MatchPin (@($profile.BridgePins|Where-Object{(Norm $_.path)-ceq(Norm $helper)})[0]));. $helper
$toolIdentities=@{};$aliases=@{}
function ToolIdentity([string]$observed,[string]$role){
 Need ($roleNames-ccontains$role) 'Fixed role only';$key=$role+'|'+(Norm $observed)
 if(-not$toolIdentities.ContainsKey($key)){$receipt=Assert-ToolFileIdentity -ObservedPath $observed -ExpectedPin $profile.ToolPins.$role;$toolIdentities[$key]=$receipt;$aliases[(Norm $observed)]=Norm $profile.ToolPins.$role.path}
 return $toolIdentities[$key]
}
foreach($role in $roleNames){[void](MatchPin $profile.ToolPins.$role)};foreach($pin in @($profile.RequiredInputPins)+@($profile.BridgePins)){[void](MatchPin $pin)}
function ToolNorm([string]$text){
 # Only proven physical tool aliases and path separators are normalized. Keep
 # case-sensitive definitions/literal arguments intact in command comparisons.
 $value=$text.Trim().Replace('\','/');foreach($alias in @($aliases.Keys|Sort-Object Length -Descending)){
  $replacement=[string]$aliases[$alias]
  $value=[regex]::Replace($value,'(?<![a-z0-9_./-])'+[regex]::Escape($alias)+'(?![a-z0-9_./-])',[Text.RegularExpressions.MatchEvaluator]{param($m)$replacement},[Text.RegularExpressions.RegexOptions]::IgnoreCase)
 };return $value
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

$cache=@{};foreach($line in (RawText ($build+'/CMakeCache.txt')).Split([char]10)){
 if($line-match'^([^#/:][^:]*):[^=]+=(.*)$'){Need (-not$cache.ContainsKey($Matches[1])) 'Duplicate actual CMake cache key';$cache[$Matches[1]]=$Matches[2].TrimEnd([char]13)}
}
foreach($pair in @(@('CMAKE_GENERATOR','Ninja Multi-Config'),@('CMAKE_CONFIGURATION_TYPES','Debug;Release;RelWithDebInfo'),@('CMAKE_SYSTEM_VERSION','10.0.19041.0'),@('CMAKE_EXPORT_COMPILE_COMMANDS','ON'))){Need ($cache.ContainsKey($pair[0])-and$cache[$pair[0]]-ceq$pair[1]) ('Actual cache differs: '+$pair[0])}
Need ((Norm $cache.CMAKE_HOME_DIRECTORY)-ceq(Norm $(if($cfg.sdkOnly){Join-Path $root 'sdk'}else{$root}))-and(Norm $cache.CMAKE_CACHEFILE_DIR)-ceq(Norm $build)) 'Actual cache source/build ownership'
foreach($definition in $profile.CMakeDefinitions.PSObject.Properties){Need ($cache.ContainsKey($definition.Name)-and$cache[$definition.Name]-ceq[string]$definition.Value) ('Actual profile definition differs: '+$definition.Name)}
foreach($pair in @(@('CMAKE_C_COMPILER','CLANG'),@('CMAKE_CXX_COMPILER','CLANG'),@('CMAKE_LINKER','LINKER'),@('CMAKE_LINKER_LLD','LINKER'),@('CMAKE_AR','ARCHIVER'),@('CMAKE_RC_COMPILER','RC'),@('CMAKE_MT','MT'),@('CMAKE_MAKE_PROGRAM','NINJA'))){Need ($cache.ContainsKey($pair[0])) ('Actual tool slot missing: '+$pair[0]);[void](ToolIdentity $cache[$pair[0]] $pair[1])}
$configurationFiles=@(Get-ChildItem -LiteralPath ($build+'/CMakeFiles') -Directory|Where-Object{$_.Name-cmatch'^[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?$'})
Need ($configurationFiles.Count-eq1) 'Unique actual CMake version directory';$compilerFacts=[Collections.Generic.List[object]]::new();$expectedMachine=$(if($cfg.architecture-ceq'x64'){34404}else{332});$expectedWidth=$(if($cfg.architecture-ceq'x64'){8}else{4});$archId=$(if($cfg.architecture-ceq'x64'){'x64'}else{'X86'})
foreach($language in @('C','CXX')){
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
 try{foreach($relative in @('build/native/include/WebView2.h',('build/native/'+$cfg.architecture+'/WebView2LoaderStatic.lib'),'LICENSE.txt','NOTICE.txt')){
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
$impl=$build+'/CMakeFiles/impl-'+$cfg.configuration+'.ninja';$edges=@(NinjaBuildEdges (RawText $impl));$queryFile=Join-Path $folder 'ninja-commands.txt';$depsFile=Join-Path $folder 'ninja-dependencies.txt'
foreach($output in @($queryFile,$depsFile)){Need (-not(Test-Path -LiteralPath $output)) 'Fresh actual Ninja readout required'}
Push-Location $build
try{
 & $profile.ToolPins.NINJA.path -f ('build-'+$cfg.configuration+'.ninja') -t commands *> $queryFile;Need ($LASTEXITCODE-eq0) 'Actual Ninja commands query failed'
 & $profile.ToolPins.NINJA.path -f ('build-'+$cfg.configuration+'.ninja') -t deps *> $depsFile;Need ($LASTEXITCODE-eq0) 'Actual Ninja dependency query failed'
}finally{Pop-Location}
$generated=[string[]]@((RawText $queryFile)-split'\r?\n'|Where-Object{$_-cne''});$normalizedGenerated=@($generated|ForEach-Object{CompileNorm $_ $true});$normalizedVerbose=@(CompileVerboseRecords $nativeText)
$graphCommands=@();$outsideGraph=@();foreach($command in $selectedCommands){$needle=CompileNorm $command.command;Need ($null-ne$needle) 'Selected compile database record must be a direct clang command';$matches=@($normalizedGenerated|Where-Object{$_-ceq$needle});Need ($matches.Count-le1) 'Ambiguous generated compile command';if($matches.Count){$graphCommands+=,$command}else{$outsideGraph+=,[ordered]@{source=$command.file;output=$command.output;scope='compile database declaration outside selected default ALL graph; not compiled proof'}}}
Need ($graphCommands.Count-gt0) 'No default graph compiler commands'
$selectedObjects=@{};foreach($command in $graphCommands){$selectedObjects[(Norm (Full $command.output $build))]=$true}
$graphEdges=@($edges|Where-Object{$_.rule-cmatch'^(C|CXX)_COMPILER(?:_|$)'-and$_.outputs.Count-eq1-and$selectedObjects.ContainsKey((Norm (Full $_.outputs[0] $build)))})
$bindings=@(CompileBindings $build $graphCommands $graphEdges);$deps=DependencyBindings $build $graphCommands (RawText $depsFile)
$previous=$null;if($null-ne$cfg.previousObservation){[void](MatchPin $cfg.previousObservation);$previous=RawText $cfg.previousObservation.path|ConvertFrom-Json;Need ($previous.schema-ceq'soui.standard-llvm-build-observation/v1'-and$previous.compilerVerified-eq$true-and$previous.architecture-ceq$cfg.architecture-and$previous.configuration-ceq$cfg.configuration-and$previous.inputIdentity-ceq$cfg.inputIdentity-and$previous.bridgeIdentity-ceq$cfg.bridgeIdentity) 'Prior observation scope/inputs differ'}
$compileRecords=[Collections.Generic.List[object]]::new();$objects=@{};$neededRoles=@{NINJA=$true};$currentCompileCount=0
foreach($command in $graphCommands){
 $source=Full $command.file $build;$sourcePin=DependencyPin $source;$object=Full $command.output $build;$objectPin=ObsPin $object;$bytes=[IO.File]::ReadAllBytes($object);$machine=CoffMachine $bytes 0 $bytes.Length;Need ($machine-eq$expectedMachine) 'Actual compiler object architecture'
 $needle=CompileNorm $command.command;[string[]]$flagArgs=@(CompileArgv $command.command);$flagArgs[0]=ToolNorm $flagArgs[0];$flags=Norm ($flagArgs-join' ');Need ($flags.Contains((Norm $profile.ToolPins.CLANG.path))-and$flags.Contains('--no-default-config')-and$flags.Contains('--target='+$profile.Target)-and$flags-match'(?<!\S)/x(?!\S)'-and$flags.Contains('-fms-compatibility-version=19.29.30159')-and$flags-notmatch'(?<!\S)/gl(?!\S)') 'Actual compiler profile command differs'
 foreach($include in $profile.IncludeDirectories){Need ($flags.Contains((Norm $include))) 'Actual compiler include root missing'}
 $isSdk=$source.StartsWith((Join-Path $root 'sdk/src/').Replace('\','/'),[StringComparison]::OrdinalIgnoreCase)-or(Norm $source).StartsWith((Norm (Join-Path $root 'sdk/src/')),[StringComparison]::Ordinal)
 if($isSdk){Need ($flags-match'(?<!\S)/md(?!\S)'-and$flags-match'(?<!\S)/u_debug(?!\S)'-and$flags-match'(?<!\S)[/-]dwinver=0x0601(?!\S)'-and$flags-match'(?<!\S)[/-]d_win32_winnt=0x0601(?!\S)') 'SDK target CRT/Unicode OS macro contract'}
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
$sdkExpected=[string[]]@($cfg.dependencyPins|Where-Object{(Norm $_.path).StartsWith((Norm (Join-Path $root 'sdk/src/')),[StringComparison]::Ordinal)-and$_.path.EndsWith('.cpp',[StringComparison]::Ordinal)}|ForEach-Object{Norm $_.path})
Need ($sdkExpected.Count-gt0-and(SameSet $sdkExpected ([string[]]@($graphCommands|Where-Object{(Norm $_.file).StartsWith((Norm (Join-Path $root 'sdk/src/')),[StringComparison]::Ordinal)}|ForEach-Object{Norm $_.file})))) 'All current normal SDK C++ sources must belong to the actual default graph'

$resourceRecords=[Collections.Generic.List[object]]::new();$resources=@{}
foreach($edge in $edges){
 if($edge.rule-cnotmatch'^RC_COMPILER(?:_|$)'){continue}
 Need ($edge.outputs.Count-eq1-and$edge.inputs.Count-eq1-and$edge.implicitOutputs.Count-eq0) 'One explicit resource source/output required'
 $output=Full $edge.outputs[0] $build;$source=Full $edge.inputs[0] $build
 $candidates=@($generated|Where-Object{(Norm (ToolNorm $_)).Contains((Norm $profile.ToolPins.RC.path))-and(Norm $_).Contains((Norm $edge.outputs[0]))})
 if(-not$candidates.Count){continue};Need ($candidates.Count-eq1-and$source.EndsWith('.rc',[StringComparison]::OrdinalIgnoreCase)) 'Exact selected RC command/source'
 $sourcePin=DependencyPin $source;$outputPin=ObsPin $output;$needle=ToolNorm $candidates[0]
 $actual=@($verbose|Where-Object{(ToolNorm $_)-ceq$needle});Need ($actual.Count-le1) 'Resource command observed repeatedly';$reuse=$null
 if($actual.Count){$neededRoles.RC=$true}else{
  Need ($null-ne$previous) 'Resource output lacks current command/prior natural proof'
  $old=@($previous.resourceRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $output)})
  Need ($old.Count-eq1-and$old[0].normalizedCommand-ceq$needle-and$old[0].source.sha256-ceq$sourcePin.sha256-and$old[0].output.sha256-ceq$outputPin.sha256) 'Incremental resource proof differs'
  # RC does not publish Ninja deps. Require every pre-frozen source/tool input
  # unchanged for reuse instead of claiming a reconstructed header-read trace.
  Need (SameSet ([string[]]@($cfg.dependencyPins|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256})) ([string[]]@($previous.frozenDependencyPins|ForEach-Object{(Norm $_.path)+'|'+$_.bytes+'|'+$_.sha256}))) 'Incremental resource dependency inventory differs'
  $reuse=$cfg.previousObservation
 }
 $record=[ordered]@{source=$sourcePin;output=$outputPin;normalizedCommand=$needle;generatedCommand=$candidates[0];actualVerbose=$(if($actual.Count){$actual[0]}else{$null});reusedFrom=$reuse;dependencyReadTraceAvailable=$false}
 $resourceRecords.Add($record);$resources[(Norm $output)]=$outputPin
}
$binaryRecords=[Collections.Generic.List[object]]::new();$requiredLibraryKeys=@{};$currentBinaryCount=0
foreach($edge in $edges){
 if($edge.rule-cnotmatch'^(C|CXX)_(STATIC_LIBRARY|EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER(?:_|$)'){continue}
 Need ($edge.outputs.Count-eq1) 'One archive/link output per selected edge';$output=Full $edge.outputs[0] $build
 if(-not(Test-Path -LiteralPath $output)){continue} # Declared non-ALL targets are recorded above, never called built.
 $role=$(if($edge.rule-match'_STATIC_LIBRARY_'){'ARCHIVER'}else{'LINKER'});$needleToken=Norm $profile.ToolPins.$role.path
 $candidates=@($generated|Where-Object{(Norm (ToolNorm $_)).Contains($needleToken)-and((Norm (ToolNorm $_)).Contains((Norm $edge.outputs[0]))-or(Norm (ToolNorm $_)).Contains((Norm $output)))})
 if(-not$candidates.Count){continue};Need ($candidates.Count-eq1) 'One generated archive/link command for output';$needle=ToolNorm $candidates[0]
 Need ($needle-notmatch'(?<!\S)/(gl|ltcg)(?::[^\s]+)?(?!\S)') 'No mixed MSVC GL/LTCG in LLVM profile'
 if($role-ceq'LINKER'){foreach($directory in $profile.LibraryDirectories){Need ((Norm $needle).Contains((Norm $directory))) 'Selected CRT/SDK linker search root absent'}}
 $actual=@($verbose|Where-Object{(ToolNorm $_)-ceq$needle});Need ($actual.Count-le1) 'Repeated archive/link command';$outputPin=ObsPin $output;$reuse=$null
 $inputObjects=@();$inputLibraries=@();$inputResources=@();foreach($relative in @($edge.inputs)+@($edge.implicitInputs)){
  $p=Full $relative $build;$key=Norm $p
  if($objects.ContainsKey($key)){$inputObjects+=,$objects[$key].object}
  elseif($resources.ContainsKey($key)){$inputResources+=,$resources[$key]}
  elseif($p.EndsWith('.lib',[StringComparison]::OrdinalIgnoreCase)){
   if($key.StartsWith(((Norm $build).TrimEnd('/')+'/'),[StringComparison]::Ordinal)){$inputLibraries+=,(ObsPin $p);$requiredLibraryKeys[$key]=$true}else{$inputLibraries+=,(DependencyPin $p)}
  }
 }
 if(-not$actual.Count){
  Need ($null-ne$previous) ('No actual link/archive command or authenticated prior output: '+$output)
  $old=@($previous.binaryRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $output)})
  Need ($old.Count-eq1-and$old[0].normalizedCommand-ceq$needle-and$old[0].output.sha256-ceq$outputPin.sha256-and$old[0].output.bytes-eq$outputPin.bytes-and(SameSet ([string[]]@($inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})))-and(SameSet ([string[]]@($inputLibraries|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputLibraries|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})))-and(SameSet ([string[]]@($inputResources|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})) ([string[]]@($old[0].inputResources|ForEach-Object{(Norm $_.path)+'|'+$_.sha256+'|'+$_.bytes})))) 'Incremental link/archive inputs/output provenance differs';$reuse=$cfg.previousObservation
 }else{$currentBinaryCount++;$neededRoles[$role]=$true;if((Norm $needle).Contains((Norm $profile.ToolPins.MT.path))){$neededRoles.MT=$true}}
 $members=$null;$machine=$expectedMachine
 if($role-ceq'ARCHIVER'){
  Need ($inputObjects.Count-gt0) 'Built archive has no current object inputs';$archive=ArchiveMembers $output;$members=$archive.members
  Need ($members.Count-eq$inputObjects.Count-and@($members|Where-Object{$_.machine-ne$expectedMachine}).Count-eq0-and(SameSet ([string[]]@($members|ForEach-Object{$_.sha256+'|'+$_.bytes})) ([string[]]@($inputObjects|ForEach-Object{$_.sha256+'|'+$_.bytes})))) 'Archive members must equal actual object bodies exactly'
 }else{$pe=PeMachine $output;$machine=$pe.machine;Need ($machine-eq$expectedMachine) 'Actual linked PE target architecture'}
 $binaryRecords.Add([ordered]@{output=$outputPin;role=$role;machine=$machine;edge=$edge;generatedCommand=$candidates[0];normalizedCommand=$needle;actualVerbose=$(if($actual.Count){$actual[0]}else{$null});reusedFrom=$reuse;inputObjects=$inputObjects;inputLibraries=$inputLibraries;inputResources=$inputResources;archiveMembers=$members})
}
Need ($binaryRecords.Count-gt0) 'No actual SDK archive/application link proof'
$sdkArchive=Join-Path $build ($(if($cfg.sdkOnly){''}else{'sdk/'})+$cfg.configuration+'/soui_sdk.lib')
Need (@($binaryRecords|Where-Object{(Norm $_.output.path)-ceq(Norm $sdkArchive)-and$_.role-ceq'ARCHIVER'}).Count-eq1) 'Actual normal SDK archive missing from proof'
foreach($key in $requiredLibraryKeys.Keys){Need (@($binaryRecords|Where-Object{(Norm $_.output.path)-ceq$key}).Count-eq1) 'Generated library dependency lacks an actual archive binding'}
foreach($role in $neededRoles.Keys){Need ($currentRoles.ContainsKey($role)) ('Actual owned tool process was not observed: '+$role)}
foreach($p in @(($build+'/CMakeCache.txt'),($build+'/CMakeFiles/CMakeConfigureLog.yaml'),($build+'/compile_commands.json'),$impl,($build+'/CMakeFiles/rules.ninja'),$queryFile,$depsFile,$cfg.nativeLog.path)){$evidencePins.Add((ObsPin $p))}
foreach($pin in $cfg.controlPins){[void](MatchPin $pin)};foreach($pin in $cfg.dependencyPins){[void](MatchPin $pin)};Need ((ObsPin $InputFile).sha256-ceq$InputSha) 'Observer input changed'
$record=[ordered]@{schema='soui.standard-llvm-build-observation/v1';profile=$cfg.profile;root=$root;architecture=$cfg.architecture;configuration=$cfg.configuration;sdkOnly=$cfg.sdkOnly;inputIdentity=$cfg.inputIdentity;bridgeIdentity=$cfg.bridgeIdentity;requestFile=$requestPin;buildReport=$cfg.buildReport;buildCapture=[ordered]@{raw=$buildRaw;context=$contextPin;ready=$readyPin;finished=$finishedPin;pid=$start.pid;creationFileTime=$start.creationFileTime;naturalRootExitCode=0;naturalSupervisorExitCode=0;wholeJobClosed=$true;forced=$false};compilerVerified=$true;observedCompilerId='Clang';observedFrontend='MSVC';observedVersion='22.1.3';observedPointerBytes=$expectedWidth;compilerFacts=$compilerFacts.ToArray();ninjaCompileEdges=$bindings;ninjaDependencyRecords=$deps.selected;outsideSelectedDependencyScope=$deps.outsideScope;outsideDefaultGraph=$outsideGraph;compileRecords=$compileRecords.ToArray();resourceRecords=$resourceRecords.ToArray();frozenDependencyPins=$cfg.dependencyPins;derivedDependencies=$derivedDependencies.ToArray();binaryRecords=$binaryRecords.ToArray();actualToolRows=$currentToolRows.ToArray();toolFileIdentities=@($toolIdentities.Values);currentCompileCount=$currentCompileCount;currentBinaryCount=$currentBinaryCount;dependencyPins=@($dependencyPins.Values);evidencePins=$evidencePins.ToArray();scope='Actual selected configuration/default ALL graph; current commands or hash-bound prior natural compile records. No CTest/runtime/OS qualification or filesystem library-read trace.';completeToolchainProvenance=$false;kernelQualified=$false;abiQualified=$false;win7Qualified=$false;qualificationPassed=$false;releaseReady=$false}
$body=[Text.UTF8Encoding]::new($false).GetBytes(($record|ConvertTo-Json -Depth 30));$stream=[IO.File]::Open($ObservationPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($body,0,$body.Length);$stream.Flush($true)}finally{$stream.Dispose()}
