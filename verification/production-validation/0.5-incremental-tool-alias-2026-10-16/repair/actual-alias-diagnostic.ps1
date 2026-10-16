$ErrorActionPreference='Stop'
$taskRoot='C:/work/sm05-release-050'
$taskAttempt="$taskRoot/out/standard-build/attempt-6a171c53-4fed-4668-beb3-8b78d511e73e"
$taskSource='C:/work/sm05-binary-alias-fix-1/Observe-LlvmBuild-v2.ps1'
$taskAst=[Management.Automation.Language.Parser]::ParseFile($taskSource,[ref]$null,[ref]$null)
$taskNames=@('Need','Norm','Full','Plain','ObsPin','MatchPin','RawText','NinjaLogicalLines','NinjaLiteralValue','BoundNinjaWorkdir','NinjaWords','NinjaBuildEdges','NinjaWorkdirScope','LocalToolSpelling','ToolIdentity','ToolNorm','BinaryCommandIndex','CompileArgv','BinaryCommandTools')
foreach($taskFunction in $taskAst.FindAll({param($a)$a-is[Management.Automation.Language.FunctionDefinitionAst]},$true)){
 if($taskNames-ccontains$taskFunction.Name){Invoke-Expression $taskFunction.Extent.Text}
}
$cfg=Get-Content -LiteralPath "$taskAttempt/llvm-observer-inputs.json" -Raw | ConvertFrom-Json
$profile=$cfg.profile
$request=Get-Content -LiteralPath $cfg.requestFile.path -Raw | ConvertFrom-Json
$profile=$request.discovery
$roleNames=@('CLANG','LINKER','ARCHIVER','RC','MT','NINJA','CMD','POWERSHELL')
$toolIdentities=@{};$aliases=@{}
. "$taskRoot/tools/internal/Tool-FileIdentity.ps1"
$build=$cfg.buildDirectory
$cache=@{}
foreach($line in (RawText "$build/CMakeCache.txt").Split([char]10)){if($line-match'^([^#/:][^:]*):[^=]+=(.*)$'){$cache[$Matches[1]]=$Matches[2].TrimEnd([char]13)}}
foreach($pair in @(@('CMAKE_C_COMPILER','CLANG'),@('CMAKE_CXX_COMPILER','CLANG'),@('CMAKE_LINKER','LINKER'),@('CMAKE_LINKER_LLD','LINKER'),@('CMAKE_AR','ARCHIVER'),@('CMAKE_RC_COMPILER','RC'),@('CMAKE_MT','MT'),@('CMAKE_MAKE_PROGRAM','NINJA'))){[void](ToolIdentity $cache[$pair[0]] $pair[1])}
$scope=NinjaWorkdirScope (RawText "$build/CMakeFiles/impl-Debug.ninja") (RawText "$build/CMakeFiles/common.ninja") (RawText "$build/CMakeFiles/rules.ninja") $build
$edges=@(NinjaBuildEdges (RawText "$build/CMakeFiles/impl-Debug.ninja") $build $scope.value)
$generated=[string[]]@((RawText "$taskAttempt/llvm-observer/ninja-commands.txt")-split'\r?\n'|Where-Object{$_-cne''})
$commands=BinaryCommandIndex $generated @()
$rows=@()
foreach($edge in $edges){
 if(($edge.outputs-join' ')-notmatch'soui_sdk.lib'){continue}
 $output=Full $edge.outputs[0] $build
 $needleToken=Norm $profile.ToolPins.ARCHIVER.path
 $relative=Norm $edge.outputs[0];$absolute=Norm $output
 $candidates=@($commands.generated|Where-Object{$_.search.Contains($needleToken)-and($_.search.Contains($relative)-or$_.search.Contains($absolute))})
 $rows+=@{edge=$edge;output=$output;exists=(Test-Path -LiteralPath $output);needle=$needleToken;candidates=$candidates;alias=$aliases}
}
$actual=@($rows|Where-Object{$_.edge.rule-cmatch'^CXX_STATIC_LIBRARY_'})
if($actual.Count-ne1-or$actual[0].candidates.Count-ne1){throw 'Actual generated SDK archive still not uniquely recognized'}
[ordered]@{input=$taskAttempt;observer=$taskSource;physicalToolIdentityChecked=$true;archiveOutput=$actual[0].output;
 actualArchiveProcessRequired=$false;actualSdkArchiveFound=$true;commands=($actual[0].candidates|ForEach-Object{$_.raw});
 aliases=$aliases;qualificationPassed=$false;releaseReady=$false}|ConvertTo-Json -Depth 10
