'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {validateObservationFacts,configureArguments}=require('../../tools/internal/llvm-installed-acceptance.cjs');
const root=path.resolve(__dirname,'../..'),suite=path.join(root,'out/acceptance/00000000-0000-0000-0000-000000000001');
const raw=(file,hash='a')=>({path:file,bytes:123,sha256:hash.repeat(64)}),clone=value=>JSON.parse(JSON.stringify(value));
// Every value below is a logical DTO fixture, never file/compiler/Owned proof.
function fixture(architecture='x64'){
  const machine=architecture==='x64'?34404:332,buildDirectory=path.join(suite,'consumer');
  const capture={raw:raw(path.join(suite,'build.ndjson')),context:raw(path.join(suite,'build.context.json')),
    ready:raw(path.join(suite,'ready.json')),finished:raw(path.join(suite,'finished.json'))};
  const archive=raw(path.join(suite,'install/lib/Release/soui_sdk.lib'));
  const cfg={root,architecture,configuration:'Release',buildDirectory,inputIdentity:'sdk-owner-full-id',bridgeIdentity:'sdk-owner-bridge-id',
    requestFile:raw(path.join(root,'out',`sdk-${architecture}`,'llvm-profile-request-Release.json')),
    buildReport:raw(path.join(root,'out',`sdk-${architecture}`,'build-Release.json')),buildCapture:capture,dependencyPins:[],
    installedConsumer:{suiteDirectory:suite,pending:raw(path.join(suite,'pending.json')),
      requestFile:raw(path.join(buildDirectory,'llvm-profile-request-Release.json')),sdkAccepted:raw(path.join(root,'accepted.json'))}};
  const subject={start:{pid:42,creationFileTime:'123456789'}},build={buildId:'fixture-sdk-id',source:{sha256:'fixture-source'}};
  const object=raw(path.join(buildDirectory,'main.cpp.obj'));
  const row={schema:'soui.installed-acceptance-llvm-build-observation/v1',profile:'LLVM22MSVC142',root,architecture,configuration:'Release',sdkOnly:false,
    inputIdentity:cfg.inputIdentity,bridgeIdentity:cfg.bridgeIdentity,compilerVerified:true,observedCompilerId:'Clang',observedFrontend:'MSVC',
    observedVersion:'22.1.3',observedPointerBytes:architecture==='x64'?8:4,completeToolchainProvenance:false,kernelQualified:false,
    abiQualified:false,win7Qualified:false,qualificationPassed:false,releaseReady:false,
    buildCapture:{...capture,...subject.start,wholeJobClosed:true,forced:false},requestFile:cfg.requestFile,buildReport:cfg.buildReport,
    installedConsumer:{sdkBuildId:build.buildId,sdkSourceSha256:build.source.sha256,sdkAccepted:cfg.installedConsumer.sdkAccepted,
      pending:cfg.installedConsumer.pending,consumerRequest:cfg.installedConsumer.requestFile,installedSdkArchive:archive},
    compilerFacts:[{language:'CXX'}],compileRecords:[{source:raw(path.join(suite,'consumer-source/main.cpp')),object,machine,reusedFrom:null}],
    currentCompileCount:1,currentBinaryCount:1,ninjaCompileEdges:[{}],ninjaDependencyRecords:[{}],frozenDependencyPins:[],
    binaryRecords:[{role:'LINKER',output:raw(path.join(buildDirectory,'bin/Release/SouiAcceptance.exe')),machine,reusedFrom:null,
      inputLibraries:[archive],inputObjects:[object]}]};
  return {row,cfg,subject,build,archive};
}
const accepts=value=>validateObservationFacts(value.row,value.cfg,value.subject,value.build,value.archive);
test('dedicated fresh CXX import DTOs preserve unqualified scope for both target architectures',()=>{
  for(const architecture of ['x86','x64']){const f=fixture(architecture);assert.equal(accepts(f),true);
    for(const key of ['completeToolchainProvenance','kernelQualified','abiQualified','win7Qualified','qualificationPassed','releaseReady'])assert.equal(f.row[key],false);}
});
test('normal graph and wrong owner profile compiler or full identity cannot substitute for installed output',()=>{
  for(const [key,value] of [['schema','soui.standard-llvm-build-observation/v1'],['profile','MSVC143'],['root',path.dirname(root)],
    ['architecture','arm64'],['configuration','Debug'],['sdkOnly',true],['inputIdentity','stock-id'],['bridgeIdentity','stock-bridge'],
    ['observedCompilerId','MSVC'],['observedVersion','22.1.2'],['observedPointerBytes',4]]){
    const f=fixture();f.row[key]=value;assert.throws(()=>accepts(f),key);
  }
});
test('published build subject frozen captures and natural closure cannot be replaced by DTO claims',()=>{
  for(const mutate of [row=>row.buildCapture.pid++,row=>row.buildCapture.creationFileTime='987',row=>row.buildCapture.forced=true,
    row=>row.buildCapture.wholeJobClosed=false,row=>row.buildCapture.raw={...row.buildCapture.raw,sha256:'b'.repeat(64)}]){
    const f=fixture();f.row=clone(f.row);mutate(f.row);assert.throws(()=>accepts(f));
  }
});
test('current SDK import identity and independently owned consumer request are required',()=>{
  for(const mutate of [row=>row.installedConsumer.sdkBuildId='old-sdk',row=>row.installedConsumer.sdkSourceSha256='old-source',
    row=>row.installedConsumer.installedSdkArchive={...row.installedConsumer.installedSdkArchive,sha256:'b'.repeat(64)},
    row=>row.installedConsumer.consumerRequest=row.requestFile,row=>row.installedConsumer.pending={...row.installedConsumer.pending,bytes:124},
    row=>row.binaryRecords[0].inputLibraries=[]]){const f=fixture();f.row=clone(f.row);mutate(f.row);assert.throws(()=>accepts(f));}
});
test('empty extra foreign reused or disconnected compile and link DTOs fail the fresh ordinary graph',()=>{
  for(const mutate of [row=>row.compilerFacts=[],row=>row.compilerFacts.push({language:'C'}),row=>row.compileRecords=[],
    row=>row.currentCompileCount=0,row=>row.currentBinaryCount=0,row=>row.compileRecords[0].reusedFrom={},
    row=>row.compileRecords[0].source.path=path.join(suite,'consumer-source/other.cpp'),row=>row.compileRecords[0].machine=332,
    row=>row.binaryRecords[0].inputObjects=[raw(path.join(suite,'foreign.obj'))],row=>row.binaryRecords[0].reusedFrom={},
    row=>row.binaryRecords[0].role='ARCHIVER',row=>row.binaryRecords[0].output.path=path.join(suite,'other.exe')]){
    const f=fixture();mutate(f.row);assert.throws(()=>accepts(f));
  }
});
test('installed observations cannot elevate toolchain kernel ABI OS or release qualification',()=>{
  for(const key of ['completeToolchainProvenance','kernelQualified','abiQualified','win7Qualified','qualificationPassed','releaseReady']){
    const f=fixture();f.row[key]=true;assert.throws(()=>accepts(f),key);
  }
  const f=fixture();f.row.frozenDependencyPins=[raw(path.join(root,'unfrozen.h'))];assert.throws(()=>accepts(f));
});
test('configure argv preserves all twelve discovery definitions and fixed owner toolchain paths',()=>{
  const names=['SOUI_NATIVE_TOOLCHAIN','SOUI_LLVM_ARCHITECTURE','SOUI_LLVM_ROOT','SOUI_MSVC142_ROOT','SOUI_WINDOWS_SDK_ROOT',
    'SOUI_NINJA_EXECUTABLE','SOUI_WINDOWS_CMD_EXECUTABLE','SOUI_LLVM_INPUT_IDENTITY','SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE',
    'SOUI_TOOL_IDENTITY_BRIDGE','SOUI_TOOL_IDENTITY_HELPER','SOUI_LLVM_BRIDGE_IDENTITY'];
  for(const architecture of ['x86','x64']){
    const definitions=Object.fromEntries(names.map((name,index)=>[name,'fixture-'+index]));
    const request={value:{architecture,discovery:{CMakeDefinitions:definitions},toolchain:{path:'untrusted-path-is-not-selected'}}};
    const args=configureArguments(root,suite,request);
    assert.ok(args.includes('--fresh'));assert.ok(args.includes('Ninja Multi-Config'));
    assert.equal(args.filter(arg=>/^\-DSOUI_[A-Z0-9_]+:STRING=/.test(arg)).length,12);
    assert.ok(args.includes('-DCMAKE_TOOLCHAIN_FILE:FILEPATH='+path.join(root,'cmake/toolchains/SouiLLVM22.cmake').replaceAll('\\','/')));
    assert.ok(!args.some(arg=>arg.includes('untrusted-path-is-not-selected')));
  }
});
test('Source retains resource phase classification and MSVC default while binding explicit LLVM resource entry',()=>{
  const source=fs.readFileSync(path.join(__dirname,'runner.cjs'),'utf8');
  assert.ok(source.includes('evidence.finalVerified===true && evidence.failure===null'));
  assert.ok(source.includes('inputRecheckDiagnostic:inputs || null'));
  assert.ok(!source.includes('LLVM ResourceCapture is unsupported'));
  assert.ok(source.includes('const frozen=resourceFiles?resourceFiles.freeze(suite,output):null'));
  assert.ok(source.includes('nativeToolchain===undefined?builds.verifyBuild(root,architecture,configuration,true)'));
  const helper=fs.readFileSync(path.join(root,'tools/internal/llvm-installed-acceptance.cjs'),'utf8');
  assert.ok(helper.includes('standard.capture(p.evidence,name,executable,argv'));
  assert.ok(!helper.includes('launchOwned('));assert.ok(!helper.includes('spawnSync('));
  assert.ok(helper.includes('profiles.sameProfile(sdk.project,request)'));
  assert.ok(helper.includes('llvm.validatePreparedProfile(root,v,prepared,requestFile)'));
});
