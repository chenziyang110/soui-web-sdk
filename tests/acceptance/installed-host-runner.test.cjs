'use strict';
// Actual orchestration with modeled tools: this is not native process evidence.
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),
  path=require('node:path'),vm=require('node:vm'),{isDeepStrictEqual}=require('node:util');
const source=fs.readFileSync(path.resolve(__dirname,'../../tools/internal/llvm-installed-acceptance.cjs'),'utf8');
const start=source.indexOf("async function run(suite,kind='acceptance'){");
const end=source.indexOf('\nfunction verify',start);
assert(start>=0&&end>start);
const body=source.slice(start,end);
function fixture(architecture,changed=false){
  const root=path.resolve('modeled-sdk'),suite=path.join(root,'out/acceptance/suite'),
    hostPath=path.join(root,'host64/OwnedProcessRunner.exe'),
    stockPath=architecture==='x64'?hostPath:path.join(root,'x86/OwnedProcessRunner.exe');
  const pin=p=>({path:p,bytes:10,sha256:'a'.repeat(64)});
  const inspect=p=>({...pin(p),requestedPath:p,...(changed&&p===hostPath?{sha256:'b'.repeat(64)}:{})});
  const prepared={pin:pin('prepared.json'),value:{outerRunner:pin(hostPath),executable:pin('Host64Observer.exe')}};
  const p={suite,architecture,configuration:'Release',build:path.join(suite,'consumer'),evidence:path.join(suite,'evidence'),
    command:'command.json',consumerRequest:'consumer.json',pending:'pending.json',record:'result.json',nativeLog:'native.log',sdkReport:'sdk.json'};
  const discovery={ToolPins:{POWERSHELL:{path:'pwsh.exe'}},NativeProcessPaths:{CMD:'cmd.exe'}};
  const sdk={prepared:{setup:{tools:{OwnedProcessRunner:stockPath}}},accepted:{inputs:pin('inputs.json')},
    selection:{requestFile:pin('request.json'),sdkAccepted:pin('accepted.json')},project:{}};
  const sdkFrozen={tools:[{role:'cmake',requestedPath:'cmake.exe'}],llvm:{debugBinding:{prepared:pin('prepared.json')}}};
  const args=['configure'],command={schema:'soui.installed-acceptance-llvm-configure/v1',args,cmake:'cmake.exe'};
  const saved=new Map(),calls=[],captured=new Map();
  function capture(name,runner){const result={modeledRunner:runner};captured.set(name,result);return result;}
  const context={path,process:{execPath:'node.exe'},owner:root,profile:'LLVM22MSVC142',isDeepStrictEqual,
    fs:{existsSync:()=>false,mkdirSync:()=>{},readFileSync:()=>Buffer.from('model')},
    read:file=>file==='inputs.json'?sdkFrozen:file===p.command?command:{architecture,configuration:'Release',nativeToolchain:{},schema:'soui.release-installed-consumer-pending/v1'},
    paths:()=>p,current:()=>sdk,profiles:{readRequest:()=>({value:{discovery},pin:pin('request.json')}),sameProfile:()=>{}},
    configureArguments:()=>args,norm:v=>path.resolve(v).toLowerCase(),
    need:(ok,message)=>{if(!ok)throw Error(message);},
    samePin:(a,b)=>a.path===b.path&&a.bytes===b.bytes&&a.sha256===b.sha256,
    pin,match:()=>{},controls:()=>[],dependencies:()=>({pins:[]}),environment:()=>({}),recheckFrozen:()=>{},
    write:(file,value)=>{saved.set(file,value);return pin(file);},
    captures:(_folder,name)=>({stdout:pin(name+'.stdout'),context:pin(name+'.context'),name}),recorded:rows=>captured.get(rows.name),
    natural:(c,expected)=>{assert.equal(c.modeledRunner,expected.runner.path);calls.push(expected);return {};},
    consumerDebug:()=>({}),validateObservation:()=>{},
    standard:{inspectTool:inspect,capture:async(_folder,name,_exe,_args,options)=>capture(name,options.runner)},
    debug:{loadPrepared:()=>prepared,makeInput:()=>({value:{nonce:'nonce'}}),
      captureBuild:async()=>({input:pin('debug-input.json'),raw:pin('debug.ndjson'),prepared:prepared.pin,capture:capture('build',hostPath)})}};
  vm.runInNewContext(body+';globalThis.invoke=run;',context);
  return {run:kind=>context.invoke(suite,kind),calls,saved,p,hostPath,stockPath};
}
for(const architecture of ['x64','x86'])for(const kind of ['acceptance','release'])
test(`${architecture} ${kind} uses prepared host runner only for debug build`,async()=>{
  const f=fixture(architecture);await f.run(kind);
  assert.equal(f.calls.length,3);
  assert.deepEqual(f.calls.map(x=>x.runner.path),[f.stockPath,f.hostPath,f.stockPath]);
  const record=f.saved.get(f.p.record);
  assert.equal(record.build.expected.runner.path,f.hostPath);
  assert.equal(record.configure.expected.runner.path,f.stockPath);
  assert.equal(record.observer.expected.runner.path,f.stockPath);
});
test('changed physical host runner is rejected before debug build',async()=>{
  const f=fixture('x86',true);await assert.rejects(f.run('acceptance'),/runner/i);
  assert.equal(f.saved.has(f.p.record),false);
});

// Exercise the real rereader's stage reconstruction independently of the writer.
const verifyStart=source.indexOf('function verifyWithInputs('),dualStart=source.indexOf('  if(dual){',verifyStart),
  dualEnd=source.indexOf('  const expectedCfg=',dualStart);
assert(verifyStart>=0&&dualStart>verifyStart&&dualEnd>dualStart);
function reread(mutation){
  const pin=file=>({path:file,bytes:10,sha256:'a'.repeat(64)}),host=pin('host64-runner.exe'),
    preparedPin=pin('prepared.json'),input={outerRunner:{...host},nonce:'nonce'},
    binding={input:pin('input.json'),prepared:{...preparedPin},observer:pin('observer.exe'),raw:pin('raw.ndjson')};
  const physical={...host,requestedPath:host.path};
  if(mutation==='input')input.outerRunner=pin('x86-runner.exe');
  if(mutation==='producer')binding.prepared.sha256='b'.repeat(64);
  if(mutation==='physical')physical.sha256='b'.repeat(64);
  const context={dual:true,cfg:{debugCapture:binding},expectedStages:{build:{args:['fixed-build']}},
    result:{build:{rows:{}}},root:'root',p:{},kind:'acceptance',sdk:{accepted:{inputs:pin('sdk.json')}},
    consumerDebug:()=>({verified:true}),recorded:()=>({}),
    read:file=>file==='input.json'?input:{llvm:{debugBinding:{prepared:preparedPin}}},
    samePin:(a,b)=>a.path===b.path&&a.bytes===b.bytes&&a.sha256===b.sha256,
    need:(ok,message)=>{if(!ok)throw Error(message);},
    debug:{loadPrepared:(file,hash)=>{assert.equal(file,preparedPin.path);assert.equal(hash,preparedPin.sha256);return {value:{outerRunner:host}};}},
    standard:{inspectTool:file=>{assert.equal(file,host.path);return physical;}}};
  vm.runInNewContext('let fastToolEvidence;'+source.slice(dualStart,dualEnd),context);
  return context.expectedStages.build;
}
test('reader reconstructs the independently prepared host runner',()=>assert.equal(reread().runner.path,'host64-runner.exe'));
for(const mutation of ['input','producer','physical'])
test('reader rejects changed '+mutation+' identity',()=>assert.throws(()=>reread(mutation),/runner|producer/i));
