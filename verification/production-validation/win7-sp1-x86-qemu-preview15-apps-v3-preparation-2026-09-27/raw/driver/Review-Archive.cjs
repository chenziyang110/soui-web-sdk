'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,prep='C:/SOUI-SDK-production-validation/preview15-win7-preparation-v3-20260927',repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const freeze=id.readJson(path.join(prep,'freeze.json')),input=id.readJson(path.join(prep,'input.json'));
assert.equal(id.hash(fs.readFileSync(path.join(prep,'freeze.json'))),'47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005');
for(const f of freeze.files){const b=fs.readFileSync(path.join(prep,f.path));assert.equal(b.length,f.bytes);assert.equal(id.hash(b),f.sha256);}
for(const name of fs.readdirSync(path.join(root,'command-logs'))){const base=path.join(root,'command-logs',name),r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,0);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const s of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,s+'.log'));assert.equal(b.length,r[s].bytes);assert.equal(id.hash(b),r[s].sha256);}}
const self=id.readJson(path.join(prep,'self-test-results.json'));assert.equal(self.acceptedFixtures,1);assert.equal(self.rejectedFixtures,27);assert.equal(self.guestExecuted,false);
const review={status:'current-guest-input-frozen-not-executed',version:input.version,freezeSha256:id.hash(fs.readFileSync(path.join(prep,'freeze.json'))),frozenFiles:freeze.files.length,inputSha256:freeze.inputSha256,buildId:input.buildId,sourceSha256:input.sourceSha256,layoutVersion:3,appTimeoutMs:input.appTimeoutMs,closeDiagnostics:input.closeDiagnostics,acceptedSyntheticFixtures:1,rejectedSyntheticFixtures:27,transferDiagnosticHelperPassed:true,processDiagnosticHelperPassed:true,guestExecuted:false,releaseReady:false};
fs.writeFileSync(path.join(root,'review.json'),id.encoded(review),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/win7-sp1-x86-qemu-preview15-apps-v3-preparation-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const [label,base] of [['preparation',prep],['driver',root]])for(const name of id.walk(base,'',(p)=>p==='payloads'||/\.(zip|exe)$/i.test(p))){const original=path.join(base,name),b=fs.readFileSync(original),relative='raw/'+label+'/'+name,dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:relative,original,bytes:b.length,sha256:id.hash(b)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,freezeSha256:review.freezeSha256,guestExecuted:false,releaseReady:false}));
