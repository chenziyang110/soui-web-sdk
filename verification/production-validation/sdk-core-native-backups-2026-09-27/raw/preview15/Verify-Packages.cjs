'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',root=__dirname;
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs')),sdk=require(path.join(repo,'tools/sdk/soui-sdk.cjs'));
const oldRoot='C:/SOUI-SDK-production-validation/preview13-install-recovery-20260926/sdk-package';
const current=path.join(root,'sdk-package'),project=path.join(root,'independent-project');
assert(!fs.existsSync(current));assert(!fs.existsSync(project));
const oldBytes=fs.readFileSync(path.join(oldRoot,'sdk-manifest.json')),old=sdk.readPackage(oldRoot);
assert.equal(old.version,'0.3.0-preview.13');assert.equal(old.files.length,419);
const backup=old.files.filter(f=>f.path.endsWith('node.dll.before-gpu-lcd'));
assert.equal(backup.length,2);assert.equal(backup.reduce((n,f)=>n+f.bytes,0),58685440);
const version=fs.readFileSync(path.join(repo,'VERSION'),'utf8').trim();assert.equal(version,'0.3.0-preview.15');
assert.equal(require(path.join(repo,'tools/internal/build-evidence.cjs')).version(repo),version);
sdk.pack(repo,current,version);const next=sdk.readPackage(current);
require('node:child_process').execFileSync(process.execPath,[path.join(repo,'tools/sdk/soui-sdk.cjs'),'verify-source','--package',current,'--source',repo],{windowsHide:true});
assert(!next.files.some(f=>backup.some(b=>b.path===f.path)));
const deps=id.readJson(path.join(current,'payload/third_party/manifests/dependencies.json'));
assert.equal(deps.sdkVersion,version);assert.equal(deps.nativeFiles.length,136);
for(const f of deps.nativeFiles){const b=fs.readFileSync(path.join(current,'payload',f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256);}
for(const f of backup){const b=fs.readFileSync(path.join(repo,f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256);}
const important=old.files.filter(f=>/^third_party\/(prebuilt|licenses)\//.test(f.path)&&!backup.some(b=>b.path===f.path));
for(const f of important)assert(next.files.some(n=>n.path===f.path),f.path);
for(const arch of ['x86','x64']){
  const p=`third_party/prebuilt/${arch}/miniblink/node.dll`;
  const expected=id.readJson(path.join(root,'kernel-import.json')).imports.find(f=>f.path===p);
  assert.equal(next.files.find(f=>f.path===p).sha256,expected.sha256);
}
fs.mkdirSync(path.join(project,'app'),{recursive:true});fs.writeFileSync(path.join(project,'app/business.json'),'{"名称":"业务账本","keep":true}\n',{flag:'wx'});
const business=id.snapshot(project,id.walk(project));const steps=[];
for(const [label,pkg,expected] of [['old',oldRoot,old],['upgrade',current,next],['rollback',oldRoot,old],['upgrade-again',current,next]]){
  const result=sdk.install(pkg,project);const lock=sdk.readLock(project);
  assert.equal(lock.manifest.version,expected.version);assert.deepEqual(lock.manifest,expected);
  assert.deepEqual(id.snapshot(project,business.files.map(f=>f.path)),business);
  for(const f of backup)assert.equal(fs.existsSync(path.join(project,f.path)),expected===old);
  steps.push({label,...result,lockSha256:id.hash(fs.readFileSync(path.join(project,'sdk-lock.json'))),businessUnchanged:true});
}
assert.deepEqual(fs.readFileSync(path.join(oldRoot,'sdk-manifest.json')),oldBytes);sdk.readPackage(oldRoot);
const oldSize=old.files.reduce((n,f)=>n+f.bytes,0),newSize=next.files.reduce((n,f)=>n+f.bytes,0);
const report={schema:'soui.sdk-native-backups-validation/v1',version,oldManifestSha256:id.hash(oldBytes),
  newManifestSha256:id.hash(fs.readFileSync(path.join(current,'sdk-manifest.json'))),oldFiles:old.files.length,newFiles:next.files.length,
  oldPayloadBytes:oldSize,newPayloadBytes:newSize,netReductionBytes:oldSize-newSize,removedHistoricalBackupBytes:58685440,
  historicalSourceBackupsUnchanged:backup,activeNativeFiles:deps.nativeFiles.length,legalFiles:next.files.filter(f=>f.path.startsWith('third_party/licenses/')).length,
  steps,sourcePackage:current,business,applicationRuntimeSizeReductionClaimed:false,currentSdkGuiValidated:false,releaseReady:false};
fs.writeFileSync(path.join(root,'package-review.json'),id.encoded(report),{flag:'wx'});
console.log(JSON.stringify(report,null,2));
