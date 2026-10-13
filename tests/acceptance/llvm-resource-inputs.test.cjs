'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {validateNativeSelection,mergeNativePins}=require('./resource-file-bindings.cjs');
const {completeResourceAnalysis}=require('./resource-validation.cjs');
const root=path.resolve(__dirname,'../..'),suite=path.join(root,'out/acceptance/00000000-0000-0000-0000-000000000001');
const pin=file=>({path:file,bytes:123,sha256:'a'.repeat(64)}),clone=value=>JSON.parse(JSON.stringify(value));
// Logical pin/record fixtures only. These tests never certify a real request or compiler capture.
function fixture(architecture='x64') {
  const stock=path.join(root,'separate-stock-owner'),dir=path.join(stock,'out',architecture==='x64'?'benchmark-protocol':'benchmark-protocol-x86');
  const selection={profile:'LLVM22MSVC142',requestFile:pin(path.join(root,`out/sdk-${architecture}/llvm-profile-request-Release.json`)),
    sdkAccepted:pin(path.join(root,`out/sdk-${architecture}/standard-llvm-Release.json`)),validationTools:{root:stock,
      preparation:pin(path.join(dir,'measurement-tools.json')),requestFile:pin(path.join(dir,'llvm-profile-request-Release.json')),
      inputIdentity:'b'.repeat(64),bridgeIdentity:'c'.repeat(64),sourceFingerprint:'d'.repeat(64)}};
  return {build:{architecture,configuration:'Release',nativeToolchain:{...clone(selection),consumer:pin(path.join(suite,'consumer-llvm-build.json'))}},
    pending:{nativeToolchain:clone(selection)},sdkBuild:{nativeToolchain:{profile:'LLVM22MSVC142',requestFile:clone(selection.requestFile)}}};
}
const selected=f=>validateNativeSelection(f.build,f.pending,f.sdkBuild,suite);
test('absence stays MSVC and a present SDK LLVM record cannot silently fall back',()=>{
  assert.equal(validateNativeSelection({}, {}, {}),null);
  assert.throws(()=>validateNativeSelection({}, {}, {nativeToolchain:{profile:'LLVM22MSVC142'}}),/fall back/);
  assert.throws(()=>validateNativeSelection({}, {nativeToolchain:{}}, {}),/fall back/);
});
test('both architectures retain explicit separate SDK stock and consumer record roles without qualification',()=>{
  for(const arch of ['x86','x64']){const f=fixture(arch),selection=selected(f);
    assert.deepEqual(selection,f.build.nativeToolchain);
    assert.notEqual(selection.requestFile.path,selection.validationTools.requestFile.path);
    assert.notEqual(selection.requestFile.path,selection.consumer.path);
    assert.notEqual(root,selection.validationTools.root);
    assert.ok(!Object.hasOwn(selection,'compilerVerified')&&!Object.hasOwn(selection,'qualified'));}
});
test('missing downgraded or incomplete selections and pending identity substitution refuse',()=>{
  const edits=[f=>delete f.build.nativeToolchain.consumer,f=>f.build.nativeToolchain.profile='MSVC143',
    f=>f.pending.nativeToolchain.profile='MSVC143',f=>delete f.sdkBuild.nativeToolchain,
    f=>f.pending.nativeToolchain.requestFile.sha256='e'.repeat(64),f=>f.build.nativeToolchain.fallback=true,
    f=>delete f.build.nativeToolchain.validationTools.sourceFingerprint];
  for(const edit of edits){const f=fixture();edit(f);assert.throws(()=>selected(f));}
});
test('cross-owner request transplants and wrong architecture configuration consumer receipts refuse',()=>{
  const edits=[f=>f.build.nativeToolchain.requestFile=f.build.nativeToolchain.validationTools.requestFile,
    f=>f.build.nativeToolchain.validationTools.requestFile=f.build.nativeToolchain.requestFile,
    f=>f.build.nativeToolchain.sdkAccepted=pin(path.join(root,'out/sdk-x86/standard-llvm-Release.json')),
    f=>f.build.nativeToolchain.consumer=pin(path.join(root,'foreign-consumer.json')),
    f=>f.sdkBuild.nativeToolchain.requestFile=pin(path.join(root,'out/sdk-x86/llvm-profile-request-Release.json')),
    f=>f.build.configuration='Debug',f=>f.build.architecture='x86'];
  for(const edit of edits){const f=fixture();edit(f);assert.throws(()=>selected(f));}
});
test('stock identity metadata and fixed record roles cannot be omitted or redirected',()=>{
  const edits=[f=>f.build.nativeToolchain.validationTools.inputIdentity='not-a-fingerprint',
    f=>f.build.nativeToolchain.validationTools.preparation=pin(path.join(root,'arbitrary-measurement.json')),
    f=>f.build.nativeToolchain.validationTools.toolModule=path.join(root,'must-not-load.cjs'),
    f=>f.sdkBuild.nativeToolchain.requestFile.bytes++,f=>f.sdkBuild.nativeToolchain.profile='MSVC143'];
  for(const edit of edits){const f=fixture();edit(f);assert.throws(()=>selected(f));}
});
test('pin union keeps exact bytes and refuses conflicting frozen Source or observation pins',()=>{
  const a=pin(path.join(root,'first.dat')),b=pin(path.join(root,'second.dat'));
  assert.deepEqual(mergeNativePins([b,a,clone(a)]),[a,b]);
  assert.throws(()=>mergeNativePins([a,{...a,bytes:a.bytes+1}]),/Conflicting/);
  assert.throws(()=>mergeNativePins([a,{...a,sha256:'b'.repeat(64)}]),/Conflicting/);
  assert.throws(()=>mergeNativePins([]),/Complete/);
  assert.throws(()=>mergeNativePins([{...a,path:'relative.dat'}]),/Exact/);
});
test('complete LLVM and MSVC file inputs never elevate descriptive rules to native software qualification',()=>{
  for(const profile of ['MSVC143','LLVM22MSVC142'])for(const resourceGate of ['passed','failed','not-evaluated']){
    const result=completeResourceAnalysis({resourceGate,passed:true,releaseReady:true},
      {inputsMatched:true,nativeScopeObserved:false,inputBindingScope:'complete-acceptance-inputs',nativeToolchainProfile:profile});
    assert.equal(result.resourceRuleObservation,resourceGate);assert.equal(result.resourceGate,'not-evaluated');
    assert.equal(result.nativeScopeGate,'not-observed');assert.equal(result.passed,false);assert.equal(result.releaseReady,false);
  }
  for(const bad of [{inputsMatched:false,nativeScopeObserved:false,nativeToolchainProfile:'LLVM22MSVC142'},
    {inputsMatched:true,nativeScopeObserved:true,nativeToolchainProfile:'LLVM22MSVC142'},
    {inputsMatched:true,nativeScopeObserved:false,nativeToolchainProfile:'auto'}])assert.throws(()=>completeResourceAnalysis({},bad));
});
test('fixed selected readers freeze original observations and complete inventories with a final recheck',()=>{
  const helper=fs.readFileSync(path.join(__dirname,'resource-file-bindings.cjs'),'utf8');
  assert.ok(helper.includes("require('../../tools/internal/llvm-installed-acceptance.cjs')"));
  assert.ok(helper.includes('installed.verify(sdkRoot,suite,selection)'));
  assert.ok(helper.includes('installed.current(sdkRoot,build.architecture,build.configuration,selection)'));
  assert.ok(helper.includes('inventories:inputs.record.dependencies.inventories'));
  assert.ok(helper.includes('...observation.record.installedEvidencePins'));
  assert.ok(helper.includes('same(extra.nativeToolchain,current.nativeToolchain)'));
  assert.ok(!/require\([^)]*(?:stock\.root|selection\.)/.test(helper));
  const reader=fs.readFileSync(path.join(__dirname,'resource-validation.cjs'),'utf8');
  assert.equal((reader.match(/verifyCompleteBinding\(binding,outputRoot,result\)/g)||[]).length,2);
  assert.ok(reader.includes('.equals(resultRaw)')&&reader.includes('.equals(bindingRaw)'));
});
