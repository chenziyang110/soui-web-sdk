'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname,read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const check=(ok,msg)=>{if(!ok)throw Error(msg);};
const walk=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>{check(!e.isSymbolicLink(),'Unexpected link');return e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)];});
const matches=(p,f)=>{const b=fs.readFileSync(p);check(b.length===f.bytes&&hash(b)===f.sha256,'Mismatch '+p);return b;};
const inputs=read(path.join(root,'inputs.json')),business=read(path.join(root,'business-input.json'));
for(const item of Object.values(inputs.packages)){
 check(JSON.stringify(walk(item.copy).map(p=>path.relative(item.copy,p).replaceAll('\\','/')).sort())===JSON.stringify(item.files.map(f=>f.path).sort()),'Package file set');
 for(const f of item.files)matches(path.join(item.copy,f.path),f);
 check(hash(fs.readFileSync(path.join(item.copy,'sdk-manifest.json')))===item.manifestSha256,'Package manifest');
}
check(hash(fs.readFileSync(path.join(root,'business-input.json')))===inputs.businessInputSha256,'Business manifest');
for(const f of business.files)matches(path.join(root,'project',f.path),f);
const stages=[],ids=new Set();let payloadCount=0;
for(const [name,key] of [['old-initial','old'],['new-upgraded','current'],['old-rolled-back','old'],['new-restored','current']]){
 const dir=path.join(root,'stages',name),result=read(path.join(dir,'result.json')),lock=read(path.join(dir,'sdk-lock.json'));
 check(lock.manifest.version===inputs.packages[key].version&&hash(Buffer.from(JSON.stringify(lock.manifest,null,2)+'\n'))===inputs.packages[key].manifestSha256,'Wrong lock');
 check(result.status==='builds-complete-desktop-pending'&&result.guiExecuted===false&&result.releaseReady===false,'Invalid stage status');
 for(const arch of ['x86','x64']){
  const commandDir=path.join(dir,'commands','build-'+arch),command=read(path.join(commandDir,'result.json'));
  check(command.exitCode===0&&!command.timedOut&&!command.error,'Raw build failed');
  for(const s of ['stdout','stderr'])matches(path.join(commandDir,s+'.log'),command[s]);
  const ad=path.join(dir,arch),record=read(path.join(ad,'build-Release.json')),reported=result.architectures.find(a=>a.architecture===arch);
  check(record.version===lock.manifest.version&&record.architecture===arch&&record.configuration==='Release'&&record.nativeBuild==='passed','Build identity');
  check(record.buildId===reported.buildId&&!ids.has(record.buildId),'Build ID reused');ids.add(record.buildId);
  check(hash(fs.readFileSync(path.join(ad,'build-Release.json')))===reported.buildRecordSha256,'Build record differs');
  check(record.source.sha256===reported.sourceSha256&&hash(Buffer.from(JSON.stringify(record.source.files,null,2)+'\n'))===record.source.sha256,'Source digest');
  const lockSource=record.source.files.find(s=>s.path==='sdk-lock.json');
  const lockBytes=Buffer.from(fs.readFileSync(path.join(dir,'sdk-lock.json'),'utf8').replace(/\r\n/g,'\n'));
  check(lockSource&&lockSource.bytes===lockBytes.length&&lockSource.sha256===hash(lockBytes),'SDK lock source binding');
  for(const f of lock.manifest.files){
   const observed=record.source.files.find(s=>s.path===f.path);
   // The existing source collector excludes dependency manifest metadata. Its
   // exact file hashes remain bound through sdk-lock.json; do not claim those
   // files were individually enumerated as native compiler inputs.
   if(!observed&&f.path.startsWith('third_party/manifests/'))continue;
   check(observed&&observed.bytes===f.bytes&&observed.sha256===f.sha256,'SDK source binding '+f.path);
  }
  const payload=record.payloads.UpgradeLedger;
  check(payload.sha256===reported.payloadSha256,'Payload identity');
  const runtime=path.join(ad,'runtime');check(JSON.stringify(walk(runtime).map(p=>path.relative(runtime,p).replaceAll('\\','/')).sort())===JSON.stringify(payload.files.map(f=>f.path).sort()),'Runtime file set');
  for(const f of payload.files){const b=matches(path.join(runtime,f.path),f);if(/\.(exe|dll)$/i.test(f.path)){check(b.toString('ascii',0,2)==='MZ','MZ');const pe=b.readUInt32LE(0x3c);check(b.readUInt32LE(pe)===0x4550&&b.readUInt16LE(pe+4)===(arch==='x86'?0x14c:0x8664),'PE architecture');}++payloadCount;}
 }
 check(result.business.length===5&&result.business.every(f=>f.preserved&&f.businessFiles===business.files.length),'Business checkpoints');stages.push(result);
}
const interrupted=read(path.join(root,'interruption-attempt1/result.json'));
check(interrupted.child.signal==='SIGTERM'&&interrupted.child.terminatedByProbe,'Not actual termination');
check(interrupted.verifyExitCode===1&&interrupted.retryInstallExitCode===1&&!interrupted.automaticRecoveryVerified,'Unexpected interruption result');
check(interrupted.oldLockUnchanged&&interrupted.businessFilesPreserved&&interrupted.oldInputsRecoverableFromLiveAndBackup,'Lost data');
const killedProject=path.join(root,'interruption-attempt1/project'),oldLock=read(path.join(root,'interruption-attempt1/lock-before.json'));
for(const f of business.files)matches(path.join(killedProject,f.path),f);
check(fs.readFileSync(path.join(root,'interruption-attempt1/lock-before.json')).equals(fs.readFileSync(path.join(killedProject,'sdk-lock.json'))),'Old lock bytes');
const recovered=read(path.join(root,'interruption-attempt1/old-input-recovery-inventory.json'));
for(const f of oldLock.manifest.files){const location=recovered.find(r=>r.path===f.path);check(location?.oldBytesRecoverable,'Not recoverable');matches(path.join(killedProject,location.location),f);}
const summary={schema:'soui.v06-upgrade-evidence-review/v1',at:new Date().toISOString(),normalStageCount:stages.length,builds:ids.size,payloadFilesVerified:payloadCount,businessFiles:business.files.length,normalBuildsVerified:true,realProcessInterruptionVerified:true,interruptedAutomaticRecovery:false,interruptedDataPreserved:true,guiExecuted:false,sourceDeliveryVerified:false,releaseReady:false};
fs.writeFileSync(path.join(root,'verification.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(summary));
