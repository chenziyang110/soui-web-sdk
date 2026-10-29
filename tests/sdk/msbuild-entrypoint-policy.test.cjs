'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');

test('actual PowerShell build policy covers configuration and restores absent or existing caller values on failure',()=>{
  const repository=path.resolve(__dirname,'../..'),folder=fs.mkdtempSync(path.join(os.tmpdir(),'soui-msbuild-policy-'));
  // Keep the small inert fixture in the temporary directory for diagnostics.
  const driver=path.join(folder,'policy.ps1');
  fs.writeFileSync(driver,String.raw`
param([string]$Repository)
$ErrorActionPreference='Stop'
$results=@()
foreach($relative in @('tools/Build.ps1','tools/internal/Build-ValidationTools.ps1')){
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Repository $relative),[ref]$tokens,[ref]$errors)
  if($errors.Count){throw 'Actual PowerShell parse failed'}
  $tries=@($ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.TryStatementAst]})
  if($tries.Count-ne1){throw 'Expected one actual main try'}
  $setup=@($ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.AssignmentStatementAst]-and
    $_.Left.Extent.Text -in @('$buildEnvironment','$nodeReusePrevious','$buildEnvironment[''MSBUILDDISABLENODEREUSE'']')})
  if($setup.Count-ne3){throw 'Expected actual caller presence/value capture'}
  $body=@($tries[0].Body.Statements)
  $apply=@($body|Where-Object{$_ -is [Management.Automation.Language.AssignmentStatementAst]-and $_.Left.Extent.Text-ceq'$env:MSBUILDDISABLENODEREUSE'})
  $configure=@($body|Where-Object{$_.Extent.Text-match '^& \$cmake(?:\.Source)? @'})[0]
  $restore=@($tries[0].Finally.Statements|Where-Object{$_ -is [Management.Automation.Language.ForEachStatementAst]-and $_.Condition.Extent.Text-ceq'$buildEnvironment.Keys'})
  $argsSetup=@($body|Where-Object{$_ -is [Management.Automation.Language.AssignmentStatementAst]-and $_.Left.Extent.Text -in @('$buildArguments','$buildArgs')})
  if($apply.Count-ne1-or$restore.Count-ne1-or$argsSetup.Count-ne1-or$apply[0].Extent.StartOffset-ge$configure.Extent.StartOffset){throw 'Policy must cover actual configure and final restoration'}
  $argsBranch=@($body|Where-Object{$_ -is [Management.Automation.Language.IfStatementAst]-and $_.Extent.StartOffset-gt$argsSetup[0].Extent.StartOffset-and $_.Extent.Text.Contains('/nr:false')})
  if($argsBranch.Count-ne1){throw 'Expected actual native profile argument branch'}
  foreach($original in @($null,'0','1')){foreach($profile in @('MSVC143','LLVM22MSVC142')){
    Remove-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
    if($null-ne$original){$env:MSBUILDDISABLENODEREUSE=$original}
    foreach($statement in $setup){. ([ScriptBlock]::Create($statement.Extent.Text))}
    $llvm=if($profile-eq'MSVC143'){$null}else{@{selected=$true}}
    $output='inert-output';$validationBuild='inert-validation';$Configuration='Release';$FreshConfigure=$false
    $caught=$false
    try{
      . ([ScriptBlock]::Create($apply[0].Extent.Text))
      if($env:MSBUILDDISABLENODEREUSE-cne'1'){throw 'Actual policy was not applied'}
      . ([ScriptBlock]::Create($argsSetup[0].Extent.Text))
      . ([ScriptBlock]::Create($argsBranch[0].Extent.Text))
      $actual=if($relative-eq'tools/Build.ps1'){$buildArguments}else{$buildArgs}
      if($profile-eq'MSVC143'){
        if($actual[-2]-cne'--'-or$actual[-1]-cne'/nr:false'){throw 'MSVC must explicitly disable node reuse'}
      }elseif($actual-contains'/nr:false'-or$actual-contains'--'){throw 'MSBuild arguments reached LLVM'}
      throw 'modeled configuration failure; no native executable is invoked'
    }catch{
      if($_.Exception.Message-cne'modeled configuration failure; no native executable is invoked'){throw}
      $caught=$true
    }finally{. ([ScriptBlock]::Create($restore[0].Extent.Text))}
    $restored=Get-Item -LiteralPath Env:MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
    if(-not$caught-or(($null-ne$restored)-ne($null-ne$original))-or($null-ne$original-and$restored.Value-cne$original)){throw 'Caller environment presence/value changed'}
    $results+=@{entry=$relative;profile=$profile;original=$original;passed=$true;modelOnly=$true}
  }}
}
$results|ConvertTo-Json -Depth 5 -Compress
`);
  const env={...process.env};for(const key of Object.keys(env))if(key.toLowerCase()==='psmodulepath')delete env[key];
  const shell=process.env.SOUI_GUI_TEST_POWERSHELL||(process.platform==='win32'?'powershell.exe':'pwsh');
  const actual=spawnSync(shell,['-NoProfile','-NonInteractive','-File',driver,'-Repository',repository],
    {encoding:'utf8',windowsHide:true,timeout:30000,env});
  assert.equal(actual.status,0,`${actual.error||''}\n${actual.stderr}\n${actual.stdout}`);
  const cases=JSON.parse(actual.stdout);assert.equal(cases.length,12);assert.equal(cases.every(row=>row.passed&&row.modelOnly),true);
});
