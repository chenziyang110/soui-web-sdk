'use strict';
// Exercise the real reader source against filesystem receipts. The observer
// dependency is controlled here; this never claims a real compile or GUI run.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),vm=require('node:vm');
const {createRequire}=require('node:module');
const evidenceFile=path.resolve(__dirname,'../../tools/internal/build-evidence.cjs');
const actualRequire=createRequire(evidenceFile);
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
