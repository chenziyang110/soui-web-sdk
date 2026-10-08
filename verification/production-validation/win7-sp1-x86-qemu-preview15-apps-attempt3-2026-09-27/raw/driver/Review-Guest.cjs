'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const prep=path.resolve(__dirname,'../preview15-win7-preparation-v3-20260927');
const attempt=path.resolve(__dirname,'../preview15-win7-apps-attempt3'),failed=path.resolve(__dirname,'../preview15-win7-apps-attempt2');
const read=n=>id.readJson(path.join(attempt,n));
const text=b=>(b[0]===255&&b[1]===254?b.subarray(2).toString('utf16le'):b.toString('utf8').replace(/^\uFEFF/,''));
const launch=read('launch.json'),result=read('qemu-result.json'),samples=read('gates-during.json'),sent=read('command-send.json'),ready=read('desktop-ready.json');
assert.equal(launch.pid,66200);assert.equal(result.pid,launch.pid);assert.equal(result.exitCode,0);assert.equal(result.externalLoadObserved,false);assert.equal(result.timedOut,false);assert.equal(result.gateFailure,null);
assert.equal(result.diskUnchanged,true);assert.equal(result.diskSha256Before,'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');assert.equal(result.diskSha256After,result.diskSha256Before);assert.equal(result.frozenCheckExitCode,0);
assert.equal(read('gate-before-launch.json').processes.length,0);assert(samples.length>20);assert(samples.every(s=>s.processes.length===0&&s.error===null));
assert.equal(sent.pid,launch.pid);assert.equal(ready.pid,launch.pid);assert.equal(sent.command,'cmd.exe /c D:\\soui-input\\run-preview15.cmd');assert(Date.parse(ready.observedAt)<Date.parse(sent.sentAt));assert.equal(ready.otherDesktopProcesses.length,0);
assert.equal(id.hash(fs.readFileSync(path.join(attempt,ready.screenshot))),ready.screenshotSha256);
const frozen={};for(const when of ['before','after']){const f=JSON.parse(text(fs.readFileSync(path.join(attempt,'frozen-check-'+when+'.log'))));assert.equal(f.status,'frozen-preparation-intact');assert.equal(f.files,612);assert.equal(f.freezeSha256,'47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005');assert.equal(f.buildId,'b8c26d8d-7144-4eb0-b889-0034ec8029fb');frozen[when]=f;}assert.deepEqual(frozen.before,frozen.after);
assert.equal(id.hash(fs.readFileSync(path.join(prep,'freeze.json'))),frozen.before.freezeSha256);
assert.equal(id.hash(fs.readFileSync(path.join(attempt,'launch-script.ps1'))),id.hash(fs.readFileSync(path.join(__dirname,'Launch-Current.ps1'))));
const commands={};for(const [name,expected] of [['guest-attempt2',1],['guest-attempt3',0],['verify-guest-attempt3',0]]){const base=path.join(__dirname,'command-logs',name),r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,expected);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(b.length,r[stream].bytes);assert.equal(id.hash(b),r[stream].sha256);}commands[name]=r;}
assert(!fs.existsSync(path.join(failed,'launch.json')));assert(!fs.existsSync(path.join(failed,'serial-export.txt')));assert(!fs.existsSync(path.join(failed,'command-send.json')));
assert.match(fs.readFileSync(path.join(__dirname,'command-logs/guest-attempt2/stderr.log'),'utf8'),/Get-FileHash/);
assert.match(fs.readFileSync(path.join(__dirname,'command-logs/guest-attempt3/stdout.log'),'utf8'),/processModulePath/);
const verifier=require(path.join(prep,'Verify-AppSerial.cjs')),serial=fs.readFileSync(path.join(attempt,'serial-export.txt')),before=id.hash(serial),input=id.readJson(path.join(prep,'input.json'));
const verified=verifier.verify(serial,input),decoded=verifier.decodeSerial(serial);assert.equal(decoded.size,37);assert.equal(verified.applications,3);
assert.equal(id.hash(fs.readFileSync(path.join(attempt,'serial-export.txt'))),before);
const decodedRoot=path.join(__dirname,'guest-attempt3-decoded');assert(!fs.existsSync(decodedRoot));fs.mkdirSync(decodedRoot);
for(const [name,b] of decoded)fs.writeFileSync(path.join(decodedRoot,name),b,{flag:'wx'});
const runs=['starter','workbench-default','workbench-react'].map(name=>({name,record:text(decoded.get(name+'-run.txt')),smoke:text(decoded.get(name+'-smoke.log')),nativeExitCode:Number(text(decoded.get(name+'-exitcode.txt')).trim())}));
assert(runs.every(r=>r.nativeExitCode===0));
const report={version:'0.3.0-preview.15',classification:'verified-functional-smoke-evidence',buildId:verified.buildId,sourceSha256:verified.sourceSha256,freezeSha256:frozen.before.freezeSha256,serialSha256:before,serialBytes:serial.length,serialFiles:decoded.size,strictValidator:verified,guestIdentity:text(decoded.get('guest-identity.txt')),harness:text(decoded.get('harness.log')),runs,fullPayloadAndLegalBeforeAndAfterMatched:true,desktopReadyBeforeSingleCommand:true,observedExternalLoad:false,loadSamples:samples.length,baseDiskUnchanged:true,originalDriverExitCode:commands['guest-attempt3'].exitCode,qemuExitCode:result.exitCode,priorAttempt2:{classification:'host-powershell-module-path-startup-failure',driverExitCode:1,qemuLaunched:false,guestCommandSent:false,correction:'Run-Guest.ps1 sets only its own process PSModulePath to Windows PowerShell system modules and verifies Get-FileHash before launching the unchanged guest driver.'},sourceReproductionClaim:false,realRenewalVerified:false,releaseReady:false};
fs.writeFileSync(path.join(__dirname,'guest-attempt3-review.json'),id.encoded(report),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt3-2026-09-27');assert(!fs.existsSync(target));const files=[];
function copy(original,rel){const b=fs.readFileSync(original),dest=path.join(target,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});files.push({path:rel,original,bytes:b.length,sha256:id.hash(b)});}
for(const [folder,prefix] of [[attempt,'attempt3'],[failed,'attempt2'],[decodedRoot,'decoded']])for(const n of id.walk(folder)){assert(!/^vl\./.test(n),'Do not commit temporary VM disk snapshots');copy(path.join(folder,n),'raw/'+prefix+'/'+n);}
for(const n of ['Launch-Current.ps1','Run-Guest.ps1','Monitor.cjs','Capture-Command.cjs','Review-Guest.cjs','guest-attempt3-review.json'])copy(path.join(__dirname,n),'raw/driver/'+n);
for(const name of Object.keys(commands))for(const n of id.walk(path.join(__dirname,'command-logs',name)))copy(path.join(__dirname,'command-logs',name,n),'raw/commands/'+name+'/'+n);
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,serialSha256:before,serialFiles:decoded.size,guest:verified.guest,nativeExitCodes:runs.map(r=>r.nativeExitCode),loadSamples:samples.length,releaseReady:false}));
