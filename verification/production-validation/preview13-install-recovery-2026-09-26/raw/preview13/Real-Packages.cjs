'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray';
const installer=path.join(repo,'tools/sdk/soui-sdk.cjs'),sdk=require(installer),id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const prior='C:/SOUI-SDK-production-validation/preview11-session-renewal-20260926/sdk-package-attempt1',next=path.join(root,'sdk-package');
const businessRoot='C:/SOUI-SDK-production-validation/preview11-upgrade-attempt2-20260926/project';
const business=JSON.parse(fs.readFileSync('C:/SOUI-SDK-production-validation/preview11-upgrade-attempt2-20260926/business-input.json','utf8'));
const encoded=v=>JSON.stringify(v,null,2)+'\n';
function save(file,value){fs.writeFileSync(path.join(root,file),encoded(value),{flag:'wx'});}
function command(project,name,verb,pkg){
 const args=[installer,verb,'--project',project];if(pkg)args.push('--package',pkg);
 const result=cp.spawnSync(process.execPath,args,{windowsHide:true,timeout:60000});
 const record={args,exitCode:result.status,signal:result.signal,error:result.error?.message};
 for(const key of ['stdout','stderr']){const bytes=result[key]||Buffer.alloc(0);fs.writeFileSync(path.join(root,name+'.'+key+'.log'),bytes,{flag:'wx'});record[key]={bytes:bytes.length,sha256:id.hash(bytes)};}
 save(name+'.json',record);return record;
}
function verifyBusiness(project){for(const file of business.files){const b=fs.readFileSync(path.join(project,file.path));assert.equal(b.length,file.bytes);assert.equal(id.hash(b),file.sha256);}return business.files.length;}
async function interrupt(project,boundary){
 const report=path.join(root,boundary+'-boundary.json');
 const args=[path.join(root,'Real-Worker.cjs'),installer,project,next,boundary,report];
 const child=cp.spawn(process.execPath,args,{windowsHide:true,stdio:['ignore','pipe','pipe']});
 const stdout=[],stderr=[];child.stdout.on('data',b=>stdout.push(b));child.stderr.on('data',b=>stderr.push(b));
 let ended=false;const closed=new Promise(resolve=>child.on('close',(code,signal)=>{ended=true;resolve({code,signal});}));
 const startedAt=new Date().toISOString(),deadline=Date.now()+60000;
 while(!fs.existsSync(report)&&!ended&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,10));
 const reached=fs.existsSync(report);if(!ended)assert.equal(child.kill('SIGKILL'),true);
 const result=await closed;save(boundary+'-child.json',{args,pid:child.pid,startedAt,finishedAt:new Date().toISOString(),reached,...result});
 fs.writeFileSync(path.join(root,boundary+'-child.stdout.log'),Buffer.concat(stdout),{flag:'wx'});
 fs.writeFileSync(path.join(root,boundary+'-child.stderr.log'),Buffer.concat(stderr),{flag:'wx'});
 assert.equal(reached,true);assert.equal(JSON.parse(fs.readFileSync(report)).pid,child.pid);assert.ok(result.signal||result.code!==0);
}
(async()=>{
 const input=id.source(repo);const oldManifest=sdk.readPackage(prior),newManifest=sdk.readPackage(next);
 assert.equal(require(path.join(repo,'tools/internal/build-evidence.cjs')).version(repo),'0.3.0-preview.13');
 save('inputs.json',{source:input,oldPackage:{path:prior,manifest:oldManifest},newPackage:{path:next,manifest:newManifest},business});
 const stages=[];
 for(const boundary of ['replace','commit']){
  const project=path.join(root,'real-'+boundary);fs.mkdirSync(project);
  for(const file of business.files){const source=path.join(businessRoot,file.path),target=path.join(project,file.path);const b=fs.readFileSync(source);assert.equal(id.hash(b),file.sha256);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b,{flag:'wx'});}
  assert.equal(command(project,boundary+'-old-install','install',prior).exitCode,0);
  verifyBusiness(project);const before=fs.readFileSync(path.join(project,'sdk-lock.json'));
  await interrupt(project,boundary);
  assert.equal(command(project,boundary+'-pending-verify','verify-project').exitCode,1);
  assert.equal(command(project,boundary+'-recover','recover').exitCode,0);
  const recovered=sdk.readLock(project);assert.equal(recovered.manifest.version,boundary==='commit'?'0.3.0-preview.13':'0.3.0-preview.11');
  if(boundary==='replace')assert.deepEqual(fs.readFileSync(path.join(project,'sdk-lock.json')),before);
  verifyBusiness(project);
  for(const [name,pkg,version]of [['upgrade',next,'0.3.0-preview.13'],['rollback',prior,'0.3.0-preview.11'],['restore',next,'0.3.0-preview.13']]){
   assert.equal(command(project,boundary+'-'+name,'install',pkg).exitCode,0);
   const lock=sdk.readLock(project);assert.equal(lock.manifest.version,version);verifyBusiness(project);
   save(boundary+'-'+name+'-lock.json',lock);
  }
  stages.push({boundary,recoveredVersion:recovered.manifest.version,businessFiles:verifyBusiness(project),oldFiles:oldManifest.files.length,newFiles:newManifest.files.length,finalVersion:sdk.readLock(project).manifest.version});
 }
 assert.equal(id.source(repo).sha256,input.sha256);sdk.readPackage(prior);sdk.readPackage(next);
 save('result.json',{finishedAt:new Date().toISOString(),sourceSha256:input.sha256,stages,realProcessTermination:true,businessPreserved:true,guiExecuted:false,nativeBuildExecuted:false,legacyRecoveryVerified:false,releaseReady:false});
 console.log(encoded({status:'verified-real-package-recovery',stages,sourceSha256:input.sha256,releaseReady:false}));
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
