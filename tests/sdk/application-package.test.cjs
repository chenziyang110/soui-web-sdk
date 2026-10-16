'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),Module=require('node:module');
const test=require('node:test'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
const selected=root;
function load(name){
  const filename=path.join(root,name),module=new Module(filename);module.filename=filename;module.paths=Module._nodeModulePaths(path.dirname(filename));
  module._compile(fs.readFileSync(path.join(selected,name),'utf8'),filename);return module.exports;
}
const kit=load('tools/internal/devkit-command.cjs');
test('modern application packaging selects the public profile without changing the default packages',()=>{
  const modern=kit.parseArgs(['package','--kind','applications','--profile','modern','--architecture','x86']);
  const plan=kit.planCommand(modern,root,'C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe');
  assert.equal(plan.args[plan.args.indexOf('-Profile')+1],'modern');
  assert.equal(plan.args[plan.args.indexOf('-Architecture')+1],'x86');
  const normal=kit.planCommand(kit.parseArgs(['package','--kind','applications']),root,'powershell.exe');
  assert(!normal.args.includes('-Profile'));assert(normal.args.includes('x64'));
});
test('package profile cannot silently select source or SDK packaging and rejects unknown values',()=>{
  for(const args of [ ['package','--profile','modern'],['package','--kind','sdk','--output','C:/package','--profile','compatibility'],
    ['package','--kind','applications','--profile','unknown'],['package','--kind','applications','--profile','modern','--profile','modern']])
    assert.throws(()=>kit.parseArgs(args),error=>error.code==='E_ARGUMENT');
});
function stageFixture(target,{generated=false,proofFailure=false,existing=false}={}){
  const calls=[],copied=[];const base='C:/Fixture',destination='C:/Staged';
  const payload={files:[{path:target+'.exe',bytes:10},{path:'resources/index.html',bytes:5}],bytes:15,sha256:'x'};
  const build={version:'0.5.0',buildId:'synthetic',source:{sha256:'source'},payloads:{SouiStarter:payload,GeneratedModern:payload},optionalPayloads:{SouiModernStarter:payload}};
  const fakeFs={existsSync:file=>file===path.resolve(base,'project.json')?generated:file===path.resolve(destination)?existing:false,
    mkdirSync:()=>{},copyFileSync:(from,to)=>copied.push({from,to})};
  const fakeId={safePath:(...args)=>path.resolve(...args),readJson:()=>({nativeTarget:'GeneratedModern',runtimeProfile:'modern'}),
    verifyPayload:(directory,name,expected,backend)=>{calls.push(['payload',directory,name,backend]);assert.equal(expected,payload);return payload;}};
  const module={exports:{}};
  const originalRequire=Module.createRequire(path.join(root,'tools/internal/build-evidence.cjs'));
  const localRequire=name=>name==='node:fs'?fakeFs:name==='./delivery-identity.cjs'?fakeId:originalRequire(name);
  const source=fs.readFileSync(path.join(selected,'tools/internal/build-evidence.cjs'),'utf8')+
    '\nverifySmoke = (...args) => verifier("compatibility", args); verifyModernSmoke = (...args) => verifier("modern", args);';
  new vm.Script(source).runInNewContext({module,require:localRequire,process,Buffer,__dirname:path.join(root,'tools/internal'),
    verifier:(kind,args)=>{calls.push([kind,...args]);if(proofFailure)throw Error('Actual startup proof rejected');return {build};}});
  return {run:()=>module.exports.stageApp(base,'x64','Release',target,destination),calls,copied};
}
test('optional modern payload uses its modern startup proof and omits the compatibility kernel requirement',()=>{
  const f=stageFixture('SouiModernStarter'),result=f.run();
  assert.equal(f.calls[0][0],'modern');assert.equal(f.calls[1][3],'webview2');assert.equal(f.copied.length,2);
  assert.equal(result.payload.bytes,15);assert.equal(result.categories.executable,10);assert.equal(result.releaseReady,false);
});
test('missing modern startup proof stops before copying any payload bytes',()=>{
  const f=stageFixture('SouiModernStarter',{proofFailure:true});assert.throws(f.run,/startup proof rejected/);assert.equal(f.copied.length,0);
});
test('compatibility staging retains its existing proof and kernel validation',()=>{
  const f=stageFixture('SouiStarter');f.run();assert.equal(f.calls[0][0],'compatibility');assert.equal(f.calls[1][3],'miniblink');
});
test('generated modern app staging also requires the actual modern startup proof',()=>{
  const f=stageFixture('GeneratedModern',{generated:true});f.run();assert.equal(f.calls[0][0],'modern');assert.equal(f.calls[1][3],'webview2');
});
test('unrecognized and inapplicable optional targets never copy data',()=>{
  for(const [target,generated]of [['Unknown',false],['SouiModernStarter',true]]){const f=stageFixture(target,{generated});assert.throws(f.run,/Unknown application target/);assert.equal(f.copied.length,0);}
});
test('existing staging output is preserved after successful proof validation',()=>{
  const f=stageFixture('SouiModernStarter',{existing:true});assert.throws(f.run,/stage already exists/);assert.equal(f.copied.length,0);
});
