'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const {hash,write,encode,snapshot,verify}=require('./Prepare.cjs');
const root=__dirname,read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const helperEnv={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
function input(){const p=read(path.join(root,'preparation.json'));assert.equal(hash(fs.readFileSync(path.join(p.parent,'freeze.json'))),p.expectedFreeze);for(const f of p.controls){const b=fs.readFileSync(path.join(root,f.path));assert.equal(b.length,f.bytes);assert.equal(hash(b),f.sha256,f.path);}verify(p.parentRuntime,p.parentPayload);verify(path.join(root,'input/runtime'),p.payload);return p;}
function gate(ownPid){const script="$busy=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests)\\.exe$' -and $_.ProcessId -ne "+Number(ownPid||0)+" }); $busy|Select-Object ProcessId,Name,CreationDate|ConvertTo-Json -Compress; if($busy.Count){exit 1}";const result=cp.spawnSync('powershell.exe',['-NoProfile','-Command',script],{env:helperEnv,windowsHide:true,timeout:20000});return{exitCode:result.status,error:result.error?.message||null,stdout:(result.stdout||Buffer.alloc(0)).toString(),stderr:(result.stderr||Buffer.alloc(0)).toString(),at:new Date().toISOString()};}
async function main(){
 const p=input();if(process.argv[2]==='--verify-only'&&process.argv.length===3){console.log(encode({verified:true,files:p.payload.length,guiExecuted:false,releaseReady:false}));return;}
 assert.equal(process.argv[2],'--run');assert.equal(process.argv.length,3);
 const before=gate(0);if(before.exitCode!==0||before.error){console.log(encode({status:'desktop-busy-no-launch',gate:before}));process.exitCode=2;return;}
 const run=path.join(root,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid),runtime=path.join(run,'runtime');
 write(path.join(run,'gate-before.json'),encode(before));
 for(const f of p.payload)write(path.join(runtime,f.path),fs.readFileSync(path.join(root,'input/runtime',f.path)));verify(runtime,p.payload);
 const again=gate(0);write(path.join(run,'gate-prelaunch.json'),encode(again));if(again.exitCode!==0||again.error){write(path.join(run,'result.json'),encode({status:'desktop-busy-no-launch',guiExecuted:false,releaseReady:false}));process.exitCode=2;return;}
 const exe=path.join(runtime,'InstalledRenewalProbe.exe'),args=['--runtime',runtime,'--report',path.join(run,'native.ndjson'),'--duration-ms','30000'];
 const env={...process.env};for(const key of Object.keys(env))if(key.startsWith('SOUI_'))delete env[key];
 const out=fs.openSync(path.join(run,'stdout.log'),'wx'),err=fs.openSync(path.join(run,'stderr.log'),'wx');
 const startedAt=new Date().toISOString(),child=cp.spawn(exe,args,{cwd:runtime,env,windowsHide:false,stdio:['ignore',out,err]});
 write(path.join(run,'launch.json'),encode({pid:child.pid||null,program:exe,args,startedAt,inputSha256:hash(fs.readFileSync(path.join(root,'preparation.json'))),diagnosticOnly:true}));
 let timedOut=false,externalLoad=false,spawnError=null,closed=false;const gates=[],timers=[];
 child.once('error',e=>{spawnError=String(e.stack||e);});
 function monitor(){if(closed)return;const check=gate(child.pid);gates.push(check);if(check.exitCode!==0||check.error){externalLoad=true;child.kill();return;}timers.push(setTimeout(monitor,5000));}
 timers.push(setTimeout(monitor,5000));
 for(const delay of [2000,40000])timers.push(setTimeout(()=>{
  if(closed||!child.pid)return;const capture=cp.spawnSync('powershell.exe',['-NoProfile','-File',path.join(root,'Capture-OwnedProbe.ps1'),'-ProbePid',String(child.pid),'-ExpectedExecutable',exe,'-OutputFile',path.join(run,'page-'+delay+'.png')],{env:helperEnv,windowsHide:true,timeout:10000});
  write(path.join(run,'capture-'+delay+'.stdout.log'),capture.stdout||Buffer.alloc(0));write(path.join(run,'capture-'+delay+'.stderr.log'),capture.stderr||Buffer.alloc(0));write(path.join(run,'capture-'+delay+'.result.json'),encode({exitCode:capture.status,error:capture.error?.message||null}));
 },delay));
 timers.push(setTimeout(()=>{timedOut=true;child.kill();},90000));
 const result=await new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));closed=true;timers.forEach(clearTimeout);fs.closeSync(out);fs.closeSync(err);
 write(path.join(run,'gates-during.json'),encode(gates));write(path.join(run,'gate-after.json'),encode(gate(0)));write(path.join(run,'payload-after.json'),encode(snapshot(runtime)));
 let identityError=null;try{verify(runtime,p.payload);input();}catch(error){identityError=String(error.stack);}
 write(path.join(run,'result.json'),encode({...result,timedOut,externalLoad,spawnError,identityError,pid:child.pid||null,startedAt,finishedAt:new Date().toISOString(),diagnosticOnly:true,productionPass:false}));
 console.log(encode({run,...result,timedOut,externalLoad,identityError,diagnosticOnly:true,productionPass:false}));process.exitCode=result.exitCode===0&&!timedOut&&!externalLoad&&!spawnError&&!identityError?0:1;
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
