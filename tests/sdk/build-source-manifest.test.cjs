'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const manifest=require('../../tools/internal/build-source-manifest.cjs');
function fixture(t){
  const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'soui-build-manifest-'));
  t.after(()=>{const actual=fs.realpathSync(root);assert(actual.startsWith(parent+path.sep));fs.rmSync(actual,{recursive:true,force:true});});
  const folder=path.join(root,'out','standard-build','attempt-test');fs.mkdirSync(folder,{recursive:true});
  fs.writeFileSync(path.join(root,'source-manifest.json'),'original manifest bytes\r\n');
  return {root,folder};
}
test('capture preserves exact bytes and refuses to overwrite an attempt snapshot',t=>{
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  assert.deepEqual(fs.readFileSync(binding.snapshot.path),fs.readFileSync(binding.live.path));
  assert.equal(binding.snapshot.sha256,binding.live.sha256);
  manifest.assertLive(binding);
  manifest.validate(binding,f.root,f.folder,[binding.snapshot]);
  assert.throws(()=>manifest.capture(f.root,f.folder));
});
test('metadata refresh is historical-only; during-build drift still fails',t=>{
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  fs.writeFileSync(binding.live.path,'later collected metadata');
  assert.throws(()=>manifest.assertLive(binding),/manifest.*changed/i);
  manifest.validate(binding,f.root,f.folder,[binding.snapshot]);
  // This function only binds historical input, and grants no compiled-source or archive acceptance.
});
test('captured bytes cannot be replaced after build',t=>{
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  fs.writeFileSync(binding.snapshot.path,'tampered');
  assert.throws(()=>manifest.validate(binding,f.root,f.folder,[binding.snapshot]),/manifest.*changed/i);
});
test('snapshot ownership, exact shape, live identity and control binding are mandatory',t=>{
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  for(const change of [b=>b.live.path=path.join(f.root,'other.json'),b=>b.snapshot.path=path.join(f.root,'other.json'),
    b=>b.live.sha256='0'.repeat(64),b=>b.live.bytes+=1,b=>b.extra=true,b=>b.schema='unknown']){
    const candidate=structuredClone(binding);change(candidate);
    assert.throws(()=>manifest.validate(candidate,f.root,f.folder,[candidate.snapshot]));
  }
  for(const controls of [[],[binding.live],[binding.snapshot,binding.snapshot],[binding.snapshot,binding.live]])
    assert.throws(()=>manifest.validate(binding,f.root,f.folder,controls));
});
test('legacy receipts retain their original live-file rule and cannot impersonate snapshots',t=>{
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  manifest.validate(undefined,f.root,f.folder,[binding.live]);
  assert.throws(()=>manifest.validate(undefined,f.root,f.folder,[binding.snapshot]));
  fs.writeFileSync(binding.live.path,'later');
  assert.throws(()=>manifest.validate(undefined,f.root,f.folder,[binding.live]),/manifest.*changed/i);
});

test('the actual LLVM reader validates captured manifests before requiring native proof',t=>{
  const crypto=require('node:crypto'),vm=require('node:vm'),{createRequire}=require('node:module');
  const f=fixture(t),binding=manifest.capture(f.root,f.folder);
  function save(name,value){
    const filename=path.join(f.folder,name),bytes=Buffer.from(JSON.stringify(value));fs.writeFileSync(filename,bytes);
    return {path:filename,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};
  }
  const debugInput=save('debug-input.json',{controlPins:[binding.snapshot]});
  const debugBinding={input:debugInput};
  const frozen={schema:'soui.standard-build-inputs/v2',invocationOptions:{architecture:'x64',configuration:'Release',sdkOnly:false},
    llvm:{sourceManifestInput:binding,debugBinding}};
  const build={buildId:'fixture-build',source:{sha256:'1'.repeat(64)}};
  const accepted={schema:'soui.standard-llvm-accepted-build/v2',architecture:'x64',configuration:'Release',sdkOnly:false,
    buildId:build.buildId,sourceSha256:build.source.sha256,qualificationPassed:false,releaseReady:false,attemptDirectory:f.folder,
    observation:save('observation.json',{schema:'soui.standard-llvm-build-observation/v2',debugCapture:debugBinding}),
    inputs:save('frozen.json',frozen),observed:save('observed.json',{}),requestFile:save('request.json',{discovery:{}}),
    buildReportSnapshot:save('build.json',build),buildCapture:Object.fromEntries(['ready','finished','context','raw'].map(name=>[name,save(name+'.json',{})]))};
  save('result.json',{accepted:true,genericSupervisionPassed:true,compilerTailAcceptance:false});
  fs.mkdirSync(path.join(f.root,'out/x64'));fs.writeFileSync(path.join(f.root,'out/x64/standard-llvm-Release.json'),JSON.stringify(accepted));
  const filename=path.resolve(__dirname,'../../tools/internal/llvm-build-observer.cjs'),nativeRequire=createRequire(filename);
  let buildReads=0,nativeReads=0;
  const localRequire=name=>name==='./build-evidence.cjs'?{verifyBuild(){buildReads++;return build;}}:
    name==='./host64-build-debug-observer.cjs'?{recordedEvidence(){nativeReads++;throw new Error('native-proof-still-required');}}:nativeRequire(name);
  const module={exports:{}};
  vm.runInNewContext(fs.readFileSync(filename,'utf8'),{require:localRequire,module,exports:module.exports,Buffer,process,console,__dirname:path.dirname(filename),__filename:filename},{filename});
  const verify=()=>module.exports.verifyStandardBuild(f.root,'x64','Release',false,{nativeToolchain:'LLVM22MSVC142'});
  fs.writeFileSync(binding.live.path,'later verification metadata');
  assert.throws(verify,/native-proof-still-required/);assert.equal(buildReads,1);assert.equal(nativeReads,1);
  fs.writeFileSync(binding.snapshot.path,'altered captured manifest');
  assert.throws(verify,/Build source manifest bytes changed/);assert.equal(buildReads,2);assert.equal(nativeReads,1);
  // Synthetic routing fixture stops at native proof; it never accepts a build.
});
