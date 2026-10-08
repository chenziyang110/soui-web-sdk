'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const review=id.readJson(path.join(root,'preparation-review-v2.json'));
assert.equal(review.status,'current-renewal-preparation-reviewed-not-compiled');assert.equal(review.sdkCompiled,false);assert.equal(review.guiExecuted,false);
for(const f of review.controls.files){const b=fs.readFileSync(path.join(root,f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256);}
for(const name of ['analyzer-self-test','runner-rejection-self-test','sdk-build-attempt1']){const base=path.join(root,'command-logs',name),r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,name==='sdk-build-attempt1'?2:0);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const s of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,s+'.log'));assert.equal(b.length,r[s].bytes);assert.equal(id.hash(b),r[s].sha256);}}
const rejected=id.readJson(path.join(root,'runner-rejection-self-test/summary.json'));assert.equal(rejected.cases,2);assert.equal(rejected.actualCurrentInputPositives,0);
const gate=JSON.parse(fs.readFileSync(path.join(root,'command-logs/sdk-build-attempt1/stdout.log'),'utf8'));assert.equal(gate.status,'busy-no-compile');assert.match(gate.gate.stdout,/qemu-system-x86_64/);
assert(!fs.existsSync(path.join(root,'staging/source/out')));assert(!fs.existsSync(path.join(root,'sdk-build-result.json')));assert(!fs.existsSync(path.join(root,'freeze.json')));
const target=path.join(repo,'verification/production-validation/preview15-real-renewal-preparation-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const name of id.walk(root,'',(_,leaf)=>leaf==='staging')){const original=path.join(root,name),b=fs.readFileSync(original),relative='raw/'+name,dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:relative,original,bytes:b.length,sha256:id.hash(b)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,analyzerSyntheticCases:106,earlyRunnerRejections:2,sdkBuildBlocked:true,compiled:false,guiExecuted:false}));
