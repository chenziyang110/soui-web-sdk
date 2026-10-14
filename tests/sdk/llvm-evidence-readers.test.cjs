'use strict';
// Exercise the real reader source against filesystem receipts. The observer
// dependency is controlled here; this never claims a real compile or GUI run.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),vm=require('node:vm');
const {createRequire}=require('node:module');
const evidenceFile=path.resolve(__dirname,'../../tools/internal/build-evidence.cjs');
const actualRequire=createRequire(evidenceFile);
test('LLVM schema2 identity adapter retains separate roots and original semantic predicates',()=>{
  // The native parser has its own complete raw fixtures. These controlled
  // dependencies isolate the actual adapter function, without any actor.
  const source=fs.readFileSync(path.resolve(__dirname,'../../tools/internal/llvm-build-observer.cjs'),'utf8');
  const body=source.slice(source.indexOf('function validateObservation('),source.indexOf('async function run('));
  const root='F:\\mock-sdk',proof={innerBuild:{pid:22,creationFileTime:'2'},outerObserver:{pid:11,creationFileTime:'1'}};
  const pin=name=>({path:'F:\\mock\\'+name,bytes:1,sha256:'a'.repeat(64)}),report={nativeToolchain:{requestFile:pin('request')},payloads:{},optionalPayloads:{}};
  const row={schema:'soui.standard-llvm-build-observation/v2',root,profile:'LLVM22MSVC142',architecture:'x64',configuration:'Release',sdkOnly:true,inputIdentity:'a',bridgeIdentity:'b',compilerVerified:true,
    observedCompilerId:'Clang',observedFrontend:'MSVC',observedVersion:'22.1.3',observedPointerBytes:8,qualificationPassed:false,releaseReady:false,
    debugCapture:{input:pin('input'),raw:pin('debug'),prepared:pin('prepared'),rootArguments:['fixed whole argv']},fastToolEvidence:proof,
    outerObserverCapture:{pid:11,creationFileTime:'1',raw:pin('outer'),context:pin('context'),ready:pin('ready'),finished:pin('finished'),wholeJobClosed:true,forced:false},
    buildCapture:{role:'inner-build',pid:22,creationFileTime:'2',raw:pin('debug'),input:pin('input'),wholeJobClosed:true,forced:false},requestFile:pin('request'),buildReport:pin('build'),
    compileRecords:[{source:pin('source'),object:pin('object'),headers:[pin('header')]}],ninjaCompileEdges:[{}],ninjaDependencyRecords:[{}],resourceRecords:[],binaryRecords:[{output:pin('archive'),inputObjects:[pin('object')],inputLibraries:[],inputResources:[]}],evidencePins:[],dependencyPins:[],frozenDependencyPins:[]};
  const need=(ok,message)=>{if(!ok)throw Error(message);};let scopeCalls=0;
  const context={path,need,profileName:'LLVM22MSVC142',isDeepStrictEqual:require('node:util').isDeepStrictEqual,
    validateNinjaScope(){scopeCalls++;},nativeNatural(){return {start:{pid:11,creationFileTime:'1'}};},samePin:(a,b)=>JSON.stringify(a)===JSON.stringify(b),match:p=>p,
    read:p=>p===pin('build').path?report:{sdkArchitecture:'x64',configuration:'Release',rootArguments:['fixed whole argv']},
    require:name=>{assert.equal(name,'./host64-build-debug-observer.cjs');return {recordedEvidence:()=>proof};}};
  const check=vm.runInNewContext(body+'\nvalidateObservation;',context);
  const run=value=>check(value,root,{architecture:'x64',configuration:'Release',sdkOnly:true},{InputIdentity:'a',BridgeIdentity:'b'},report,{result:{reportSha256:'a'.repeat(64)}},{},()=>{});
  assert.equal(run(row),true);assert.equal(scopeCalls,1);
  for(const change of [v=>v.outerObserverCapture.pid=22,v=>v.buildCapture.pid=11,v=>v.buildCapture.role='owned-start',v=>v.fastToolEvidence={...proof,innerBuild:{pid:23,creationFileTime:'2'}},v=>v.observedPointerBytes=4,v=>v.compileRecords=[],v=>v.ninjaCompileEdges=[],v=>v.binaryRecords=[],v=>v.compilerVerified=false,v=>v.schema='soui.standard-llvm-build-observation/v1']){
    const candidate=structuredClone(row);change(candidate);assert.throws(()=>run(candidate));
  }
});
function fixture(t,llvm){
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-evidence-readers-'));
  t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
  const put=(name,body)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,body);};
  const version='0.5.0-preview.1';put('VERSION',version);put('sdk/include/soui/version.h',`#define SOUI_SDK_VERSION_STRING "${version}"`);
  put('sdk/web/package.json',JSON.stringify({version}));put('sdk/web/package-lock.json',JSON.stringify({version,packages:{'':{version}}}));
  put('out/x64/CMakeCache.txt','SOUI_BUILD_WEBVIEW2:BOOL=ON\n');
  put('out/x64/sdk/renderers/webview2/Release/soui_webview2.lib','optional archive');
  for(const target of ['SouiStarter','SouiWorkbench','SouiModernStarter']){
    put(`out/x64/bin/Release/${target}/${target}.exe`,'executable '+target);
    put(`out/x64/bin/Release/${target}/resources/index.html`,'page');
    if(target!=='SouiModernStarter')put(`out/x64/bin/Release/${target}/node.dll`,'kernel');
  }
  const calls=[];let refused=true;
  const module={exports:{}},context={module,exports:module.exports,Buffer,console,process,
    require:name=>name==='./llvm-build-observer.cjs'?{verifyStandardBuild:(...args)=>{
      calls.push(args);if(refused)throw new Error('E_ACTUAL_OBSERVER_MISSING');return {compilerVerified:true};
    }}:actualRequire(name)};
  vm.runInNewContext(fs.readFileSync(evidenceFile,'utf8'),context,{filename:evidenceFile});
  const reader=module.exports,selection={};
  if(llvm){
    put('cmake/toolchains/SouiLLVM22.cmake','fixed product toolchain');
    for(const language of ['C','CXX'])put(`out/x64/CMakeFiles/4.2.3/CMake${language}Compiler.cmake`,'raw configuration');
    const discovery={Profile:'LLVM22MSVC142',Architecture:'x64',ActualCompilerVersion:null,GeneratorValidated:false,Qualified:false,
      InputIdentitySchema:'soui.llvm-profile-inputs/v2',InputIdentity:'a'.repeat(64),BridgeIdentity:'b'.repeat(64),
      ToolPins:Object.fromEntries(['CLANG','LINKER','ARCHIVER','RC','MT','NINJA','CMD','POWERSHELL'].map(key=>[key,{}])),
      RequiredInputPins:Array.from({length:17},()=>({})),BridgePins:[{},{}],CMakeDefinitions:Object.fromEntries(Array.from({length:12},(_,n)=>['k'+n,'v']))};
    const toolchain=path.join(root,'cmake/toolchains/SouiLLVM22.cmake'),bytes=fs.readFileSync(toolchain);
    selection.nativeToolchain='LLVM22MSVC142';selection.profileRequest=path.join(root,'out/x64/llvm-profile-request-Release.json');
    put('out/x64/llvm-profile-request-Release.json',JSON.stringify({schema:'soui.build-toolchain-request/v1',profile:'LLVM22MSVC142',
      architecture:'x64',discovery,toolchain:{path:toolchain,bytes:bytes.length,sha256:actualRequire('./delivery-identity.cjs').hash(bytes)}}));
  }
  reader.start(root,'x64','Release',false,selection);reader.finish(root,'x64','Release',false,selection);
  const build=reader.verifyBuild(root,'x64','Release',false,selection);
  const runs=reader.targets(root).flatMap(target=>(target==='SouiWorkbench'?['default','react']:['default']).map(scenario=>
    ({target,scenario,passed:true,exitCode:0,timedOut:false,log:'application-ready host-close-complete'})));
  put('out/x64/smoke-Release.json',JSON.stringify({schema:'soui.native-smoke/v2',buildId:build.buildId,sourceSha256:build.source.sha256,
    version,architecture:'x64',configuration:'Release',runs,passed:true,finishedAt:new Date().toISOString(),payloads:build.payloads}));
  put('out/x64/modern-smoke-Release.json',JSON.stringify({schema:'soui.modern-smoke/1',buildId:build.buildId,sourceSha256:build.source.sha256,
    version,architecture:'x64',configuration:'Release',target:'SouiModernStarter',payload:build.optionalPayloads.SouiModernStarter,
    pid:1,exitCode:0,timedOut:false,passed:true,finishedAt:new Date().toISOString(),log:'application-ready host-close-complete host-started;runtime='}));
  return {root,reader,build,selection,calls,allow:()=>{refused=false;}};
}
for(const operation of ['beginSmoke','finishSmoke','verifySmoke','verifyModernSmoke','stageApp'])
  test(operation+' requires actual LLVM standard proof even when selection is inferred from the current build',t=>{
    const f=fixture(t,true),invoke=()=>operation==='stageApp'?f.reader.stageApp(f.root,'x64','Release','SouiStarter',path.join(f.root,'stage')):
      f.reader[operation](f.root,'x64','Release');
    assert.throws(invoke,/E_ACTUAL_OBSERVER_MISSING/);assert.equal(f.calls.length,1);
    assert.equal(f.calls[0][4].nativeToolchain,'LLVM22MSVC142');assert.equal(f.calls[0][4].profileRequest,f.selection.profileRequest);
  });
test('an explicit opposite profile cannot reuse an LLVM smoke or deployment receipt',t=>{
  const f=fixture(t,true);f.allow();
  assert.throws(()=>f.reader.verifySmoke(f.root,'x64','Release',{nativeToolchain:'MSVC143'}),/profile differs/);
  assert.equal(f.calls.length,0);
});
test('existing MSVC smoke and deployment readers retain their original proof path',t=>{
  const f=fixture(t,false);f.reader.verifySmoke(f.root,'x64','Release');f.reader.verifyModernSmoke(f.root,'x64','Release');
  f.reader.stageApp(f.root,'x64','Release','SouiStarter',path.join(f.root,'stage'));assert.equal(f.calls.length,0);
});
