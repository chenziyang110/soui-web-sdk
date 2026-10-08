$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. 'F:/work/sm05/tools/internal/CMake-Configure.ps1'
$cmake='C:/Program Files/CMake/bin/cmake.exe'
$root='F:\work\sm05-actions\out\actiongen-x86-independent-9a78df14766c4a1f8740adff9bbc46e5'
$commands=[Collections.Generic.List[object]]::new()
function Run-CMake([string[]]$Arguments) { $commands.Add(@{executable=$cmake;arguments=$Arguments}); & $cmake @Arguments; if($LASTEXITCODE -ne 0){throw ('CMake command failed: '+$LASTEXITCODE)} }
try {
$argsConfig=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot 'F:/work/sm05-actions' -SourceDirectory (Join-Path $root 'consumer-source') -BuildDirectory (Join-Path $root 'consumer-build') -Architecture x86)
$argsConfig+=@('-T','v143',('-DCMAKE_PREFIX_PATH='+ (Join-Path $root 'install-relocated').Replace('\','/')))
Run-CMake $argsConfig
Run-CMake @('--build',(Join-Path $root 'consumer-build'),'--config','Release','--target','soui_action_consumer','--','/nr:false','/m:1')
[IO.File]::WriteAllText((Join-Path $root 'consumer-result.json'),(@{passed=$true;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
exit 0
} catch {
[IO.File]::WriteAllText((Join-Path $root 'consumer-result.json'),(@{passed=$false;error=$_.Exception.Message;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Error $_
exit 1
}
