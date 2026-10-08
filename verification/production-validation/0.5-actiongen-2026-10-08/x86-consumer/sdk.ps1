$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. 'F:/work/sm05/tools/internal/CMake-Configure.ps1'
$cmake='C:/Program Files/CMake/bin/cmake.exe'
$root='F:\work\sm05-actions\out\actiongen-x86-independent-9a78df14766c4a1f8740adff9bbc46e5'
$commands=[Collections.Generic.List[object]]::new()
function Run-CMake([string[]]$Arguments) { $commands.Add(@{executable=$cmake;arguments=$Arguments}); & $cmake @Arguments; if($LASTEXITCODE -ne 0){throw ('CMake command failed: '+$LASTEXITCODE)} }
try {
$argsConfig=@(Get-SouiCMakeConfigureArguments -CMake $cmake -WorkspaceRoot 'F:/work/sm05-actions' -SourceDirectory 'F:/work/sm05-actions/sdk' -BuildDirectory (Join-Path $root 'sdk-build') -Architecture x86)
$argsConfig+=@('-T','v143','-DSOUI_BUILD_WEBVIEW2=OFF',('-DCMAKE_INSTALL_PREFIX='+ (Join-Path $root 'install-original').Replace('\','/')))
Run-CMake $argsConfig
Run-CMake @('--build',(Join-Path $root 'sdk-build'),'--config','Release','--target','soui_sdk','--','/nr:false','/m:1')
Run-CMake @('--install',(Join-Path $root 'sdk-build'),'--config','Release')
$original=[IO.Path]::GetFullPath((Join-Path $root 'install-original'))
$relocated=[IO.Path]::GetFullPath((Join-Path $root 'install-relocated'))
$owned=[IO.Path]::GetFullPath($root).TrimEnd('\','/')+'\'
foreach($target in @($original,$relocated)){if(-not $target.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw 'Relocation path escaped owned fixture'}}
if(Test-Path -LiteralPath $relocated){throw 'Relocation destination already exists'}
Assert-SouiCMakePath $original
Move-Item -LiteralPath $original -Destination $relocated
if(Test-Path -LiteralPath $original){throw 'Original install path remains'}
[IO.File]::WriteAllText((Join-Path $root 'sdk-result.json'),(@{passed=$true;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
exit 0
} catch {
[IO.File]::WriteAllText((Join-Path $root 'sdk-result.json'),(@{passed=$false;error=$_.Exception.Message;commands=@($commands.ToArray())}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Error $_
exit 1
}
