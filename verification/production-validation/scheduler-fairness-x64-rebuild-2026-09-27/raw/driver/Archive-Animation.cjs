'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler';
const source='C:/SOUI-SDK-production-validation/preview14-renewal-animation-candidate-x64-20260926';
const target=path.join(repo,'verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27');
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const review=id.readJson(path.join(source,'review.json'));
assert.equal(review.nativeExitCode,0);assert.equal(review.originalAnimationUnchanged,true);
assert.equal(review.kernelSha256,id.readJson(path.join(__dirname,'dependency-review.json')).artifacts.find(a=>a.kind==='miniblink').sha256);
const manifest=id.readJson(path.join(target,'raw-manifest.json'));
for(const name of id.walk(source,'',(_,leaf)=>['runtimes','runtime'].includes(leaf)).concat([])) {
  const relative='raw/animated/'+name,original=path.join(source,name),bytes=fs.readFileSync(original);
  const previous=manifest.files.find(f=>f.path===relative),dest=path.join(target,relative);
  if(previous){assert.equal(id.hash(bytes),previous.sha256);assert.deepEqual(fs.readFileSync(dest),bytes);continue;}
  fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});
  manifest.files.push({path:relative,original,bytes:bytes.length,sha256:id.hash(bytes)});
}
const self=fs.readFileSync(__filename),relative='raw/driver/Archive-Animation.cjs';
fs.writeFileSync(path.join(target,relative),self,{flag:'wx'});
manifest.files.push({path:relative,original:__filename,bytes:self.length,sha256:id.hash(self)});
manifest.files.sort((a,b)=>a.path.localeCompare(b.path));
for(const f of manifest.files){assert.equal(id.hash(fs.readFileSync(f.original)),f.sha256);assert.equal(id.hash(fs.readFileSync(path.join(target,f.path))),f.sha256);}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded(manifest));
console.log(JSON.stringify({files:manifest.files.length,nativeExitCode:review.nativeExitCode,freeze:review.freezeSha256}));
