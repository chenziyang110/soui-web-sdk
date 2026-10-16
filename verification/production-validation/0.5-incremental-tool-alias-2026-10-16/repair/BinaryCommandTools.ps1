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
