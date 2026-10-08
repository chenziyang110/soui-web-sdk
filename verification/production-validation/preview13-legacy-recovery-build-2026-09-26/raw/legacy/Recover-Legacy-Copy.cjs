'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs')),sdk=require(path.join(repo,'tools/sdk/soui-sdk.cjs'));
const previousRoot='C:/SOUI-SDK-production-validation/preview11-upgrade-attempt2-20260926';
const incident=path.join(previousRoot,'interruption-attempt1'),original=path.join(incident,'project'),project=path.join(root,'project');
const inputs=id.readJson(path.join(previousRoot,'inputs.json')),business=id.readJson(path.join(previousRoot,'business-input.json'));
const candidate='C:/SOUI-SDK-production-validation/preview13-install-recovery-20260926/sdk-package';
const save=(name,bytes)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes,{flag:'wx'});};
const json=(name,value)=>save(name,id.encoded(value));
const snapshot=directory=>id.snapshot(directory,id.walk(directory,''));
function match(file,expected){if(!fs.existsSync(file)||!fs.statSync(file).isFile())return false;const b=fs.readFileSync(file);return b.length===expected.bytes&&id.hash(b)===expected.sha256;}
function businessCheck(directory){for(const f of business.files)assert(match(id.safePath(directory,f.path),f),f.path);}
function command(name,verb,pkg){
 const args=[path.join(repo,'tools/sdk/soui-sdk.cjs'),verb,'--project',project];if(pkg)args.push('--package',pkg);
 const startedAt=new Date().toISOString(),result=cp.spawnSync(process.execPath,args,{windowsHide:true,timeout:120000});
 const record={args,startedAt,finishedAt:new Date().toISOString(),exitCode:result.status,signal:result.signal,error:result.error?.message||null};
 for(const key of ['stdout','stderr']){const b=result[key]||Buffer.alloc(0);save('commands/'+name+'/'+key+'.log',b);record[key]={bytes:b.length,sha256:id.hash(b)};}
 json('commands/'+name+'/result.json',record);return record;
}
function main(){
 assert(!fs.existsSync(project),'Refuse existing recovery copy');
 const sdkSource=id.source(repo),before=snapshot(original),old=sdk.readPackage(inputs.packages.old.copy),attempted=sdk.readPackage(inputs.packages.current.copy),next=sdk.readPackage(candidate);
 assert.equal(old.version,'0.3.0-preview.10');assert.equal(attempted.version,'0.3.0-preview.11');assert.equal(next.version,'0.3.0-preview.13');
 const oldLock=fs.readFileSync(path.join(incident,'lock-before.json'));
 assert.deepEqual(fs.readFileSync(path.join(original,'sdk-lock.json')),oldLock);
 const parsed=JSON.parse(oldLock.toString('utf8').replace(/^\uFEFF/,''));assert.equal(parsed.manifestSha256,id.hash(id.encoded(old)));
 businessCheck(original);
 json('original-before.json',before);save('old-lock.json',oldLock);json('business-input.json',business);
 json('inputs.json',{sdkSource,original,incident,oldPackage:inputs.packages.old.copy,oldManifest:old,attemptedPackage:inputs.packages.current.copy,attemptedManifest:attempted,newPackage:candidate,newManifest:next});
 for(const f of before.files){const source=id.safePath(original,f.path),target=id.safePath(project,f.path);const b=fs.readFileSync(source);assert.equal(id.hash(b),f.sha256);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b,{flag:'wx'});}
 assert.equal(snapshot(project).sha256,before.sha256);json('clone-before.json',snapshot(project));
 assert.equal(command('automatic-recover-refused','recover').exitCode,1);
 assert.equal(command('initial-verification-refused','verify-project').exitCode,1);
 const legacyNames=fs.readdirSync(project).filter(n=>n.startsWith('.sdk-install-'));assert.deepEqual(legacyNames,['.sdk-install-C4ZTbs']);
 const legacy=legacyNames[0],oldMap=new Map(old.files.map(f=>[f.path,f])),nextMap=new Map(attempted.files.map(f=>[f.path,f]));
 const plan=[],newOnly=[];
 // Full preflight precedes every write. Only this known incident is accepted.
 for(const f of old.files){
  const live=id.safePath(project,f.path),backup=id.safePath(project,legacy+'/old/'+f.path);
  const source=match(live,f)?f.path:match(backup,f)?legacy+'/old/'+f.path:null;
  assert(source,'Old SDK bytes unavailable: '+f.path);
  if(fs.existsSync(live))assert(match(live,f)||match(live,nextMap.get(f.path)||{}),'Unexpected live bytes: '+f.path);
  plan.push({path:f.path,source,replace:source!==f.path,bytes:f.bytes,sha256:f.sha256});
 }
 for(const f of attempted.files)if(!oldMap.has(f.path)&&fs.existsSync(id.safePath(project,f.path))){assert(match(id.safePath(project,f.path),f));newOnly.push(f);}
 for(const f of before.files.filter(f=>f.path.startsWith(legacy+'/'))){
  const relative=f.path.slice(legacy.length+1),side=relative.startsWith('old/')?oldMap:relative.startsWith('new/')?nextMap:null;
  assert(side,'Unexpected legacy record');const expected=side.get(relative.slice(4));assert(expected,'Unexpected staged path');assert.equal(f.bytes,expected.bytes);assert.equal(f.sha256,expected.sha256);
 }
 const allowed=new Set([...business.files.map(f=>f.path),...old.files.map(f=>f.path),...attempted.files.map(f=>f.path),'sdk-lock.json']);
 for(const f of before.files)assert(allowed.has(f.path)||f.path.startsWith(legacy+'/'),'Unexpected original project file');
 json('recovery-plan.json',{mode:'explicit-known-incident-copy-only',oldFiles:plan,newOnly,legacy});
 for(const item of plan.filter(f=>f.replace)){
  const from=id.safePath(project,item.source),to=id.safePath(project,item.path),staged=id.safePath(root,'restore-stage/'+item.path);
  assert(match(from,item));fs.mkdirSync(path.dirname(staged),{recursive:true});fs.copyFileSync(from,staged,fs.constants.COPYFILE_EXCL);
  assert(match(staged,item));fs.mkdirSync(path.dirname(to),{recursive:true});fs.renameSync(staged,to);
 }
 for(const f of newOnly){
  const from=id.safePath(project,f.path),to=id.safePath(root,'quarantine/'+f.path);assert(match(from,f));fs.mkdirSync(path.dirname(to),{recursive:true});fs.renameSync(from,to);
 }
 // Move only the checked clone's exact legacy directory; retain every backup.
 const from=path.resolve(project,legacy),to=path.resolve(root,'legacy-evidence');
 assert(from.startsWith(path.resolve(root)+path.sep)&&to.startsWith(path.resolve(root)+path.sep));assert(!fs.existsSync(to));
 fs.renameSync(from,to);
 assert.deepEqual(fs.readFileSync(path.join(project,'sdk-lock.json')),oldLock);businessCheck(project);
 const restored=snapshot(project),expected=[...business.files.map(f=>f.path),...old.files.map(f=>f.path),'sdk-lock.json'].sort();
 assert.deepEqual(restored.files.map(f=>f.path).sort(),expected);
 assert.equal(command('restored-verify','verify-project').exitCode,0);assert.equal(sdk.readLock(project).manifest.version,'0.3.0-preview.10');
 json('restored-project.json',restored);
 assert.equal(command('upgrade-preview13','install',candidate).exitCode,0);
 assert.equal(command('verify-preview13','verify-project').exitCode,0);businessCheck(project);
 assert.equal(sdk.readLock(project).manifest.version,'0.3.0-preview.13');save('upgraded-lock.json',fs.readFileSync(path.join(project,'sdk-lock.json')));
 const after=snapshot(original);json('original-after.json',after);assert.equal(after.sha256,before.sha256);assert.equal(id.source(repo).sha256,sdkSource.sha256);
 json('result.json',{schema:'soui.known-legacy-recovery/v1',finishedAt:new Date().toISOString(),manualRecoveryVerified:true,automaticLegacyRecoveryVerified:false,
  originalUnchanged:true,originalSha256:before.sha256,originalFiles:before.files.length,restoredOldFiles:old.files.length,replacedFiles:plan.filter(f=>f.replace).length,
  quarantinedNewOnlyFiles:newOnly.length,businessFiles:business.files.length,newSdkFiles:next.files.length,sourceSha256:sdkSource.sha256,guiExecuted:false,releaseReady:false});
 console.log(id.encoded({status:'known-incident-copy-restored-and-upgraded',manualRecoveryVerified:true,automaticLegacyRecoveryVerified:false,originalUnchanged:true,releaseReady:false}));
}
try{main();}catch(error){console.error(error.stack);process.exitCode=1;}
