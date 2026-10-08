'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const attempt=path.resolve(__dirname,'../preview15-win7-apps-attempt1'),read=n=>id.readJson(path.join(attempt,n));
const launch=read('launch.json'),result=read('qemu-result.json'),samples=read('gates-during.json');
assert.equal(launch.pid,83580);assert.equal(result.pid,launch.pid);assert.equal(result.exitCode,-1);
assert.equal(result.externalLoadObserved,true);assert.equal(result.timedOut,false);assert.equal(result.gateFailure,null);
assert.equal(result.diskUnchanged,true);assert.equal(result.diskSha256Before,'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');assert.equal(result.diskSha256After,result.diskSha256Before);assert.equal(result.frozenCheckExitCode,0);
assert.equal(read('gate-before-launch.json').processes.length,0);
const interference=samples.filter(s=>s.processes.length);assert.equal(interference.length,1);assert(interference[0].processes.some(p=>p.ProcessId===28876&&p.Name==='qemu-system-x86_64.exe'));
assert(samples.every(s=>s.error===null));assert(!fs.existsSync(path.join(attempt,'command-send.json')));
assert.equal(fs.statSync(path.join(attempt,'serial-export.txt')).size,0);
const frozen={};for(const when of ['before','after']){const b=fs.readFileSync(path.join(attempt,'frozen-check-'+when+'.log'));const text=b.subarray(0,2).equals(Buffer.from([0xff,0xfe]))?b.toString('utf16le'):b.toString('utf8');const f=JSON.parse(text.replace(/^\uFEFF/,''));assert.equal(f.status,'frozen-preparation-intact');assert.equal(f.files,612);assert.equal(f.freezeSha256,'47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005');assert.equal(f.buildId,'b8c26d8d-7144-4eb0-b889-0034ec8029fb');frozen[when]=f;}
assert.deepEqual(frozen.before,frozen.after);
assert.equal(id.hash(fs.readFileSync(path.join(attempt,'launch-script.ps1'))),id.hash(fs.readFileSync(path.join(__dirname,'Launch-Current.ps1'))));
const commandRoot=path.join(__dirname,'command-logs/guest-attempt1'),command=id.readJson(path.join(commandRoot,'result.json'));
assert.equal(command.exitCode,1);assert.equal(command.timedOut,false);assert.equal(command.error,null);
for(const s of ['stdout','stderr']){const b=fs.readFileSync(path.join(commandRoot,s+'.log'));assert.equal(b.length,command[s].bytes);assert.equal(id.hash(b),command[s].sha256);}
const snapshots=fs.readdirSync(attempt).filter(n=>/^vl\./.test(n)).map(name=>({name,bytes:fs.statSync(path.join(attempt,name)).size,sha256:id.hash(fs.readFileSync(path.join(attempt,name)))}));
const report={version:'0.3.0-preview.15',classification:'external-load-interrupted-before-guest-command',qemuPid:launch.pid,qemuExitCode:result.exitCode,driverExitCode:command.exitCode,externalLoad:interference,guestCommandSent:false,guestApplicationExecuted:false,serialBytes:0,frozenBeforeAndAfter:frozen,diskSha256Before:result.diskSha256Before,diskSha256After:result.diskSha256After,desktopScreenshot:{path:'boot-2.png',sha256:id.hash(fs.readFileSync(path.join(attempt,'boot-2.png'))),observed:'Windows 7 desktop, taskbar and QEMU disk driver notification visible; no SDK command sent'},excludedSnapshotFiles:snapshots,applicationFailure:false,win7ApplicationPass:false,releaseReady:false};
fs.writeFileSync(path.join(__dirname,'interrupted-review.json'),id.encoded(report),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt1-2026-09-27');assert(!fs.existsSync(target));const files=[];
function copy(original,rel){const b=fs.readFileSync(original),dest=path.join(target,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:rel,original,bytes:b.length,sha256:id.hash(b)});}
for(const name of id.walk(attempt)){if(/^vl\./.test(name))continue;copy(path.join(attempt,name),'raw/attempt/'+name);}
for(const name of ['Launch.ps1','Launch-Current.ps1','Monitor.cjs','Capture-Command.cjs','Review-Interrupted.v1.cjs','review-attempt1.json','Review-Interrupted.cjs','interrupted-review.json'])copy(path.join(__dirname,name),'raw/driver/'+name);
for(const name of id.walk(commandRoot))copy(path.join(commandRoot,name),'raw/command/'+name);
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,classification:report.classification,guestApplicationExecuted:false,releaseReady:false}));
