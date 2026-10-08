'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',source='C:/SOUI-SDK-source-validation/preview15-no-git';
const id=require(path.join(source,'tools/internal/delivery-identity.cjs')),evidence=require(path.join(source,'tools/internal/build-evidence.cjs'));
const sourceSha256='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';assert.equal(id.source(source).sha256,sourceSha256);assert(!fs.existsSync(path.join(source,'.git')));
const archive=id.readJson(path.join(root,'../relocate-result.json'));assert.equal(id.hash(fs.readFileSync(path.join(source,'source-manifest.json'))),archive.sourceManifestSha256);
const runs=fs.readdirSync(path.join(root,'runs')).sort();assert.equal(runs.length,2);const cases=[];
for(const name of runs){const base=path.join(root,'runs',name),read=n=>id.readJson(path.join(base,n));const result=read('result.json'),launch=read('launch.json'),build=read('build-before.json'),smoke=read('smoke.json');
 assert.equal(build.source.sha256,sourceSha256);assert.equal(build.version,'0.3.0-preview.15');assert.equal(result.exitCode,0);assert.equal(result.timedOut,false);assert.equal(result.interference,false);assert.equal(result.spawnError,null);assert.equal(launch.driverSha256,id.hash(fs.readFileSync(path.join(root,'Run.cjs'))));
 for(const gate of [read('gate-before.json'),...read('gates-during.json'),read('gate-after.json')]){assert.equal(gate.exitCode,0);assert.equal(gate.error,null);assert.equal(gate.stdout.trim(),'');}
 assert.equal(smoke.buildId,build.buildId);assert.equal(smoke.passed,true);assert.equal(smoke.runs.length,3);evidence.verifySmoke(source,build.architecture,'Release');
 for(const run of smoke.runs){assert.equal(run.exitCode,0);assert.equal(run.timedOut,false);assert.equal(run.passed,true);assert.match(run.log,/application-ready/);assert.match(run.log,/host-close-complete/);}
 cases.push({run:name,architecture:build.architecture,buildId:build.buildId,nativeExitCodes:smoke.runs.map(r=>r.exitCode),payloads:build.payloads});
}
for(const name of fs.readdirSync(path.join(root,'command-logs'))){const base=path.join(root,'command-logs',name),record=id.readJson(path.join(base,'result.json'));assert.equal(record.exitCode,name==='x86-apps-attempt1'?2:0);assert.equal(record.timedOut,false);assert.equal(record.error,null);for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(b.length,record[stream].bytes);assert.equal(id.hash(b),record[stream].sha256);}}
const report={version:'0.3.0-preview.15',sourceSha256,archiveSha256:archive.archiveSha256,sourceManifestSha256:archive.sourceManifestSha256,cases,nativeRuns:6,nativeExitCodesAllZero:true,fullPayloadsAndLegalMatched:true,noExternalDesktopLoadObserved:true,firstAttemptBlockedBeforeGui:true,sourceDeliveryVerified:false,realRenewalVerified:false,releaseReady:false};
fs.writeFileSync(path.join(root,'review.json'),id.encoded(report),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/preview15-relocated-desktop-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const name of id.walk(root)){const original=path.join(root,name),b=fs.readFileSync(original),relative='raw/'+name,dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:relative,original,bytes:b.length,sha256:id.hash(b)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});console.log(JSON.stringify({rawFiles:files.length,nativeRuns:6,allExitCodesZero:true,releaseReady:false}));
