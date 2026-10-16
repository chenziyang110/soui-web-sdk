'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const prepared=JSON.parse(fs.readFileSync(__dirname+'/PREPARED.json'));
const built=JSON.parse(fs.readFileSync(__dirname+'/capture-build-x64-build2/BUILT.json'));assert(built.built);built.pins.forEach(e.checkPin);e.verifySource(built.native,__dirname+'/native');
prepared.inputs.forEach(x=>e.checkPin(x.pin));e.checkPin(prepared.before);e.checkPin(prepared.after);
const folder=__dirname+'/cases2';fs.mkdirSync(folder);
const binaries=['Before','After'].map(variant=>({variant,pin:e.pin(__dirname+'/out/x64-build2/Release/Parser'+variant+'.exe')}));
const records=[];
(async()=>{
 for(const binary of binaries)for(const test of prepared.inputs){
  const expected=binary.variant==='Before'&&test.name==='duplicate-uia'?'input-explicit-profile':test.expected;
  const expectedExit=binary.variant==='Before'&&test.name==='with-uia'?1:0;
  const name=binary.variant.toLowerCase()+'-'+test.name;
  const c=await standard.capture(folder,name,binary.pin.path,[test.pin.path,expected],{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:__dirname,env:process.env,timeoutMs:15000,drainMs:2000});
  const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
  const closed=c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
  const stdout=fs.readFileSync(folder+'/'+name+'-stdout.bin','utf8');
  records.push({name,expected,expectedExit,result:c.result,terminal:v.terminal,closed,stdout});
  fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({prepared,binaries,records},null,2)+'\n');
  assert(closed&&c.result.exitCode===expectedExit&&c.result.supervisorExitCode===expectedExit,name);
  if(expectedExit===1)assert(stdout.includes('native-input-rejected|input-explicit-profile|13'));
  console.log(JSON.stringify({name,expectedExit,actual:c.result.exitCode,closed}));
 }
 [prepared.before,prepared.after,...binaries.map(x=>x.pin),...prepared.inputs.map(x=>x.pin)].forEach(e.checkPin);
 fs.writeFileSync(folder+'/VERIFIED.json',JSON.stringify({passed:true,actualNativeParser:true,baselineWithUiaRejected:true,fixedWithUiaAccepted:true,negativeGuardsPreserved:true,records,result:e.pin(folder+'/RESULT.json'),fullBuildPending:true,releaseReady:false},null,2)+'\n',{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
