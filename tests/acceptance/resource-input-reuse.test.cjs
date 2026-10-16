'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict'),test=require('node:test'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..');
const source=fs.readFileSync(path.join(root,'tests/acceptance/runner.cjs'),'utf8');
function section(text,start,end){const a=text.indexOf(start),b=text.indexOf(end,a+start.length);assert(a>=0&&b>a,'Actual verified-input functions required');return text.slice(a,b);}
const body=section(source,'function verifyInputs(suite) {','function finishLongCapture(');
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function fixture(llvm=true){
 const kernel=Buffer.from('original-kernel'),build={schema:'soui.acceptance-build/v3',buildId:'build-1',sourceSha256:'source-1',architecture:'x64',configuration:'Release',
  web:{typecheck:true},payload:{sha256:'payload-1'},installedSdk:{sha256:'install-1'},consumerSource:{sha256:'consumer-1'},kernel:{sha256:digest(kernel)},...(llvm?{nativeToolchain:{profile:'LLVM22MSVC142'}}:{})};
 const state={build,sdk:{buildId:build.buildId,source:{sha256:build.sourceSha256}},web:build.web,kernel,payload:'payload-1',install:'install-1',consumer:'consumer-1',llvmCalls:0,msvcCalls:0};
 const context={root:'C:\\sdk',path:path.win32,fs:{readFileSync:()=>state.kernel},
  identity:{readJson:()=>state.build,hash:digest,walk:()=>[],snapshot:dir=>({sha256:dir.endsWith('install')?state.install:state.consumer})},
  verifyWeb:()=>state.web,payload:()=>({sha256:state.payload}),
  builds:{verifyBuild:()=>{state.msvcCalls++;return state.sdk;}},
  require:name=>{assert.equal(name,'../../tools/internal/llvm-installed-acceptance.cjs');return {verifyWithInputs:()=>{state.llvmCalls++;if(state.failure)throw Error(state.failure);return {sdk:{build:state.sdk},consumer:{path:'verified-consumer'}};}};}};
 vm.runInNewContext(body+';globalThis.api={verifyInputs,verify};',context);
 return {state,api:context.api};
}
test('each LLVM invocation obtains its own fully verified inputs and legacy return stays unchanged',()=>{
 const f=fixture();assert.strictEqual(f.api.verifyInputs('C:\\suite').build,f.state.build);assert.equal(f.state.llvmCalls,1);
 assert.strictEqual(f.api.verify('C:\\suite'),f.state.build);assert.equal(f.state.llvmCalls,2);assert.equal(f.state.msvcCalls,0);
 f.state.failure='Changed actual SDK input';assert.throws(()=>f.api.verifyInputs('C:\\suite'),/Changed actual SDK input/);assert.equal(f.state.llvmCalls,3);
});
test('MSVC verification still runs freshly on every invocation',()=>{
 const f=fixture(false);const result=f.api.verifyInputs('C:\\suite');assert.equal(result.installed,null);assert.strictEqual(result.sdk,f.state.sdk);
 f.api.verify('C:\\suite');assert.equal(f.state.msvcCalls,2);assert.equal(f.state.llvmCalls,0);
});
for(const [name,mutate,expected]of [
 ['SDK identity',s=>s.sdk={...s.sdk,buildId:'changed'},/source, SDK build/],
 ['SDK source',s=>s.sdk={...s.sdk,source:{sha256:'changed'}},/source, SDK build/],
 ['runtime payload',s=>s.payload='changed',/runtime drift/],
 ['installed tree',s=>s.install='changed',/Installed SDK drift/],
 ['consumer source',s=>s.consumer='changed',/consumer source drift/],
 ['kernel input',s=>s.kernel=Buffer.from('changed'),/candidate input drift/],
 ['Web result',s=>s.web={changed:true},/Web build record drift/]
])test(name+' drift after a successful invocation is rejected',()=>{
 const f=fixture();f.api.verifyInputs('C:\\suite');mutate(f.state);assert.throws(()=>f.api.verifyInputs('C:\\suite'),expected);
});
test('installed reader legacy wrapper preserves shape and explicit release selection',()=>{
 const text=fs.readFileSync(path.join(root,'tools/internal/llvm-installed-acceptance.cjs'),'utf8');
 const wrapper=section(text,"function verify(root,suite,selection,kind='acceptance'){",'function runtimeRunner(');
 const calls=[],build={},consumer={},context={verifyWithInputs:(...args)=>{calls.push(args);return {sdk:{build},consumer};}};
 vm.runInNewContext(wrapper+';globalThis.run=verify;',context);
 const selection={};const result=context.run('root','suite',selection,'release');
 assert.deepEqual(Object.keys(result),['build','consumer']);assert.strictEqual(result.build,build);assert.strictEqual(result.consumer,consumer);
 assert.deepEqual(calls,[['root','suite',selection,'release']]);
});
test('complete binding checks obtain fresh verified input sets without a second independent SDK lookup',()=>{
 const text=fs.readFileSync(path.join(root,'tests/acceptance/resource-file-bindings.cjs'),'utf8');
 const body=section(text,'function currentRecords(suite) {','function requestFor(');
 const source={sha256:'source-1',bytes:1,files:[{path:'source'}]},sdk={source},build={architecture:'x64',configuration:'Release',nativeToolchain:{profile:'LLVM22MSVC142'}},installed={actual:'verification-result'};
 const publicArtifacts={sha256:'web-1',files:[]};let calls=0,sourceNow=source;
 const context={path:path.win32,sdkRoot:'C:\\sdk',same:(a,b)=>JSON.stringify(a)===JSON.stringify(b),need:(ok,message)=>{if(!ok)throw Error(message);},
  readJson:file=>({pin:{path:file},record:file.endsWith('build.json')?build:file.endsWith('build-Release.json')?sdk:file.endsWith('web-sdk.json')?{artifacts:publicArtifacts}:{}}),
  require:name=>{assert.equal(name,'./runner.cjs');return {verifyInputs:()=>{calls++;return {build,sdk,installed};}};},
  validateRecordSet:()=>({sourceSha256:source.sha256}),
  identity:{sourceSnapshot:()=>sourceNow,walk:()=>[],snapshot:()=>publicArtifacts},
  nativeInputs:(suite,b,s,proof)=>{assert.strictEqual(b,build);assert.strictEqual(s,sdk);assert.strictEqual(proof,installed);return {verifiedWithinThisCall:true};}};
 vm.runInNewContext(body+';globalThis.run=currentRecords;',context);
 context.run('C:\\suite');context.run('C:\\suite');assert.equal(calls,2);
 sourceNow={...source,sha256:'changed'};assert.throws(()=>context.run('C:\\suite'),/Complete current SDK source differs/);assert.equal(calls,3);
});
