'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),cp=require('node:child_process');
const repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const read=name=>fs.readFileSync(path.join(repo,name));
const previous=id.readJson(path.join(repo,'third_party/manifests/dependencies.json'));
assert.equal(previous.sdkVersion,'0.3.0-preview.13');
const backups=previous.nativeFiles.filter(f=>f.path.endsWith('.before-gpu-lcd'));
assert.equal(backups.length,2);assert.equal(backups.reduce((n,f)=>n+f.bytes,0),58685440);
for(const f of previous.nativeFiles)assert.equal(id.hash(read(f.path)),f.sha256);
const imports=[];
for(const arch of ['x86','x64']){
  const external=`C:/SOUI-SDK-production-validation/preview14-renewal-animation-candidate-${arch}-20260926`;
  const reviewed=id.readJson(path.join(external,'review.json'));
  assert.equal(reviewed.nativeExitCode,0);assert.equal(reviewed.originalAnimationUnchanged,true);
  const artifact=id.readJson(path.join(repo,`out/dependencies/${arch}/build-All.json`)).artifacts.find(x=>x.kind==='miniblink');
  const bytes=fs.readFileSync(artifact.path);assert.equal(id.hash(bytes),artifact.sha256.toLowerCase());assert.equal(id.hash(bytes),reviewed.kernelSha256);
  assert.equal(bytes.readUInt16LE(bytes.readUInt32LE(0x3c)+4),arch==='x86'?0x14c:0x8664);
  const target=`third_party/prebuilt/${arch}/miniblink/node.dll`;
  imports.push({architecture:arch,path:target,bytes:bytes.length,sha256:id.hash(bytes),previousSha256:id.hash(read(target)),sourceInputSha256:reviewed.kernelBuildSourceSha256,
    buildEvidence:`verification/production-validation/scheduler-fairness-${arch}-rebuild-2026-09-${arch==='x86'?'26':'27'}/raw/build/build-All.json`,
    originalAnimationEvidence:`verification/production-validation/scheduler-fairness-${arch}-rebuild-2026-09-${arch==='x86'?'26':'27'}/raw/animated/review.json`});
}
fs.writeFileSync(path.join(__dirname,'before-dependencies.json'),id.encoded(previous),{flag:'wx'});
for(const name of ['tools/sdk/soui-sdk.cjs','tools/internal/Update-DependencyManifest.cjs']){
  const bytes=cp.execFileSync('git',['-C',repo,'show','HEAD:'+name],{windowsHide:true});
  const dest=path.join(__dirname,'before',name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});
}
for(const name of ['VERSION','sdk/include/soui/version.h','sdk/web/package.json','sdk/web/package-lock.json','README.md']){
  const text=read(name).toString('utf8');assert(text.includes('0.3.0-preview.13'),name);
  fs.writeFileSync(path.join(repo,name),text.replaceAll('0.3.0-preview.13','0.3.0-preview.14'));
}
for(const f of imports){
  const source=id.readJson(path.join(repo,`out/dependencies/${f.architecture}/build-All.json`)).artifacts.find(x=>x.kind==='miniblink').path;
  fs.copyFileSync(source,path.join(repo,f.path));assert.equal(id.hash(read(f.path)),f.sha256);
}
for(const f of backups)assert.equal(id.hash(read(f.path)),f.sha256);
const record={schema:'soui.kernel-import/v1',sdkVersion:'0.3.0-preview.14',sourceCommit:'be1a70872682cb25acbcbfc96fb7fe46bb322667',
  imports,unchangedHistoricalBackups:backups,patches:['third_party/patches/miniblink/0008-bound-ready-task-priority-wait.patch'],
  currentSdkApplicationValidationComplete:false,binaryLicenseReviewComplete:false,releaseReady:false};
fs.writeFileSync(path.join(repo,'third_party/manifests/kernel-build-preview14.json'),id.encoded(record),{flag:'wx'});
fs.writeFileSync(path.join(__dirname,'kernel-import.json'),id.encoded(record),{flag:'wx'});
console.log(JSON.stringify({version:record.sdkVersion,imported:imports.map(f=>({architecture:f.architecture,sha256:f.sha256})),preservedBackupBytes:58685440}));
