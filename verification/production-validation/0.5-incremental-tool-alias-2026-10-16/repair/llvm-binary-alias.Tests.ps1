param([Parameter(Mandatory)][string]$ObserverPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$ast=[Management.Automation.Language.Parser]::ParseFile($ObserverPath,[ref]$null,[ref]$null)
foreach($name in @('Need','Norm','LocalToolSpelling','CompileArgv','ToolNorm','BinaryCommandTools','BinaryCommandIndex')){
 $nodes=@($ast.FindAll({param($a)$a-is[Management.Automation.Language.FunctionDefinitionAst]-and$a.Name-ceq$name},$true))
 if($name-ceq'BinaryCommandTools'-and$nodes.Count-eq0){continue} # Baseline must reach the semantic alias assertion.
 if($nodes.Count-ne1){throw "Missing unique production function: $name"}
 . ([scriptblock]::Create($nodes[0].Extent.Text))
}
function Check([bool]$condition,[string]$message){if(-not$condition){throw $message}}
$aliases=@{};$calls=[Collections.Generic.List[object]]::new()
$known=@{'c:/tools/llvm-lib.exe'='c:/program files/llvm/bin/llvm-lib.exe';'c:/tools/lld-link.exe'='c:/program files/llvm/bin/lld-link.exe';
 'c:/windows/system32/cmd.exe'='c:/windows/system32/cmd.exe';'c:/kits/rc.exe'='c:/kits/rc.exe';'c:/kits/mt.exe'='c:/kits/mt.exe'}
function ToolIdentity([string]$observed,[string]$role){
 $key=Norm $observed
 Check ($known.ContainsKey($key)) 'Modeled physical file identity rejects unknown path'
 $calls.Add(@{path=$observed;role=$role});$aliases[$key]=$known[$key]
}
$archive='C:\Windows\System32\cmd.exe /C "cd . && C:\tools\llvm-lib.exe /nologo /out:sdk\Debug\soui_sdk.lib a.obj && cd ."'
$expected=$archive.Replace('\','/').Replace('C:/Windows/System32/cmd.exe','c:/windows/system32/cmd.exe').Replace('C:/tools/llvm-lib.exe','c:/program files/llvm/bin/llvm-lib.exe')
$index=BinaryCommandIndex @($archive) @()
Check ($index.generated[0].canonical-ceq$expected) 'Incremental archive must authenticate its configured short alias without a current archiver process'
Check ($index.actual.Count-eq0) 'Configured alias must never invent current execution evidence'
Check (@($calls|Where-Object{$_.role-ceq'ARCHIVER'}).Count-eq1) 'Archive role identity is checked'
$link='C:\Windows\System32\cmd.exe /C "cd . && "C:\Program Files\CMake\bin\cmake.exe" -E vs_link_exe --msvc-ver=1929 --intdir=app.dir --rc=C:\kits\rc.exe --mt=C:\kits\mt.exe --manifests -- C:\tools\lld-link.exe /out:app.exe a.obj /INCREMENTAL:NO && cd ."'
$index=BinaryCommandIndex @($link,$link) @($link)
Check ($index.generated.Count-eq2) 'Duplicate commands remain ambiguous'
Check ($index.generated[0].canonical.Contains('c:/program files/llvm/bin/lld-link.exe')) 'Incremental wrapper linker alias authenticated'
Check ($index.actual[$index.generated[0].canonical].Count-eq1) 'Actual wrapper command shares canonical identity'
Check (@($calls|Where-Object{$_.role-ceq'RC'}).Count-gt0-and@($calls|Where-Object{$_.role-ceq'MT'}).Count-gt0) 'Configured wrapper tools authenticated, never marked executed'
$direct='"C:\tools\lld-link.exe" /out:app.exe a.obj'
Check ((BinaryCommandIndex @($direct) @()).generated[0].canonical.Contains('c:/program files/llvm/bin/lld-link.exe')) 'Direct absolute linker supported'
foreach($raw in @($archive.Replace('cd .','cd elsewhere'),$archive.Replace('/C','/K'),
 $archive.Replace('a.obj','a.obj & echo x'),$archive.Replace('a.obj','a.obj > x'),
 $archive.Replace('llvm-lib.exe','llvm-lib.exe.extra'),$archive.Replace('C:\tools\llvm-lib.exe','llvm-lib.exe'),
 $link.Replace('--manifests --','--manifests unexpected --'),$link.Replace('vs_link_exe','unrecognized-wrapper'))){
 Check (@(BinaryCommandTools $raw).Count-eq0) 'Unknown shell/wrapper/relative shape cannot authenticate a tool'
}
$failed=$false
try{[void](BinaryCommandIndex @($archive.Replace('C:\tools\llvm-lib.exe','C:\untrusted\llvm-lib.exe')) @())}catch{$failed=$_.Exception.Message.Contains('physical file identity')}
Check $failed 'Lookalike executable cannot be aliased based on basename'
$case=$archive.Replace('a.obj','CaseSensitive.obj')
$different=$archive.Replace('a.obj','casesensitive.obj')
$index=BinaryCommandIndex @($case,$different) @($case)
Check ($index.generated[0].canonical-cne$index.generated[1].canonical) 'Argument case stays significant'
Write-Output 'Incremental binary tool aliases: all modeled behavior cases passed; no compiler execution or file identity claim.'
