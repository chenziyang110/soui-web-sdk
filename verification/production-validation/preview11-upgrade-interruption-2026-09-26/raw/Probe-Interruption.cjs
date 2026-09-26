'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto');
const root=__dirname,dir=path.join(root,'interruption-attempt1'),project=path.join(dir,'project');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const save=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
const json=(p,v)=>save(p,JSON.stringify(v,null,2)+'\n');
const inputs=read(path.join(root,'inputs.json')),business=read(path.join(root,'business-input.json'));
const tool=path.join(inputs.packages.current.copy,'payload/tools/sdk/soui-sdk.cjs');
function rawRun(name,args){
 const start=new Date().toISOString(),r=cp.spawnSync(process.execPath,args,{windowsHide:true,maxBuffer:8*1024*1024,timeout:120000});
 const log=path.join(dir,'commands',name);save(path.join(log,'stdout.log'),r.stdout||Buffer.alloc(0));save(path.join(log,'stderr.log'),r.stderr||Buffer.alloc(0));
 const result={program:process.execPath,args,startedAt:start,finishedAt:new Date().toISOString(),exitCode:r.status,signal:r.signal,error:r.error?.message||null};
 json(path.join(log,'result.json'),result);return result;
}
function businessUnchanged(){for(const f of business.files){const b=fs.readFileSync(path.join(project,f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)return false;}return true;}
async function main(){
 if(fs.existsSync(dir))throw Error('Refuse existing interruption sample');fs.mkdirSync(dir);
 for(const f of business.files){const b=fs.readFileSync(path.join(root,'project',f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)throw Error('Business input changed');save(path.join(project,f.path),b);}
 const initial=rawRun('initial-install',[tool,'install','--package',inputs.packages.old.copy,'--project',project]);if(initial.exitCode!==0)throw Error('Initial installation failed');
 save(path.join(dir,'lock-before.json'),fs.readFileSync(path.join(project,'sdk-lock.json')));
 const oldLock=read(path.join(project,'sdk-lock.json'));
 const boundary=path.join(dir,'boundary.json'),out=fs.openSync(path.join(dir,'child.stdout.log'),'wx'),err=fs.openSync(path.join(dir,'child.stderr.log'),'wx');
 const record={startedAt:new Date().toISOString(),program:process.execPath,args:[path.join(root,'Interrupt-Child.cjs'),project,inputs.packages.current.copy,boundary],pid:null,exitCode:null,signal:null,terminatedByProbe:false};
 const child=cp.spawn(process.execPath,record.args,{windowsHide:true,stdio:['ignore',out,err]});record.pid=child.pid;
 let exited=false;const done=new Promise(resolve=>{child.once('error',e=>{record.error=e.message;});child.once('close',(code,signal)=>{record.exitCode=code;record.signal=signal;exited=true;resolve();});});
 const start=Date.now();while(!fs.existsSync(boundary)&&!exited&&Date.now()-start<30000)await new Promise(resolve=>setTimeout(resolve,25));
 if(fs.existsSync(boundary)){
  const b=read(boundary);if(b.pid!==child.pid)throw Error('Boundary process identity mismatch');
  record.boundary=b;record.terminatedByProbe=child.kill();
 }else if(!exited){record.timeoutBeforeBoundary=true;child.kill();}
 await done;fs.closeSync(out);fs.closeSync(err);record.finishedAt=new Date().toISOString();json(path.join(dir,'child.result.json'),record);
 if(!record.boundary||!record.terminatedByProbe)throw Error('No real interruption captured');
 const verify=rawRun('verify-after-interruption',[tool,'verify-project','--project',project]);
 const install=rawRun('retry-current-install',[tool,'install','--package',inputs.packages.current.copy,'--project',project]);
 const stages=fs.readdirSync(project).filter(n=>n.startsWith('.sdk-install-'));
 const inventory=[];let recoverable=true;
 for(const f of oldLock.manifest.files){
  const candidates=[path.join(project,f.path),...stages.map(s=>path.join(project,s,'old',f.path))];
  const matching=candidates.filter(p=>fs.existsSync(p)&&fs.statSync(p).isFile()).find(p=>{const b=fs.readFileSync(p);return b.length===f.bytes&&hash(b)===f.sha256;});
  if(!matching)recoverable=false;inventory.push({path:f.path,oldBytesRecoverable:!!matching,location:matching?path.relative(project,matching).replaceAll('\\','/'):null});
 }
 json(path.join(dir,'old-input-recovery-inventory.json'),inventory);
 const sameLock=fs.readFileSync(path.join(dir,'lock-before.json')).equals(fs.readFileSync(path.join(project,'sdk-lock.json')));
 const result={schema:'soui.real-installer-interruption/v1',finishedAt:new Date().toISOString(),interruption:'actual child termination after completed rename; no exception injection',oldVersion:inputs.packages.old.version,newVersion:inputs.packages.current.version,child:record,verifyExitCode:verify.exitCode,retryInstallExitCode:install.exitCode,oldLockUnchanged:sameLock,businessFilesPreserved:businessUnchanged(),oldInputsRecoverableFromLiveAndBackup:recoverable,remainingInstallDirectories:stages,automaticRecoveryVerified:verify.exitCode===0||install.exitCode===0,guiExecuted:false,releaseReady:false};
 json(path.join(dir,'result.json'),result);console.log(JSON.stringify(result));
 if(verify.exitCode!==0||install.exitCode!==0)process.exitCode=1;
}
main().catch(e=>{console.error(e.stack||e.message);process.exitCode=2;});
