'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');
const identity=require('../../tools/internal/delivery-identity.cjs');
const {exactSnapshot,validateRecordSet,verifyCompleteBinding,freeze}=require('./resource-file-bindings.cjs');
const {analyzeFile}=require('./resource-validation.cjs');

function tree(t) {
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-resource-inputs-'));
  t.after(()=>{
    const base=fs.realpathSync(os.tmpdir()),real=fs.realpathSync(root);
    assert.ok(real.startsWith(base+path.sep)&&path.basename(real).startsWith('soui-resource-inputs-'));
    fs.rmSync(real,{recursive:true,force:true});
  });
  const write=(name,bytes=Buffer.from(name))=>{
    const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);
  };
  write('input.cpp');write('web/main.ts');
  return {root,write,original:identity.snapshot(root,identity.walk(root))};
}

test('a complete exact snapshot retains its own physical root and original byte identity',t=>{
  const {root,original}=tree(t),current=exactSnapshot(root,original);
  assert.equal(current.root,root);assert.equal(current.inventoryMode,'exact-tree');
  assert.deepEqual(current.inventoryRoots,['']);
  assert.deepEqual(current.files,original.files);assert.equal(current.sha256,original.sha256);
  assert.equal(current.bytes,original.bytes);
});
test('partial, missing and extra input membership cannot become a complete snapshot',t=>{
  const {root,write,original}=tree(t);
  const partial=identity.snapshot(root,['input.cpp']);
  assert.throws(()=>exactSnapshot(root,partial),/complete membership/);
  write('dist-react/generated.js');assert.throws(()=>exactSnapshot(root,original),/complete membership/);
  fs.rmSync(path.join(root,'dist-react'),{recursive:true});
  fs.unlinkSync(path.join(root,'web/main.ts'));
  assert.throws(()=>exactSnapshot(root,original),/complete membership/);
});
test('current changed bytes and dishonest snapshot totals are refused',t=>{
  const {root,write,original}=tree(t);
  write('input.cpp',Buffer.from('changed'));
  assert.throws(()=>exactSnapshot(root,original),/bytes or complete membership/);
  const wrong=structuredClone(original);wrong.bytes++;
  assert.throws(()=>exactSnapshot(root,wrong),/total differs/);
});
test('only acceptance payload admits the original exact navigation diagnostic',t=>{
  const {root,write,original}=tree(t);
  write('resources/source-navigation.log',Buffer.from('diagnostic after native navigation'));
  assert.equal(exactSnapshot(root,original,'acceptance-payload').sha256,original.sha256);
  assert.throws(()=>exactSnapshot(root,original,'exact-tree'),/complete membership/);
  write('resources/unlisted.log');
  assert.throws(()=>exactSnapshot(root,original,'acceptance-payload'),/complete membership/);
});
test('exact input snapshots reject traversal, normalization overrides and unknown modes',t=>{
  const {root,original}=tree(t);
  const traversal=structuredClone(original);traversal.files[0].path='../escape.cpp';
  traversal.sha256=identity.hash(identity.encoded(traversal.files));
  assert.throws(()=>exactSnapshot(root,traversal),/Invalid path/);
  const normalized=structuredClone(original);normalized.files[0].normalization='lf';
  normalized.sha256=identity.hash(identity.encoded(normalized.files));
  assert.throws(()=>exactSnapshot(root,normalized),/normalization/);
  assert.throws(()=>exactSnapshot(root,original,'arbitrary-source'),/Unknown/);
});

// These records are DTOs for cross-record checks, never genuine SDK/native evidence.
function snapshotDto(role) {
  const bytes=Buffer.from(role),files=[{path:role+'.txt',bytes:bytes.length,sha256:identity.hash(bytes)}];
  return {files,bytes:bytes.length,sha256:identity.hash(identity.encoded(files))};
}
function records() {
  const source=snapshotDto('sdk-source'),consumerSource=snapshotDto('consumer'),webSdk=snapshotDto('web-sdk');
  const ids={architecture:'x64',configuration:'Release',version:'0.5.0-preview.1',
    buildId:'11111111-1111-4111-8111-111111111111'};
  const kernel={kind:'explicit-candidate',bytes:3,sha256:identity.hash(Buffer.from('dll'))};
  const web={schema:'soui.acceptance-web/v1',consumerSourceSha256:consumerSource.sha256,
    webSdkSha256:webSdk.sha256,output:snapshotDto('web-output'),typecheck:{passed:true,typescript:'5.9.3',
      configSha256:'a'.repeat(64),logSha256:'b'.repeat(64)}};
  const build={schema:'soui.acceptance-build/v3',...ids,sourceSha256:source.sha256,consumerSource,webSdk,kernel,web,
    consumerBuildId:'22222222-2222-4222-8222-222222222222',installedSdk:snapshotDto('installed'),
    payload:snapshotDto('payload'),releaseReady:false};
  return {sdkBuild:{schema:'soui.source-build/v2',...ids,source,sdkOnly:true,nativeBuild:'passed',releaseReady:false},
    build,pending:{...ids,sourceSha256:source.sha256,consumerSource,webSdk,kernel},webBuild:web,
    publicWeb:{schema:'soui.web-build/v1',sourceSha256:source.sha256,artifacts:snapshotDto('public-web')}};
}
test('record validation preserves SDK and consumer identities as separate fields without qualification',()=>{
  const data=records(),ids=validateRecordSet(data);
  assert.equal(ids.buildId,data.sdkBuild.buildId);assert.equal(ids.consumerBuildId,data.build.consumerBuildId);
  assert.equal(ids.sourceSha256,data.sdkBuild.source.sha256);
  assert.notEqual(ids.sourceSha256,data.build.consumerSource.sha256);
  assert.ok(!Object.hasOwn(ids,'passed')&&!Object.hasOwn(ids,'qualified'));
});
test('cross-record source, version, schema and build identity drift is refused',()=>{
  const edits=[value=>{value.pending.buildId='33333333-3333-4333-8333-333333333333';},
    value=>{value.sdkBuild.version='0.5.0-preview.2';},value=>{value.publicWeb.sourceSha256='c'.repeat(64);},
    value=>{value.pending.sourceSha256=value.build.consumerSource.sha256;},
    value=>{value.sdkBuild.architecture='x86';},value=>{value.sdkBuild.nativeBuild='pending';},
    value=>{value.sdkBuild.sdkOnly=false;},value=>{value.build.schema='soui.acceptance-build/v2';}];
  for(const edit of edits){const data=structuredClone(records());edit(data);assert.throws(()=>validateRecordSet(data));}
});
test('Web snapshot/typecheck and copied kernel record drift is refused',()=>{
  const edits=[value=>{value.webBuild.consumerSourceSha256='d'.repeat(64);},
    value=>{value.webBuild.typecheck.passed=false;},value=>{value.pending.webSdk=snapshotDto('other-web-sdk');},
    value=>{value.build.kernel.kind='qualified-win7-kernel';},value=>{value.build.kernel.bytes='3';},
    value=>{value.build.installedSdk.bytes++;}];
  for(const edit of edits){const data=structuredClone(records());edit(data);assert.throws(()=>validateRecordSet(data));}
});
test('a binding cannot assert a software/native collector or redirect the current SDK input roots',()=>{
  const root=path.resolve(__dirname,'../..'),suite=path.join(root,'out/acceptance/logical-only');
  const run=path.join(suite,'reload-44444444-4444-4444-8444-444444444444');
  const binding={schema:'soui.resource-file-bindings/v2',additionalInputs:{schema:'soui.acceptance-resource-inputs/v1',
    sdkRoot:root,suiteRoot:suite,runRoot:run,nativeScope:{status:'observed',software:true}}};
  assert.throws(()=>verifyCompleteBinding(binding,run),/cannot be asserted/);
  const redirected=structuredClone(binding);redirected.additionalInputs.suiteRoot=os.tmpdir();
  assert.throws(()=>verifyCompleteBinding(redirected,run),/current SDK acceptance suite/);
});
test('incomplete v2 file inputs cannot yield an evaluated resource gate or load caller tool code',t=>{
  const {root,write}=tree(t);
  write('result.json',Buffer.from(JSON.stringify({schema:'soui.acceptance-run/v1',passed:true})));
  write('binding.json',Buffer.from(JSON.stringify({schema:'soui.resource-file-bindings/v2',
    toolModule:path.join(root,'must-not-load.cjs')})));
  write('must-not-load.cjs',Buffer.from('throw new Error("caller tool code must not load");'));
  const result=analyzeFile(path.join(root,'result.json'),path.join(root,'binding.json'));
  assert.equal(result.resourceGate,'not-evaluated');assert.equal(result.passed,false);assert.equal(result.releaseReady,false);
});
test('freeze rejects outside-suite paths before constructing a file binding',t=>{
  const {root}=tree(t);
  assert.throws(()=>freeze(root,path.join(root,'run')),/current SDK acceptance suite/);
  assert.ok(!fs.existsSync(path.join(root,'run','file-bindings.json')));
});
