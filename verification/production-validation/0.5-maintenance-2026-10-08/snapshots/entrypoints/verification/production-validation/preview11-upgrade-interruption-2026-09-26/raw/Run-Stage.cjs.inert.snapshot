'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto');
const root=__dirname,project=path.join(root,'project');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
const json=(p,v)=>write(p,JSON.stringify(v,null,2)+'\n');
const plan=[['old-initial','old'],['new-upgraded','current'],['old-rolled-back','old'],['new-restored','current']];
const index=Number(process.argv[2]);if(!Number.isInteger(index)||index<0||index>3)throw Error('Stage 0..3 required');
const [name,key]=plan[index],stage=path.join(root,'stages',name),inputs=read(path.join(root,'inputs.json'));
const business=read(path.join(root,'business-input.json'));
const facts=[];
function verifyBusiness(label){for(const f of business.files){const b=fs.readFileSync(path.join(project,f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)throw Error('Business changed: '+f.path);}facts.push({label,at:new Date().toISOString(),businessFiles:business.files.length,preserved:true});}
function verifyInputs(){for(const p of Object.values(inputs.packages))for(const f of p.files){const b=fs.readFileSync(path.join(p.copy,f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)throw Error('Frozen package changed');}}
async function run(label,program,args,timeoutMs=720000){
 const dir=path.join(stage,'commands',label);fs.mkdirSync(dir,{recursive:true});
 const out=fs.openSync(path.join(dir,'stdout.log'),'wx'),err=fs.openSync(path.join(dir,'stderr.log'),'wx');
 const result={label,program,args,cwd:project,startedAt:new Date().toISOString(),exitCode:null,signal:null,timedOut:false,error:null};
 await new Promise(resolve=>{
  const child=cp.spawn(program,args,{cwd:project,windowsHide:true,stdio:['ignore',out,err]});result.pid=child.pid;
  const timer=setTimeout(()=>{result.timedOut=true;if(child.pid)cp.spawnSync('taskkill.exe',['/PID',String(child.pid),'/T','/F'],{windowsHide:true,timeout:15000});},timeoutMs);
  child.once('error',e=>{result.error=e.message;});
  child.once('close',(code,signal)=>{clearTimeout(timer);result.exitCode=code;result.signal=signal;resolve();});
 });
 fs.closeSync(out);fs.closeSync(err);result.finishedAt=new Date().toISOString();
 for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(dir,stream+'.log'));result[stream]={bytes:b.length,sha256:hash(b)};}
 json(path.join(dir,'result.json'),result);console.log(label+' exit='+result.exitCode);
 if(result.exitCode!==0||result.error||result.timedOut)throw Error(label+' failed');
}
async function main(){
 if(fs.existsSync(stage))throw Error('Stage exists; preserve original failure and use explicit new attempt');
 if(index>0){const prior=read(path.join(root,'stages',plan[index-1][0],'result.json'));if(prior.status!=='builds-complete-desktop-pending')throw Error('Previous stage incomplete');}
 fs.mkdirSync(stage,{recursive:true});verifyInputs();verifyBusiness('before-install');
 const tool=path.join(root,'inputs/current-package/payload/tools/sdk/soui-sdk.cjs');
 json(path.join(stage,'lock-before.json'),read(path.join(project,'sdk-lock.json')));
 if(index>0)await run('install',process.execPath,[tool,'install','--package',inputs.packages[key].copy,'--project',project],120000);
 verifyBusiness('after-install');
 await run('verify-project',process.execPath,[tool,'verify-project','--project',project],120000);
 const lock=read(path.join(project,'sdk-lock.json'));
 if(lock.manifest.version!==inputs.packages[key].version)throw Error('Unexpected installed SDK');
 write(path.join(stage,'sdk-lock.json'),fs.readFileSync(path.join(project,'sdk-lock.json')));
 const architectures=[];
 for(const arch of ['x86','x64']){
  const args=['-NoProfile','-File',path.join(project,'tools/Build.ps1'),'-Architecture',arch,'-Configuration','Release'];if(arch==='x64')args.push('-SkipWeb');
  await run('build-'+arch,'powershell.exe',args);
  verifyBusiness('after-build-'+arch);
  const evidence=require(path.join(project,'tools/internal/build-evidence.cjs'));
  const build=evidence.verifyBuild(project,arch,'Release');
  const dir=path.join(stage,arch);fs.mkdirSync(dir);
  const record=path.join(project,'out',arch,'build-Release.json');write(path.join(dir,'build-Release.json'),fs.readFileSync(record));
  const payload=build.payloads.UpgradeLedger,src=path.join(project,'out',arch,'bin/Release/UpgradeLedger'),dst=path.join(dir,'runtime');
  for(const file of payload.files){const b=fs.readFileSync(path.join(src,file.path));if(b.length!==file.bytes||hash(b)!==file.sha256)throw Error('Payload mismatch');write(path.join(dst,file.path),b);}
  for(const log of ['native-Release.log','sdk-web-install.log','sdk-web-build.log','web-install.log','web-build.log']){
   const source=path.join(project,'out',arch,log);if(fs.existsSync(source))write(path.join(dir,log),fs.readFileSync(source));
  }
  architectures.push({architecture:arch,buildId:build.buildId,sourceSha256:build.source.sha256,payloadSha256:payload.sha256,payloadFiles:payload.files.length,buildRecordSha256:hash(fs.readFileSync(record)),guiExecuted:false});
 }
 verifyInputs();verifyBusiness('stage-end');
 json(path.join(stage,'result.json'),{schema:'soui.upgrade-build-stage/v1',stage:name,version:lock.manifest.version,status:'builds-complete-desktop-pending',finishedAt:new Date().toISOString(),architectures,business:facts,guiExecuted:false,upgradeRuntimeVerified:false,sourceDeliveryVerified:false,releaseReady:false});
 console.log(name+' builds complete, GUI pending');
}
main().catch(e=>{console.error(e.stack||e.message);process.exitCode=1;});
