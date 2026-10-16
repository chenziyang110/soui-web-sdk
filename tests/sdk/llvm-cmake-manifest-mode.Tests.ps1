param([string]$ObserverPath=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tools/internal/Observe-LlvmBuild.ps1'))))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($ObserverPath,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual observer must parse'}
foreach($name in @('Need','CompileArgv','Read-CmakeManifestMode')){
 $nodes=@($ast.FindAll({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq$name},$true))
 if($nodes.Count-ne1){throw ('Unique actual function required: '+$name)}
 . ([scriptblock]::Create($nodes[0].Extent.Text))
}
$command='C:\Windows\System32\cmd.exe /C "cd . && "C:\Program Files\CMake\bin\cmake.exe" -E vs_link_exe --msvc-ver=1929 --intdir=CMakeFiles\App.dir\Release --rc=C:\SDK\rc.exe --mt=C:\SDK\mt.exe --manifests  -- C:\LLVM\lld-link.exe /nologo main.obj /out:app.exe /INCREMENTAL:NO /subsystem:windows library.lib && cd ."'
$checks=0
function Check($value,[string]$message){if(-not$value){throw $message};$script:checks++}
$mode=Read-CmakeManifestMode $command '4.2.3'
Check ($null-ne$mode-and$mode.requiresManifestTool-eq$false-and$mode.msvcVersion-eq1929-and$mode.nonIncremental-eq$true) 'Actual CMake direct manifest embedding must not invent an mt.exe invocation'
Check ($mode.cmake-ceq'C:\Program Files\CMake\bin\cmake.exe'-and$mode.mt-ceq'C:\SDK\mt.exe') 'Exact configured tool identities retained'
foreach($version in @('1600','1500')){Check ((Read-CmakeManifestMode ($command.Replace('1929',$version)) '4.2.3').requiresManifestTool-eq$true) 'Older wrapper must still require mt.exe'}
foreach($flag in @('/INCREMENTAL','/incremental:yes','-INCREMENTAL:YES')){Check ((Read-CmakeManifestMode ($command.Replace('/INCREMENTAL:NO',$flag)) '4.2.3').requiresManifestTool-eq$true) 'Incremental wrapper must still require mt.exe'}
foreach($changed in @(
 $command.Replace('--manifests  --','--manifests app.manifest --'),
 $command.Replace('/INCREMENTAL:NO',''),
 $command.Replace('/INCREMENTAL:NO','/INCREMENTAL:NO /INCREMENTAL:YES'),
 $command.Replace('/INCREMENTAL:NO','@flags.rsp /INCREMENTAL:NO'),
 $command.Replace('--msvc-ver=1929','--msvc-ver=unknown'),
 $command.Replace('-E vs_link_exe','-E vs_link_dll'),
 $command.Replace(' --mt=C:\SDK\mt.exe',''),
 $command.Replace('library.lib','library.lib && echo forged'),
 $command.Replace('C:\LLVM\lld-link.exe','lld-link.exe'),
 $command.Replace('C:\SDK\mt.exe','C:\SDK\bad|mt.exe'),
 $command.Replace('"C:\Program Files\CMake\bin\cmake.exe"','"C:\Program Files\CMake\bin\cmake.exe')
)){Check ($null-eq(Read-CmakeManifestMode $changed '4.2.3')) 'Unknown or ambiguous mode must retain conservative evidence requirement'}
foreach($version in @('4.2.2','4.3.0','')){Check ($null-eq(Read-CmakeManifestMode $command $version)) 'Unverified CMake version must not bypass mt.exe evidence'}
Check ((Read-CmakeManifestMode ($command.Replace('/INCREMENTAL:NO','-incremental:no')) '4.2.3').requiresManifestTool-eq$false) 'CMake case insensitive switch semantics retained'
[ordered]@{testsPassed=$checks;scope='Actual observer pure manifest mode parser; no native process proof'}|ConvertTo-Json -Compress
