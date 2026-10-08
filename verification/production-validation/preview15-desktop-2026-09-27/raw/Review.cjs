'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs')),evidence=require(path.join(repo,'tools/internal/build-evidence.cjs'));
const sourceSha256='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';assert.equal(id.source(repo).sha256,sourceSha256);
const controls=['Run.cjs','Capture-Command.cjs','Review.cjs'].map(name=>{const bytes=fs.readFileSync(path.join(root,name));return{path:name,bytes:bytes.length,sha256:id.hash(bytes)};});
const directories=fs.readdirSync(path.join(root,'runs')).sort();assert.equal(directories.length,8);const cases=[];let nativeRuns=0;
for(const name of directories){
 const dir=path.join(root,'runs',name),read=name=>id.readJson(path.join(dir,name));
 const result=read('result.json'),launch=read('launch.json'),build=read('build-before.json');
 assert.equal(build.version,'0.3.0-preview.15');assert.equal(build.source.sha256,sourceSha256);assert.equal(result.exitCode,0);assert.equal(result.timedOut,false);assert.equal(result.interference,false);assert.equal(result.spawnError,null);
 assert.equal(launch.driverSha256,controls[0].sha256);const arch=build.architecture;
 for(const g of [read('gate-before.json'),...read('gates-during.json'),read('gate-after.json')]){assert.equal(g.exitCode,0);assert.equal(g.error,null);assert.equal(g.stdout.trim(),'');}
 const entry={run:name,architecture:arch,originalExitCode:result.exitCode};
 if(fs.existsSync(path.join(dir,'probe-input.json'))){
  const probe=read('probe-input.json'),bytes=fs.readFileSync(probe.program);assert.equal(id.hash(bytes),probe.sha256);assert.equal(bytes.readUInt16LE(bytes.readUInt32LE(0x3c)+4),arch==='x86'?0x14c:0x8664);entry.kind=probe.args[0];entry.nativeExitCodes=[result.exitCode];++nativeRuns;
 }else if(fs.existsSync(path.join(dir,'smoke.json'))){
  const smoke=read('smoke.json');assert.equal(smoke.passed,true);assert.equal(smoke.buildId,build.buildId);assert.equal(smoke.runs.length,3);evidence.verifySmoke(repo,arch,'Release');
  for(const run of smoke.runs){assert.equal(run.exitCode,0);assert.equal(run.timedOut,false);assert.equal(run.passed,true);assert.match(run.log,/application-ready/);assert.match(run.log,/host-close-complete/);}
  entry.kind='current-apps';entry.nativeExitCodes=smoke.runs.map(r=>r.exitCode);nativeRuns+=3;
 }else{
  const report=read('rebuilt-smoke.json');assert.equal(report.passed,true);assert.equal(report.architecture,arch);assert.equal(report.runs.length,2);
  for(const run of report.runs){assert.equal(run.exitCode,0);assert.equal(run.timedOut,false);assert.equal(run.passed,true);assert.match(run.log,/application-ready/);assert.match(run.log,/host-close-complete/);}
  const artifacts=id.readJson(path.join(repo,`out/dependencies/${arch}/build-All.json`)).artifacts;
  const replacements=new Map(artifacts.map(a=>[a.kind==='miniblink'?'node.dll':a.kind+'.dll',a]));assert.equal(replacements.size,6);
  const payloads=read('rebuilt-payloads.json');assert.equal(payloads.length,2);
  for(const p of payloads){
   const expected=build.payloads[p.target].files.map(f=>{const a=replacements.get(f.path);if(!a)return f;const bytes=fs.readFileSync(a.path);assert.equal(id.hash(bytes),a.sha256.toLowerCase());return{path:f.path,bytes:bytes.length,sha256:id.hash(bytes)};});
   assert.deepEqual(p.payload.files,expected);id.verifyPayload(p.runtime,p.target,p.payload);
  }
  entry.kind='freshly-built-dependencies';entry.nativeExitCodes=report.runs.map(r=>r.exitCode);nativeRuns+=2;
 }
 cases.push(entry);
}
assert.equal(nativeRuns,14);
const commandNames=fs.readdirSync(path.join(root,'command-logs'));assert.equal(commandNames.length,9);
for(const name of commandNames){const base=path.join(root,'command-logs',name),r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,name==='x86-window-thread-timeout-attempt1'?2:0);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const stream of ['stdout','stderr']){const bytes=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(bytes.length,r[stream].bytes);assert.equal(id.hash(bytes),r[stream].sha256);}}
const review={version:'0.3.0-preview.15',sourceSha256,controls,cases,nativeRuns,nativeExitCodesAllZero:true,fullPayloadsMatched:true,noExternalDesktopLoadObserved:true,firstAttemptBlockedBeforeLaunch:true,realRenewalVerified:false,productionPass:false};
fs.writeFileSync(path.join(root,'review.json'),id.encoded(review),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/preview15-desktop-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const name of id.walk(root)){const original=path.join(root,name),bytes=fs.readFileSync(original),dest=path.join(target,'raw',name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});files.push({path:'raw/'+name,original,bytes:bytes.length,sha256:id.hash(bytes)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,version:review.version,nativeRuns,allNativeExitCodesZero:true,productionPass:false}));
