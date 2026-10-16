'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const adapter=require('../../tools/internal/llvm-installed-acceptance.cjs');
const root=path.resolve(__dirname,'../..'),profile='LLVM22MSVC142',clone=value=>structuredClone(value);
const raw=(file,hash='a')=>({path:file,bytes:123,sha256:hash.repeat(64)});
// Logical DTOs only: a passed assertion is not compilation, tool or Owned evidence.
function fixture(architecture='x64',kind='release'){
  const suite=kind==='release'?path.join(root,'out/release-validation',architecture,'00000000000000000000000000000001'):
    path.join(root,'out/acceptance/00000000-0000-0000-0000-000000000001');
  const buildDirectory=path.join(suite,'consumer'),machine=architecture==='x64'?34404:332,key=kind==='release'?'releaseConsumer':'installedConsumer';
  const capture=Object.fromEntries(['raw','context','ready','finished'].map(name=>[name,raw(path.join(suite,name+'.json'))]));
  const archive=raw(kind==='release'?path.join(root,'out/install',architecture,'lib/Release/soui_sdk.lib'):path.join(suite,'install/lib/Release/soui_sdk.lib'));
  const cfg={root,architecture,configuration:'Release',buildDirectory,inputIdentity:'sdk-owner-full-id',bridgeIdentity:'sdk-owner-bridge-id',
    requestFile:raw(path.join(root,'out',`sdk-${architecture}`,'llvm-profile-request-Release.json')),
    buildReport:raw(path.join(root,'out',`sdk-${architecture}`,'build-Release.json')),buildCapture:capture,dependencyPins:[],
    [key]:{schema:`soui.installed-${kind==='release'?'release':'acceptance'}-llvm-context/v1`,suiteDirectory:suite,
      pending:raw(path.join(suite,kind==='release'?'consumer-pending.json':'pending.json')),
      requestFile:raw(path.join(buildDirectory,'llvm-profile-request-Release.json')),sdkAccepted:raw(path.join(root,'accepted.json'))}};
  const subject={start:{pid:42,creationFileTime:'123456789'}},build={buildId:'sdk-build',source:{sha256:'sdk-source'}},object=raw(path.join(buildDirectory,'main.obj'));
  const row={schema:`soui.installed-${kind==='release'?'release':'acceptance'}-llvm-build-observation/v1`,profile,root,architecture,configuration:'Release',sdkOnly:false,
    inputIdentity:cfg.inputIdentity,bridgeIdentity:cfg.bridgeIdentity,compilerVerified:true,observedCompilerId:'Clang',observedFrontend:'MSVC',observedVersion:'22.1.3',
    observedPointerBytes:architecture==='x64'?8:4,completeToolchainProvenance:false,kernelQualified:false,abiQualified:false,win7Qualified:false,qualificationPassed:false,releaseReady:false,
    buildCapture:{...capture,...subject.start,wholeJobClosed:true,forced:false},requestFile:cfg.requestFile,buildReport:cfg.buildReport,
    [key]:{sdkBuildId:build.buildId,sdkSourceSha256:build.source.sha256,sdkAccepted:cfg[key].sdkAccepted,pending:cfg[key].pending,
      consumerRequest:cfg[key].requestFile,installedSdkArchive:archive},compilerFacts:[{language:'CXX'}],
    compileRecords:[{source:raw(kind==='release'?path.join(root,'tests/package-consumer/main.cpp'):path.join(suite,'consumer-source/main.cpp')),object,machine,reusedFrom:null}],
    currentCompileCount:1,currentBinaryCount:1,ninjaCompileEdges:[{}],ninjaDependencyRecords:[{}],frozenDependencyPins:[],
    binaryRecords:[{role:'LINKER',output:raw(kind==='release'?path.join(buildDirectory,'Release/soui_consumer.exe'):path.join(buildDirectory,'bin/Release/SouiAcceptance.exe')),
      machine,reusedFrom:null,inputLibraries:[archive],inputObjects:[object]}]};
  return {row,cfg,subject,build,archive,key,suite};
}
const accepts=f=>adapter.validateObservationFacts(f.row,f.cfg,f.subject,f.build,f.archive);

test('dual root consumer observations retain both architectures and both explicit contexts',()=>{
 for(const architecture of ['x86','x64'])for(const kind of ['release','acceptance']){
  const f=fixture(architecture,kind),rawDebug=raw(path.join(f.suite,'host64-debug.ndjson')),input=raw(path.join(f.suite,'host64-inputs.json'));
  f.cfg.schema='soui.standard-llvm-observer-inputs/v2';f.cfg.debugCapture={raw:rawDebug,input};
  f.cfg.fastToolEvidence={innerBuild:{pid:43,creationFileTime:'123456790'},outerObserver:f.subject.start,experimentalLifecycleObserved:true};
  f.row.schema=f.row.schema.replace('/v1','/v2');f.row.outerObserverCapture=f.row.buildCapture;
  f.row.buildCapture={role:'inner-build',...f.cfg.fastToolEvidence.innerBuild,raw:rawDebug,input,naturalRootExitCode:0,wholeJobClosed:true,forced:false};
  f.row.originalPollingCapture=f.cfg.buildCapture;f.row.debugCapture=f.cfg.debugCapture;f.row.fastToolEvidence=f.cfg.fastToolEvidence;
  assert.equal(accepts(f),true);f.row.schema=f.row.schema.replace(kind,kind==='release'?'acceptance':'release');assert.throws(()=>accepts(f));
 }
});
test('fixed package and ordinary acceptance CXX contexts remain distinct and support both architectures',()=>{
  for(const architecture of ['x86','x64'])for(const kind of ['release','acceptance'])assert.equal(accepts(fixture(architecture,kind)),true);
});
test('release context cannot relabel acceptance output or mix either context role',()=>{
  for(const alter of [f=>f.row.schema='soui.installed-acceptance-llvm-build-observation/v1',
    f=>{f.row.installedConsumer=f.row.releaseConsumer;delete f.row.releaseConsumer;},
    f=>f.row.installedConsumer=f.row.releaseConsumer,f=>f.cfg.installedConsumer=f.cfg.releaseConsumer,
    f=>f.row.compileRecords[0].source.path=path.join(f.suite,'consumer-source/main.cpp'),
    f=>f.row.binaryRecords[0].output.path=path.join(f.suite,'consumer/bin/Release/SouiAcceptance.exe')]){
    const f=fixture();alter(f);assert.throws(()=>accepts(f));
  }
});
test('package consumer requires current SDK archive own request and frozen pending data',()=>{
  for(const alter of [f=>f.row.releaseConsumer.sdkBuildId='old',f=>f.row.releaseConsumer.sdkSourceSha256='old-source',
    f=>f.row.releaseConsumer.consumerRequest=f.row.requestFile,f=>f.row.releaseConsumer.pending={...f.row.releaseConsumer.pending,bytes:124},
    f=>f.row.releaseConsumer.installedSdkArchive={...f.archive,sha256:'b'.repeat(64)},f=>f.row.binaryRecords[0].inputLibraries=[],
    f=>f.row.frozenDependencyPins=[raw(path.join(root,'unfrozen.h'))]]){const f=fixture();alter(f);assert.throws(()=>accepts(f));}
});
test('actual whole Job subject and exact capture bytes cannot be replaced by forced close assertions',()=>{
  for(const alter of [f=>f.row.buildCapture.forced=true,f=>f.row.buildCapture.wholeJobClosed=false,
    f=>f.row.buildCapture.pid++,f=>f.row.buildCapture.creationFileTime='987',
    f=>f.row.buildCapture.raw={...f.row.buildCapture.raw,sha256:'b'.repeat(64)}]){const f=fixture();alter(f);assert.throws(()=>accepts(f));}
});
test('CXX package graph must freshly compile its own object and link the accepted imported archive',()=>{
  for(const alter of [f=>f.row.compilerFacts.push({language:'C'}),f=>f.row.compileRecords=[],
    f=>f.row.currentCompileCount=0,f=>f.row.currentBinaryCount=0,f=>f.row.compileRecords[0].reusedFrom={},
    f=>f.row.compileRecords[0].machine=332,f=>f.row.binaryRecords[0].inputObjects=[raw(path.join(root,'foreign.obj'))],
    f=>f.row.binaryRecords[0].reusedFrom={},f=>f.row.binaryRecords[0].role='ARCHIVER']){const f=fixture();alter(f);assert.throws(()=>accepts(f));}
});
test('release imported observation retains exact compiler full IDs and unqualified higher scope',()=>{
  for(const [key,value] of [['profile','MSVC143'],['inputIdentity','stock-id'],['bridgeIdentity','stock-bridge'],
    ['observedCompilerId','MSVC'],['observedVersion','22.1.2'],['observedPointerBytes',4],['configuration','Debug']]){
    const f=fixture();f.row[key]=value;assert.throws(()=>accepts(f),key);
  }
  for(const key of ['completeToolchainProvenance','kernelQualified','abiQualified','win7Qualified','qualificationPassed','releaseReady']){
    const f=fixture();f.row[key]=true;assert.throws(()=>accepts(f),key);
  }
});
test('release configure getter preserves twelve authenticated definitions and fixed SDK owner paths',()=>{
  const names=['SOUI_NATIVE_TOOLCHAIN','SOUI_LLVM_ARCHITECTURE','SOUI_LLVM_ROOT','SOUI_MSVC142_ROOT','SOUI_WINDOWS_SDK_ROOT','SOUI_NINJA_EXECUTABLE',
    'SOUI_WINDOWS_CMD_EXECUTABLE','SOUI_LLVM_INPUT_IDENTITY','SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE','SOUI_TOOL_IDENTITY_BRIDGE','SOUI_TOOL_IDENTITY_HELPER','SOUI_LLVM_BRIDGE_IDENTITY'];
  for(const architecture of ['x86','x64']){
    const f=fixture(architecture),request={value:{architecture,discovery:{CMakeDefinitions:Object.fromEntries(names.map((name,index)=>[name,'input-'+index]))},toolchain:{path:'foreign-root/toolchain.cmake'}}};
    const args=adapter.configureArguments(root,f.suite,request,'release');
    assert.deepEqual(args.slice(0,6),['-S',path.join(root,'tests/package-consumer'),'-B',path.join(f.suite,'consumer'),'-G','Ninja Multi-Config']);
    assert.ok(args.includes('--fresh'));assert.equal(args.filter(arg=>/^-DSOUI_[A-Z0-9_]+:STRING=/.test(arg)).length,12);
    assert.ok(args.includes('-DCMAKE_TOOLCHAIN_FILE:FILEPATH='+path.join(root,'cmake/toolchains/SouiLLVM22.cmake').replaceAll('\\','/')));
    assert.ok(args.includes('-DSouiSdk_DIR='+path.join(root,'out/install',architecture)+'/lib/cmake/SouiSdk'));
    assert.ok(!args.some(arg=>arg.includes('foreign-root')||arg.includes('SOUI_ACCEPTANCE_WEB_ROOT')));
  }
});
test('release path getter rejects a foreign owner architecture suite or ordinary acceptance namespace',()=>{
  const f=fixture();assert.equal(adapter.releasePaths(root,f.suite,'x64','Release').executable,path.join(f.suite,'consumer/Release/soui_consumer.exe'));
  for(const args of [[path.dirname(root),f.suite,'x64','Release'],[root,f.suite,'x86','Release'],[root,f.suite,'arm64','Release'],
    [root,path.join(root,'out/acceptance/00000000-0000-0000-0000-000000000001'),'x64','Release'],[root,f.suite,'x64','Other']]){
    assert.throws(()=>adapter.releasePaths(...args));
  }
});
