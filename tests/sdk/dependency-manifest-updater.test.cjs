'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),cp=require('node:child_process');
const repository=path.resolve(__dirname,'../..'),hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const policy='third_party/manifests/miniblink-frame-policy-state-20261102.json';
const patch='third_party/patches/miniblink/0033-frame-policy-applied-state.patch';
const provenance='third_party/manifests/kernel-fixture-current.json';
const output='third_party/manifests/dependencies.json',license='third_party/manifests/license-review.json';
const patchBytes=Buffer.from('diff --git a/fixture.cpp b/fixture.cpp\r\n--- a/fixture.cpp\r\n+++ b/fixture.cpp\r\n@@ -1 +1 @@\r\n-before\r\n+after\r\n');
const originalIds=['soui-source','soui-llvm-patch','soui-llvm-source','soui-build-provenance','miniblink-source','miniblink-encoding-patch',
  'miniblink-scheduler-patch','miniblink-build-provenance','miniblink-cache-budget-patch','miniblink-cache-source','miniblink-input-latency-patch',
  'miniblink-input-latency-source','miniblink-precise-deadline-patch','miniblink-precise-deadline-source','zlib-source','json','tailwind-utility-css',
  'miniblink-x86','zlib-runtime-x86','miniblink-x64','zlib-runtime-x64','web-sdk','template-web','modern-template-web','webview2-sdk','demo-web'];
function put(root,name,bytes){const full=path.join(root,name);fs.mkdirSync(path.dirname(full),{recursive:true});fs.writeFileSync(full,bytes);return full;}
function pe(architecture){
  // Same minimal complete PE/COFF shape used by dependency-evidence contracts;
  // these 512-byte bytes are never loaded or described as a rebuilt kernel.
  const bytes=Buffer.alloc(512),is64=architecture==='x64';bytes.writeUInt16LE(0x5a4d);bytes.writeUInt32LE(64,60);bytes.writeUInt32LE(0x4550,64);
  bytes.writeUInt16LE(is64?0x8664:0x14c,68);bytes.writeUInt16LE(1,70);bytes.writeUInt16LE(is64?112:96,84);
  bytes.writeUInt16LE(0x2002,86);bytes.writeUInt16LE(is64?0x20b:0x10b,88);return bytes;
}
function fixture(t){
  const retained=process.env.SOUI_UPDATER_FIXTURE_ROOT,base=retained?path.resolve(retained):os.tmpdir();
  if(retained){assert(path.isAbsolute(retained));fs.mkdirSync(base,{recursive:true});}
  const root=fs.mkdtempSync(path.join(base,'soui-updater-'));
  assert.notEqual(root,repository);assert(!root.startsWith(repository+path.sep));
  if(retained)t.diagnostic('retained isolated updater fixture: '+root);
  else t.after(()=>{const resolved=path.resolve(root);assert.equal(path.dirname(resolved),path.resolve(base));assert(path.basename(resolved).startsWith('soui-updater-'));fs.rmSync(resolved,{recursive:true,force:true});});
  for(const name of ['Update-DependencyManifest.cjs','delivery-identity.cjs','native-payload.cjs','sdk-version.cjs'])
    put(root,'tools/internal/'+name,fs.readFileSync(path.join(repository,'tools/internal',name)));
  for(const name of [
    'third_party/sources/soui/SOURCE-MANIFEST.json','third_party/patches/soui/0001-llvm-conformance.patch',
    'third_party/manifests/soui-llvm-source-050.json','third_party/manifests/soui-build-050.json',
    'third_party/manifests/provenance/preview14/third_party/sources/miniblink/SOURCE-MANIFEST.json',
    'third_party/manifests/provenance/preview14/third_party/patches/miniblink/0007-mark-utf8-source-encoding.patch',
    'third_party/manifests/provenance/preview14/third_party/patches/miniblink/0008-bound-ready-task-priority-wait.patch',
    'third_party/patches/miniblink/0012-retained-resource-byte-budget.patch','third_party/manifests/kernel-cache-source-050.json',
    'third_party/patches/miniblink/0013-bound-main-thread-scheduler-batches.patch','third_party/manifests/kernel-input-latency-source.json',
    'third_party/patches/miniblink/0029-retain-precise-shared-timer-wait.patch','third_party/manifests/miniblink-precise-deadline-20261030.json',
    'third_party/sources/zlib/source-manifest.json','third_party/include/json/json.hpp','demos/workbench/web/apps/react/utility.css',
    'sdk/web/package-lock.json','templates/desktop-app/web/package-lock.json','templates/modern-app/web/package-lock.json',
    'sdk/renderers/webview2/dependency.json','demos/workbench/web/package-lock.json'])put(root,name,'fixture source\r\n');
  put(root,'VERSION','0.7.0-preview.2\n');put(root,patch,patchBytes);
  const policyBytes=Buffer.from(JSON.stringify({schema:'soui.miniblink-frame-policy-candidate/v1',sdkVersion:'0.7.0-preview.1',hashScope:'raw-file-bytes',
    patch:{path:patch,bytes:patchBytes.length,sha256:hash(patchBytes)}})+'\r\n');put(root,policy,policyBytes);
  const independentLicense=Buffer.from('{"fixture":"independent license review; never generated"}\r\n');put(root,license,independentLicense);
  put(root,output,JSON.stringify({dependencies:[{id:'miniblink-frame-policy-source',source:policy},{id:'miniblink-frame-policy-patch',source:patch},
    {id:'arbitrary-unknown-row',source:'unreviewed/not-preserved'}]})+'\n');
  put(root,'third_party/manifests/kernel-fixture-evidence.json','{"fixtureOnly":true,"buildExecuted":false}\n');
  const imports=['x86','x64'].map(architecture=>{
    const bytes=pe(architecture),name='third_party/prebuilt/'+architecture+'/miniblink/node.dll';put(root,name,bytes);
    put(root,'third_party/prebuilt/'+architecture+'/runtime/vc/zlib1.dll','fake zlib fixture');
    put(root,name+'.before-gpu-lcd','historical backup fixture');
    return {architecture,path:name,bytes:bytes.length,sha256:hash(bytes),sourceInputSha256:'a'.repeat(64),buildEvidence:'third_party/manifests/kernel-fixture-evidence.json'};
  });
  const kernel={schema:'soui.kernel-import/v1',sdkVersion:'0.7.0-preview.2',releaseReady:false,imports};
  const saveKernel=()=>put(root,provenance,JSON.stringify(kernel)+'\n');saveKernel();
  let invocation=0;
  const run=()=>{
    // Execute only the copied production CLI. __dirname therefore selects this
    // fresh fake payload, never the real repository or its dependencies output.
    const child=cp.spawnSync(process.execPath,[path.join(root,'tools/internal/Update-DependencyManifest.cjs'),'--kernel-provenance',provenance],
      {cwd:root,encoding:'utf8',windowsHide:true,timeout:10000});
    put(root,'invocation-'+(++invocation)+'.json',JSON.stringify({status:child.status,signal:child.signal,error:child.error?.message||null,stdout:child.stdout,stderr:child.stderr},null,2)+'\n');
    return child;
  };
  return {root,run,kernel,saveKernel,policyBytes,independentLicense,read:name=>fs.readFileSync(path.join(root,name)),write:(name,bytes)=>put(root,name,bytes)};
}
test('regeneration retains the two fixed frame-policy sources and original CRLF patch identity',t=>{
  const f=fixture(t),first=f.run();assert.equal(first.status,0,first.stderr);
  const manifest=JSON.parse(f.read(output)),rows=manifest.dependencies.filter(row=>row.id==='miniblink-frame-policy-source'||row.id==='miniblink-frame-policy-patch');
  assert.equal(rows.length,2,'Regeneration dropped existing fixed F01 source/patch provenance rows');
  assert.deepEqual(rows.map(row=>row.id),['miniblink-frame-policy-source','miniblink-frame-policy-patch']);
  assert.deepEqual(manifest.dependencies.filter(row=>!rows.includes(row)).map(row=>row.id),originalIds,'Original definition order changed');
  const source=rows.find(row=>row.source===policy),patchRow=rows.find(row=>row.source===patch);assert(source&&patchRow);
  assert.equal(source.sourceNormalization,'lf');assert.equal(source.sha256,hash(Buffer.from(f.policyBytes.toString().replace(/\r\n/g,'\n'))));
  assert.equal(patchRow.sourceNormalization,'none');assert.equal(patchRow.sha256,hash(patchBytes));
  assert.notEqual(patchRow.sha256,hash(Buffer.from(patchBytes.toString().replace(/\r\n/g,'\n'))));
  assert(rows.every(row=>/20261102.*selected kernel provenance/.test(row.version)&&!row.version.includes('prebuilt unchanged')));
  assert.equal(manifest.dependencies.some(row=>row.id==='arbitrary-unknown-row'),false);
  assert.equal(manifest.nativeFiles.length,4);assert.equal(manifest.nativeFiles.some(row=>row.path.endsWith('.before-gpu-lcd')),false);
  assert.deepEqual(f.read(license),f.independentLicense);assert.deepEqual(f.read(policy),f.policyBytes);assert.deepEqual(f.read(patch),patchBytes);
  const original=f.read(output),second=f.run();assert.equal(second.status,0,second.stderr);assert.deepEqual(f.read(output),original);
  assert.deepEqual(f.read(license),f.independentLicense);
});
test('missing fixed frame-policy sources fail before overwriting metadata',t=>{
  for(const name of [policy,patch]){
    const f=fixture(t),original=f.read(output);fs.unlinkSync(path.join(f.root,name));const result=f.run();
    assert.notEqual(result.status,0);assert.match(result.stderr,/ENOENT|no such file/i);assert.deepEqual(f.read(output),original);assert.deepEqual(f.read(license),f.independentLicense);
  }
});
test('fixed frame-policy sources cannot alias output or independent license metadata',t=>{
  for(const reserved of [output,license]){
    const f=fixture(t),original=f.read(output);fs.unlinkSync(path.join(f.root,policy));fs.linkSync(path.join(f.root,reserved),path.join(f.root,policy));
    const result=f.run();assert.notEqual(result.status,0);assert.match(result.stderr,/cannot alias reserved metadata/);
    assert.deepEqual(f.read(output),original);assert.deepEqual(f.read(license),f.independentLicense);
  }
});
test('frame-policy preservation retains selected kernel version and PE architecture validation',t=>{
  for(const invalid of ['version','architecture']){
    const f=fixture(t),original=f.read(output);
    if(invalid==='version')f.kernel.sdkVersion='0.7.0-preview.1';
    else{
      const imported=f.kernel.imports.find(row=>row.architecture==='x64'),bytes=pe('x86');f.write(imported.path,bytes);imported.sha256=hash(bytes);
    }
    f.saveKernel();const result=f.run();assert.notEqual(result.status,0);assert.match(result.stderr,/version|architecture differs/i);
    assert.deepEqual(f.read(output),original);assert.deepEqual(f.read(license),f.independentLicense);
  }
});
