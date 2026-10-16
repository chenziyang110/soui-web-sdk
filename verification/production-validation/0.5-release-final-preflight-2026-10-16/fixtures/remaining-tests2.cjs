'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const standard=require(root+'/tools/internal/standard-build.cjs');
const entry=root+'/tools/Test.ps1',source=fs.readFileSync(entry,'utf8');
const groups=[];
for(const match of source.matchAll(/^  & node --test (.+)$/gm)){
 const line=match[1];
 if(line.trim()==='@newReaderTests'){
  const declaration=source.match(/\$newReaderTests=@\(([\s\S]+?)\) \|/);assert(declaration);
  groups.push([...declaration[1].matchAll(/'([^']+)'/g)].map(m=>m[1]));
 }else{
  const files=[...line.matchAll(/\(Join-Path \$root '([^']+)'\)/g)].map(m=>m[1]);
  assert(files.length&&line.replace(/\(Join-Path \$root '[^']+'\)/g,'').trim()==='');groups.push(files);
 }
}
const start=groups.findIndex(group=>group[0]==='tests/sdk/delivery-identity.test.cjs');assert(start>=0);
const selected=groups.slice(start);
// This conditional vendor source comparison also follows the failed group.
selected.splice(3,0,['tests/native/miniblink_header_abi.test.cjs']);
selected.splice(0,5);
const folder=__dirname+'/remaining-tests2';fs.mkdirSync(folder);
const pins=[entry,...new Set(selected.flat().map(name=>root+'/'+name))].map(e.pin);
fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({groups:selected,pins,scope:'Actual remaining public Node test groups; not a full Build/Test.ps1 acceptance'},null,2)+'\n',{flag:'wx'});
const env={...process.env,SOUI_GUI_TEST_POWERSHELL:'C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'};
(async()=>{
 const npm=path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js');
 const install=await standard.capture(folder,'npm-ci',process.execPath,[npm,'--prefix',root+'/tests/acceptance','ci','--no-audit','--no-fund'],{
  runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:root,env,timeoutMs:180000,drainMs:2000});
 const installed=standard.verifyStream(install.rawReport,JSON.parse(install.contextBytes.toString('utf8')),install.ready,install.result);
 assert(install.result.exitCode===0&&install.result.supervisorExitCode===0&&installed.terminal.activeProcesses===0&&installed.terminal.closureConfirmed&&!installed.terminal.terminatedOwnedJob);
 const results=[];
 for(let index=0;index<selected.length;++index){
  const group=selected[index],name='group-'+index;
  console.log(JSON.stringify({name,first:group[0],started:true}));
  const c=await standard.capture(folder,name,process.execPath,['--test',...group.map(file=>root+'/'+file)],{
   runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:root,env,timeoutMs:300000,drainMs:2000});
  const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
  const passed=c.result.exitCode===0&&c.result.supervisorExitCode===0&&v.terminal.activeProcesses===0&&
   v.terminal.closureConfirmed&&!v.terminal.terminatedOwnedJob&&!c.result.stopRequested&&!c.result.fallbackUsed;
  results.push({name,group,passed,result:c.result,terminal:v.terminal});
  fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({results,pins,complete:results.length===selected.length},null,2)+'\n');
  assert(passed,'Actual remaining public test group failed: '+name);
  console.log(JSON.stringify({name,passed}));
 }
 pins.forEach(e.checkPin);console.log('All selected public test groups passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
