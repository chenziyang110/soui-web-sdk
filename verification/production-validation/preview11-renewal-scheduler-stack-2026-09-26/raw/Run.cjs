'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,arch=process.argv[2],expected=process.argv[3];assert.equal(arch,'x86');assert(/^[a-f0-9]{64}$/.test(expected||''));assert.equal(process.argv.length,4);
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),encode=v=>JSON.stringify(v,null,2)+'\n',read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
const walk=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());return e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)];});
const snapshot=p=>walk(p).map(f=>{const b=fs.readFileSync(f);return{path:path.relative(p,f).replaceAll('\\','/'),bytes:b.length,sha256:hash(b)};}).sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
function verify(base,files){assert.deepEqual(snapshot(base).filter(f=>f.path!=='source-navigation.log'),files);}
function input(){const raw=fs.readFileSync(path.join(root,'freeze.json'));assert.equal(hash(raw),expected);const f=JSON.parse(raw);assert.equal(f.schema,'soui.renewal-scheduler-stack/v1');for(const x of [f.pdb,f.debugger])assert.equal(hash(fs.readFileSync(x.path)),x.sha256);for(const c of f.controls){const b=fs.readFileSync(path.join(root,c.path));assert.equal(b.length,c.bytes);assert.equal(hash(b),c.sha256);}for(const r of Object.values(f.runtimes))verify(r.runtime,r.payload.files);return f;}
const helperEnv={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
function gate(own){const script="$busy=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests)\\.exe$' -and $_.ProcessId -ne "+Number(own||0)+" }); $busy|Select-Object ProcessId,Name,CreationDate|ConvertTo-Json -Compress; if($busy.Count){exit 1}";const r=cp.spawnSync('powershell.exe',['-NoProfile','-Command',script],{env:helperEnv,windowsHide:true,timeout:20000});return{exitCode:r.status,error:r.error?.message||null,stdout:r.stdout?.toString()||'',stderr:r.stderr?.toString()||'',at:new Date().toISOString()};}
async function main(){const f=input(),first=gate();if(first.exitCode!==0||first.error){console.log(encode({status:'desktop-busy-no-launch',first}));process.exitCode=2;return;}
 const run=path.join(root,'runs',arch+'-'+new Date().toISOString().replace(/[:.]/g,'-')),runtime=path.join(run,'runtime'),r=f.runtimes[arch];write(path.join(run,'gate-before.json'),encode(first));
 for(const file of r.payload.files)write(path.join(runtime,file.path),fs.readFileSync(path.join(r.runtime,file.path)));verify(runtime,r.payload.files);
 const payload=snapshot(runtime);write(path.join(run,'payload-before.json'),encode(payload));
 const second=gate();write(path.join(run,'gate-prelaunch.json'),encode(second));if(second.exitCode!==0||second.error){write(path.join(run,'result.json'),encode({status:'desktop-busy-no-launch',guiExecuted:false}));process.exitCode=2;return;}
 const exe=path.join(runtime,'InstalledRenewalProbe.exe'),args=['--runtime',runtime,'--report',path.join(run,'native.ndjson'),'--duration-ms','30000'],env={...process.env};for(const key of Object.keys(env))if(key.startsWith('SOUI_'))delete env[key];const out=fs.openSync(path.join(run,'stdout.log'),'wx'),err=fs.openSync(path.join(run,'stderr.log'),'wx');
 const child=cp.spawn(exe,args,{cwd:runtime,env,windowsHide:false,stdio:['ignore',out,err]});write(path.join(run,'launch.json'),encode({pid:child.pid,exe,args,freezeSha256:expected,schedulerStackDiagnosticOnly:true,startedAt:new Date().toISOString()}));
 let debuggerDone=Promise.resolve(),debuggerResult=null;
 let closed=false,timedOut=false,externalLoad=false,spawnError=null;const gates=[],timers=[];child.once('error',e=>{spawnError=String(e);});function monitor(){if(closed)return;const g=gate(child.pid);gates.push(g);if(g.exitCode!==0||g.error){externalLoad=true;child.kill();return;}timers.push(setTimeout(monitor,5000));}timers.push(setTimeout(monitor,5000));timers.push(setTimeout(()=>{timedOut=true;child.kill();},90000));
 timers.push(setTimeout(()=>{
  if(closed||!child.pid)return;
  const debuggerArgs=['-p',String(child.pid),'-pd','-y',path.dirname(f.pdb.path),'-logo',path.join(run,'debugger.log'),'-cf',path.join(root,'scheduler.cdb')];
  write(path.join(run,'debugger-launch.json'),encode({program:f.debugger.path,args:debuggerArgs,ownedNativePid:child.pid,expectedExecutable:exe,startedAt:new Date().toISOString()}));
  const dout=fs.openSync(path.join(run,'debugger.stdout.log'),'wx'),derr=fs.openSync(path.join(run,'debugger.stderr.log'),'wx');
  const debug=cp.spawn(f.debugger.path,debuggerArgs,{windowsHide:true,env:helperEnv,stdio:['ignore',dout,derr]});let debugTimedOut=false,debugError=null;
  const limit=setTimeout(()=>{debugTimedOut=true;debug.kill();},20000);debug.once('error',e=>{debugError=String(e);});
  debuggerDone=new Promise(resolve=>debug.once('close',(exitCode,signal)=>{clearTimeout(limit);fs.closeSync(dout);fs.closeSync(derr);debuggerResult={pid:debug.pid,exitCode,signal,timedOut:debugTimedOut,error:debugError,finishedAt:new Date().toISOString()};write(path.join(run,'debugger-result.json'),encode(debuggerResult));resolve();}));
 },10000));
 const result=await new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));closed=true;timers.forEach(clearTimeout);await debuggerDone;fs.closeSync(out);fs.closeSync(err);write(path.join(run,'gates-during.json'),encode(gates));write(path.join(run,'gate-after.json'),encode(gate()));write(path.join(run,'payload-after.json'),encode(snapshot(runtime)));let identityError=null;try{verify(runtime,payload);input();}catch(e){identityError=String(e.stack);}
 write(path.join(run,'result.json'),encode({...result,pid:child.pid,timedOut,externalLoad,spawnError,identityError,debuggerResult,schedulerStackDiagnosticOnly:true,finishedAt:new Date().toISOString(),realRenewalVerified:false,releaseReady:false}));console.log(encode({run,...result,timedOut,externalLoad,identityError,schedulerStackDiagnosticOnly:true,releaseReady:false}));process.exitCode=result.exitCode===0&&!timedOut&&!externalLoad&&!spawnError&&!identityError?0:1;
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});
