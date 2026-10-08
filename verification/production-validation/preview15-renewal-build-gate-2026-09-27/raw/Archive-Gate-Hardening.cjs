'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const old=id.readJson(path.join(root,'preparation-review-v2.json')),now=id.readJson(path.join(root,'preparation-review-v3.json'));
assert.equal(now.sourceSha256,old.sourceSha256);assert.equal(now.sdkManifestSha256,old.sdkManifestSha256);assert.equal(now.sdkCompiled,false);assert.equal(now.guiExecuted,false);
for(const f of old.controls.files){const original=f.path==='Build-Sdk.cjs'?'provenance/Build-Sdk.before-gate-hardening.cjs':f.path;assert.equal(id.hash(fs.readFileSync(path.join(root,original))),f.sha256);}
for(const f of now.controls.files)assert.equal(id.hash(fs.readFileSync(path.join(root,f.path))),f.sha256);
assert.equal(id.source(path.join(root,'staging/source')).sha256,now.sourceSha256);
assert(!fs.existsSync(path.join(root,'staging/source/out')));assert(!fs.existsSync(path.join(root,'sdk-build-result.json')));assert(!fs.existsSync(path.join(root,'freeze.json')));
const before=id.readJson(path.join(root,'gate-selftests/before/result.json')),after=id.readJson(path.join(root,'gate-selftests/after/result.json'));
assert.equal(before.sourceSha256,id.hash(fs.readFileSync(path.join(root,'provenance/Build-Sdk.before-gate-hardening.cjs'))));assert.equal(after.sourceSha256,id.hash(fs.readFileSync(path.join(root,'Build-Sdk.cjs'))));
assert.equal(before.failed,3);assert.equal(after.failed,0);assert.equal(after.passed,6);assert.equal(before.compilerStarted,false);assert.equal(after.compilerStarted,false);
assert.deepEqual(before.cases.filter(c=>!c.passed).map(c=>c.name),['query-error-must-block','stderr-despite-zero-must-block','unexpected-stdout-must-block']);
const commands=[['gate-selftest-before',1],['gate-selftest-after',0],['preparation-review-v3',0],['sdk-build-attempt2',2]];
for(const [name,exit] of commands){const base=path.join(root,'command-logs',name),r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,exit);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(b.length,r[stream].bytes);assert.equal(id.hash(b),r[stream].sha256);}}
const actual=id.readJson(path.join(root,'command-logs/sdk-build-attempt2/stdout.log'));assert.equal(actual.status,'gate-rejected-no-compile');assert.equal(actual.gate.exitCode,2);assert.match(actual.gate.stdout,/qemu-system-x86_64/);
const gateFiles=fs.readdirSync(root).filter(n=>/^sdk-x86-gate-\d+\.json$/.test(n)&&id.readJson(path.join(root,n)).at===actual.gate.at);assert.equal(gateFiles.length,1);
const report={version:now.version,sourceSha256:now.sourceSha256,sdkManifestSha256:now.sdkManifestSha256,oldBuildDriverSha256:before.sourceSha256,newBuildDriverSha256:after.sourceSha256,oldGateRejectedCases:before.passed,oldGateFailedCases:before.failed,newGatePassedCases:after.passed,actualDriverExitCode:2,actualGateObservedExternalQemu:true,stagingUnchanged:true,onlyExistingControlChanged:'Build-Sdk.cjs',sdkCompiled:false,consumerCompiled:false,guiExecuted:false,realRenewalVerified:false,releaseReady:false};
fs.writeFileSync(path.join(root,'gate-hardening-review.json'),id.encoded(report),{flag:'wx'});
const names=['Build-Sdk.cjs','Test-Build-Gate.cjs','Archive-Gate-Hardening.cjs','gate-hardening-review.json','preparation-review-v2.json','preparation-review-v3.json','staging-input.json','provenance/Build-Sdk.before-gate-hardening.cjs','gate-selftests/before/result.json','gate-selftests/after/result.json',...gateFiles];
for(const [name] of commands)for(const n of id.walk(path.join(root,'command-logs',name)))names.push('command-logs/'+name+'/'+n);
const dest=path.join(repo,'verification/production-validation/preview15-renewal-build-gate-2026-09-27');assert(!fs.existsSync(dest));const files=[];
for(const n of names){const original=path.join(root,n),b=fs.readFileSync(original),relative='raw/'+n,target=path.join(dest,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b,{flag:'wx'});files.push({path:relative,original,bytes:b.length,sha256:id.hash(b)});}
fs.writeFileSync(path.join(dest,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,oldFailedCases:before.failed,newPassedCases:after.passed,actualDriverExitCode:2,sdkCompiled:false,releaseReady:false}));
