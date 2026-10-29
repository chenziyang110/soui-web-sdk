'use strict';
// Preparation only. Root must review this exact file, plan and PS root before the explicit invocation.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const BASE='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/win7-conversion-current/r2';
const PLAN_SHA='de0b8c132efa2d691579d7da43bdc5d07beb35fd030ab47fe142160d0180ca97';
const GATE='ROOT_REVIEWED_ONE_WIN7_BASE_CONVERSION_R2_V1';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const samePath=(a,b)=>path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();
const identity=s=>({dev:String(s.dev),fileId:String(s.ino),bytes:Number(s.size),mtimeNs:String(s.mtimeNs),ctimeNs:String(s.ctimeNs),nlink:String(s.nlink)});
function plain(file){let p=path.resolve(file);for(;;){assert(!fs.lstatSync(p).isSymbolicLink(),'Linked conversion input');const next=path.dirname(p);if(next===p)break;p=next;}assert(samePath(fs.realpathSync(file),file),'Redirected conversion input');}
function pin(file){plain(file);const before=fs.lstatSync(file,{bigint:true});assert(before.isFile()&&before.size<8388608n);const b=fs.readFileSync(file);assert.deepEqual(identity(fs.lstatSync(file,{bigint:true})),identity(before));return {path:file,bytes:b.length,sha256:sha(b)};}
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);}
function write(dir,name,value){fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
function sourceNow(held){
  return held.map(x=>{const actual=identity(fs.fstatSync(x.fd,{bigint:true}));assert.deepEqual(actual,x.identity,'Held source identity changed');assert.deepEqual(identity(fs.lstatSync(x.row.path,{bigint:true})),actual,'Source path identity changed');if(x.row.kind==='descriptor'){const b=Buffer.alloc(x.row.bytes);assert.equal(fs.readSync(x.fd,b,0,b.length,0),b.length);assert.equal(sha(b),x.row.sha256);}return {path:x.row.path,kind:x.row.kind,identity:actual};});
}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
async function main(){
  assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');assert(samePath(__dirname,BASE));
  assert.deepEqual(process.argv.slice(2),[GATE,'--plan-sha256',PLAN_SHA]);
  const planFile=path.join(BASE,'Conversion.plan.prepared.json');assert.equal(pin(planFile).sha256,PLAN_SHA);
  const plan=JSON.parse(fs.readFileSync(planFile,'utf8'));assert.equal(plan.schema,'soui.one-win7-immutable-base-conversion-plan/v2');assert.equal(plan.executionApproved,false);assert.equal(os.hostname().toLowerCase(),plan.hostName.toLowerCase());
  assert.equal(plan.timeoutMs,600000);assert.equal(plan.drainMs,2000);assert.equal(plan.requiredFreeBytes,90194313216);
  assert.equal(plan.sourceLeaf,'F:/vm/Windows 7 x64 Sp1-000008.vmdk');assert.equal(plan.destinationRoot,'C:/SOUI-SDK-QEMU-Win7');assert.equal(plan.destination,plan.destinationRoot+'/base-win7-sp1-x64.qcow2');
  assert.deepEqual(plan.qemuArguments,['convert','-f','vmdk','-O','qcow2',plan.sourceLeaf,plan.destination]);
  assert.equal(plan.sources.length,102);assert.equal(plan.sources.filter(x=>x.kind==='descriptor').length,6);assert.equal(plan.sources.filter(x=>x.kind==='extent').length,96);assert.equal(new Set(plan.sources.map(x=>x.path.toLowerCase())).size,102);
  assert.equal(plan.tools.ownedModule.sha256,'8ccec5802c793b45990d2d7585f2c29fb618a7b170adebd5d270a7c291757ce7');assert.equal(plan.tools.ownedRunner.sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
  const controlPins=[pin(__filename),pin(planFile),plan.sourceReport,...Object.values(plan.tools),...plan.occupancyDependencies];for(const p of controlPins)check(p);
  assert(!fs.existsSync(plan.destinationRoot),'Fresh C: destination only; preserve every existing/partial output');
  const free=fs.statfsSync('C:/',{bigint:true});const freeBytes=free.bavail*free.bsize;assert(freeBytes>=BigInt(plan.requiredFreeBytes),'C: conservative budget unavailable');
  const dir=path.join(BASE,'attempt-'+crypto.randomUUID());fs.mkdirSync(dir);const held=[];
  let run=null,ready=null,readyError=null,result=null,failure=null,root=null,sourceAfter=null,outputMetadata=null,out=null,err=null;
  try{
    const env={...process.env};for(const n of Object.keys(env))if(n.toLowerCase()==='psmodulepath')delete env[n];
    const observed=spawnSync(plan.tools.powershell.path,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',plan.tools.occupancyPs.path,'-PlanFile',planFile,'-PlanSha256',PLAN_SHA],{encoding:'utf8',windowsHide:true,env,timeout:15000,maxBuffer:1048576});
    write(dir,'Occupancy.controller.actual.json',{exitCode:observed.status,signal:observed.signal,error:observed.error?.message||null,stdout:observed.stdout,stderr:observed.stderr,readOnly:true});
    assert.equal(observed.status,0);const occupancy=JSON.parse(observed.stdout);assert.equal(occupancy.schema,'soui.native-ps-targeted-psutil-vm-occupancy/v1');assert.equal(occupancy.WmiOrCimUsed,false);assert.equal(occupancy.unknownNamesAdmitted,false);assert.deepEqual(occupancy.sourceLocks,[]);assert(occupancy.vmProcesses.every(p=>p.commandLineAvailable&&!p.referencesExactFvmRoot),'Source occupancy unknown/active');
    for(const row of plan.sources){
      assert(samePath(path.win32.dirname(row.path),'F:/vm'));plain(row.path);const fd=fs.openSync(row.path,'r');const actual=identity(fs.fstatSync(fd,{bigint:true}));held.push({fd,row,identity:actual});
      assert.equal(actual.fileId,row.fileId);assert.equal(actual.bytes,row.bytes);assert.equal(actual.mtimeNs,row.mtimeNs);assert.equal(actual.nlink,'1');
    }
    write(dir,'Source.node-held.before.actual.json',{heldSources:held.length,nodeReadHandlesDoNotClaimWriteExclusion:true,powerShellWillEnforceFileShareRead:true,sources:sourceNow(held)});
    const args=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',plan.tools.powershellRoot.path,'-PlanFile',planFile,'-PlanSha256',PLAN_SHA,'-EvidenceDirectory',dir,'-RootGate',GATE];
    write(dir,'Invocation.actual.json',{schema:'soui.one-win7-conversion-owned-invocation/v1',executable:plan.tools.powershell,args,cwd:BASE,timeoutMs:600000,drainMs:2000,actualCFreeBytes:String(freeBytes),controlPins,nodeHeldSourceCount:held.length,outputPath:plan.destination,sourceFullImageHashPerformed:false,vmStarted:false});
    out=fs.openSync(path.join(dir,'stdout.raw'),'wx');err=fs.openSync(path.join(dir,'stderr.raw'),'wx');
    const {launchOwned}=require(plan.tools.ownedModule.path);
    run=launchOwned(plan.tools.powershell.path,args,{runner:plan.tools.ownedRunner.path,cwd:BASE,report:path.join(dir,'owned.ndjson'),timeoutMs:600000,drainMs:2000,stdout:out,stderr:err,env});
    try{ready=await run.ready;write(dir,'ready.json',ready);}catch(error){readyError=error.message;write(dir,'ready-error.json',{message:error.message});}
    result=await run.finished;write(dir,'finished.json',result);natural(result);assert.equal(readyError,null);
    const raw=fs.readFileSync(path.join(dir,'owned.ndjson'));assert.equal(raw.at(-1),10);assert.equal(sha(raw),result.reportSha256);const rows=raw.toString('utf8').trimEnd().split('\n').map(JSON.parse);
    assert.equal(rows[0].pid,ready.pid);assert.equal(rows[0].creationFileTime,ready.creationFileTime);assert.deepEqual(rows.at(-1),result.terminal);
    const context=JSON.parse(fs.readFileSync(path.join(dir,'owned.ndjson.context.json'),'utf8'));assert(samePath(context.executable,plan.tools.powershell.path));assert.deepEqual(context.args,args);assert.equal(context.timeoutMs,600000);assert.equal(context.drainMs,2000);assert(!context.tailImagePin);
    assert.equal(context.runnerSha256,plan.tools.ownedRunner.sha256);assert.equal(context.executableSha256,plan.tools.powershell.sha256);
    const converters=rows.filter(x=>x.event==='owned-member'&&typeof x.image==='string'&&samePath(x.image,plan.tools.qemuImg.path));
    assert(converters.length>0&&converters.every(x=>Number.isSafeInteger(x.pid)&&x.pid>0&&/^[1-9]\d*$/.test(x.creationFileTime)),'Actual qemu-img member identity required');
    assert.equal(new Set(converters.map(x=>x.pid+':'+x.creationFileTime)).size,1,'One actual qemu-img PID/birth required; repeated observations are preserved');
    root=JSON.parse(fs.readFileSync(path.join(dir,'Root.finished.actual.json'),'utf8').replace(/^\uFEFF/,''));
    assert.equal(root.rootPid,ready.pid);assert.equal(root.actualNativeExitCode,0);assert.equal(root.rootExitCode,0);assert.equal(root.heldSourceCount,102);assert.equal(root.releasedSourceCount,102);assert.deepEqual(root.releaseFailures,[]);assert.equal(root.failure,null);assert.equal(root.sourceMetadataAndDescriptorPinsUnchanged,true);assert.equal(root.sourceWriteDeleteDeniedWhileHeld,true);
    assert(root.qcow2&&root.qcow2.backingFileAbsent&&root.qcow2.readOnlyBase);assert.equal(root.qcow2.virtualBytes,plan.sourceVirtualBytes);
    assert.deepEqual(fs.readdirSync(plan.destinationRoot).sort(),['base-win7-sp1-x64.qcow2','ownership.json']);
    outputMetadata={path:plan.destination,...identity(fs.statSync(plan.destination,{bigint:true})),standaloneQcow2HeaderProof:root.qcow2,fullImageHashPerformed:false};
  }catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    if(run&&!result){result=await run.finished;write(dir,'unfinished-result.json',result);}
    if(out!==null)fs.closeSync(out);if(err!==null)fs.closeSync(err);
    try{sourceAfter=sourceNow(held);for(const p of controlPins)check(p);}catch(error){failure??={message:error.message,stack:error.stack};}
    write(dir,'Source.node-held.after.actual.json',{heldSources:held.length,sources:sourceAfter,unchanged:sourceAfter!==null&&held.length===102});
    let released=0;for(const x of held){try{fs.closeSync(x.fd);released++;}catch(error){failure??={message:error.message};}}
    const passed=failure===null&&readyError===null&&root!==null&&result?.passed===true&&sourceAfter!==null&&held.length===102&&released===102;
    const summary={schema:'soui.one-win7-immutable-base-conversion-result/v1',passed,planSha256:PLAN_SHA,ready,readyError,result,rootResult:root,failure,actualNodeHeldSourceCount:held.length,nodeReleasedSourceCount:released,sourceIdentityAfterVerified:sourceAfter!==null&&held.length===102,outputMetadata,partialOutputExists:fs.existsSync(plan.destination),preserveEveryPartialOutput:true,noRetryOverwriteDeleteRename:true,fullImageHashPerformed:false,vmStarted:false,poolChanged:false,productOrCompilerExecuted:false,releaseReady:false,productionQualificationComplete:false};
    write(dir,'Summary.actual.json',summary);console.log(JSON.stringify({directory:dir,passed,failure:failure?.message||null,output:outputMetadata?.path||null,partialOutputPreserved:fs.existsSync(plan.destination),vmStarted:false}));
    if(!passed)process.exitCode=1;
  }
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
