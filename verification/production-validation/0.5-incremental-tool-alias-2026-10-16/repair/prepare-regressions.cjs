'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050';
let tests=fs.readFileSync(root+'/tests/sdk/llvm-binary-command-index.Tests.ps1','utf8');
const old="@('ToolNorm','BinaryCommandIndex')";assert(tests.includes(old));
tests=tests.replace(old,"@('Need','LocalToolSpelling','CompileArgv','ToolNorm','BinaryCommandTools','BinaryCommandIndex')");
fs.writeFileSync(__dirname+'/llvm-binary-command-index.Tests.ps1',tests,{flag:'wx'});
let diag=fs.readFileSync('C:/work/sm05-source27-pipeline-1/diagnose-binary-index.ps1','utf8');
diag=diag.replace('$taskSource="$taskRoot/tools/internal/Observe-LlvmBuild.ps1"',"$taskSource='C:/work/sm05-binary-alias-fix-1/Observe-LlvmBuild-v2.ps1'");
diag=diag.replace("'BinaryCommandIndex')","'BinaryCommandIndex','CompileArgv','BinaryCommandTools')");
diag=diag.replace('$rows | ConvertTo-Json -Depth 15',`$actual=@($rows|Where-Object{$_.edge.rule-cmatch'^CXX_STATIC_LIBRARY_'})
if($actual.Count-ne1-or$actual[0].candidates.Count-ne1){throw 'Actual generated SDK archive still not uniquely recognized'}
[ordered]@{input=$taskAttempt;observer=$taskSource;physicalToolIdentityChecked=$true;archiveOutput=$actual[0].output;
 actualArchiveProcessRequired=$false;actualSdkArchiveFound=$true;commands=($actual[0].candidates|ForEach-Object{$_.raw});
 aliases=$aliases;qualificationPassed=$false;releaseReady=$false}|ConvertTo-Json -Depth 10`);
fs.writeFileSync(__dirname+'/actual-alias-diagnostic.ps1',diag,{flag:'wx'});
console.log('Prepared original index regressions and real physical-alias diagnostic.');
