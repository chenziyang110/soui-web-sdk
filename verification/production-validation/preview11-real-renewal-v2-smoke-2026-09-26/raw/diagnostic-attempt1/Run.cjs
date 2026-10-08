'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto');
const root=__dirname,source='C:/SOUI-SDK-production-validation/preview11-real-renewal-preparation-v2-20260926/attempts/x64-attempt1/runtime',runtime=path.join(root,'runtime');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
const walk=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>{if(e.isSymbolicLink())throw Error('Linked input');return e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)];});
const snapshot=p=>walk(p).map(f=>({path:path.relative(p,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:hash(fs.readFileSync(f))})).sort((a,b)=>a.path.localeCompare(b.path));
async function main(){
 const gate=cp.spawnSync('powershell.exe',['-NoProfile','-Command',"$busy=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(qemu-system-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests)\\.exe$' }); if($busy.Count){$busy|Select-Object ProcessId,Name|ConvertTo-Json;exit 1}"],{windowsHide:true,timeout:20000});
 write(path.join(root,'gate.stdout.log'),gate.stdout||Buffer.alloc(0));write(path.join(root,'gate.stderr.log'),gate.stderr||Buffer.alloc(0));if(gate.status!==0||gate.error)throw Error('Desktop occupied');
 const before=snapshot(source).filter(f=>f.path!=='source-navigation.log');
 for(const f of before)write(path.join(runtime,f.path),fs.readFileSync(path.join(source,f.path)));
 const index=path.join(runtime,'resources/index.html'),original=fs.readFileSync(index,'utf8');write(path.join(root,'index-before.html'),original);
 const diagnostic='<script>(function(){var messages=[];function show(m){messages.push(String(m).slice(0,600));var e=document.getElementById("details");if(e)e.textContent=messages.join("\\n");}window.addEventListener("error",function(e){show("ERROR: "+e.message+" @ "+e.filename+":"+e.lineno);});window.addEventListener("load",function(){show("LOADED System="+typeof window.System+" native="+typeof window.__souiNativePostMessage+" public="+typeof window.souiNative);});})();</script>';
 if(!original.includes('<head>'))throw Error('No head');fs.writeFileSync(index,original.replace('<head>','<head>'+diagnostic));
 write(path.join(root,'payload-before.json'),JSON.stringify({sourceRuntime:source,sourcePayload:before,diagnosticPayload:snapshot(runtime),diagnosticOnly:true,guiPassClaim:false},null,2)+'\n');
 const exe=path.join(runtime,'InstalledRenewalProbe.exe'),args=['--runtime',runtime,'--report',path.join(root,'native.ndjson'),'--duration-ms','30000'];
 const out=fs.openSync(path.join(root,'stdout.log'),'wx'),err=fs.openSync(path.join(root,'stderr.log'),'wx');
 const env={...process.env};for(const k of Object.keys(env))if(k.startsWith('SOUI_'))delete env[k];
 const startedAt=new Date().toISOString(),child=cp.spawn(exe,args,{cwd:runtime,env,windowsHide:true,stdio:['ignore',out,err]});
 write(path.join(root,'launch.json'),JSON.stringify({pid:child.pid,program:exe,args,startedAt,diagnosticOnly:true})+'\n');
 let timedOut=false;const timeout=setTimeout(()=>{timedOut=true;child.kill();},90000);
 const result=await new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));clearTimeout(timeout);fs.closeSync(out);fs.closeSync(err);
 write(path.join(root,'result.json'),JSON.stringify({...result,timedOut,pid:child.pid,startedAt,finishedAt:new Date().toISOString(),payloadAfter:snapshot(runtime),diagnosticOnly:true,productionPass:false},null,2)+'\n');
 console.log(JSON.stringify(result));process.exitCode=result.exitCode||1;
}
main().catch(e=>{console.error(e.stack);process.exitCode=2;});
