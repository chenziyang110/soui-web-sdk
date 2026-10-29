'use strict';
// Task-VM only. One architecture per explicit command; no Shell restart or input injection.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {TextDecoder}=require('node:util');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};}
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);}
function json(f){return JSON.parse(fs.readFileSync(f,'utf8'));}
function write(f,o){fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});}
function names(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?names(d,n):(assert(e.isFile()),[n]);}).sort();}
function relative(p){assert(typeof p==='string'&&!path.isAbsolute(p)&&!p.includes('\\')&&p.split('/').every(s=>s&&s!=='.'&&s!=='..'&&!s.includes(':')));return p;}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
function verifyRuntime(arm){
  assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);
  const d=path.join(root,arm.runtimeDirectory);assert.deepEqual(names(d),arm.files.map(p=>relative(p.relative)).sort());
  for(const p of arm.files)check({...p,path:path.join(d,relative(p.relative))});
}
function validateNative(file,architecture,ready){
  const raw=fs.readFileSync(file);assert(raw.length>0&&raw.length<=8388608&&raw.at(-1)===10,'Complete bounded native NDJSON required');
  const text=new TextDecoder('utf-8',{fatal:true}).decode(raw);assert(Buffer.from(text,'utf8').equals(raw));
  const rows=text.trimEnd().split('\n').map(JSON.parse);assert(rows.length>0&&rows.length<=128);
  const first=rows[0],last=rows.at(-1);assert.equal(first.event,'run.started');assert.equal(last.event,'run.finished');
  let elapsed=-1;
  for(const [i,r]of rows.entries()){
    assert.equal(r.sequence,i+1);assert(Number.isInteger(r.elapsedMs)&&r.elapsedMs>=elapsed&&r.elapsedMs<90000);elapsed=r.elapsedMs;
    assert.equal(r.sdkVersion,'0.6.0');assert.equal(r.architecture,architecture);assert.equal(r.pid,ready.pid);
    assert.equal(r.birthFileTime,ready.creationFileTime);assert.equal(r.runId,`${ready.pid}-${ready.creationFileTime}`);
    assert(/^[0-9]{10,20}$/.test(r.utcFileTime));
    assert(!['run.error','stage.error','cleanup.error','log.limit-reached','owner.userdata-failed'].includes(r.event));
  }
  assert.equal(first.wholeDeadlineMs,90000);assert.equal(first.timerMs,50);assert.equal(first.iconId,73);assert.equal(first.messageSourceVerified,false);
  const one=event=>{const a=rows.filter(r=>r.event===event);assert.equal(a.length,1,event);return a[0];};
  const all=event=>rows.filter(r=>r.event===event);
  const resident=r=>assert(r.installed===true&&r.desired===true&&r.cleanupPending===false);
  const clean=r=>assert(r.installed===false&&r.desired===false&&r.cleanupPending===false);
  const before=(a,b)=>assert(rows.indexOf(a)<rows.indexOf(b),`${a.event} must precede ${b.event}`);
  assert.equal(one('owner.created').visible,true);const installed=one('tray.install');assert(installed.ok);resident(installed);
  const notifications=all('taskbar.received');assert.equal(notifications.length,2);assert.deepEqual(notifications.map(r=>r.count),[1,2]);assert(notifications.every(r=>r.sourceVerified===false));
  const dispatched=all('tray.taskbar-dispatched');assert.equal(dispatched.length,2);assert.deepEqual(dispatched.map(r=>r.stage),[1,2]);assert(dispatched.every(r=>r.handled&&r.ok&&r.sourceVerified===false));resident(dispatched[0]);clean(dispatched[1]);
  const recovered=one('stage.recovery-confirmed');resident(recovered);assert.equal(recovered.removeEnabled,true);
  const removed=one('tray.remove');assert(removed.ok&&removed.call===1);clean(removed);assert.equal(all('tray.cleanup-remove').length,0);
  const noResurrection=one('stage.no-resurrection');clean(noResurrection);assert.equal(noResurrection.settleMs,500);assert.equal(noResurrection.finishEnabled,false);
  const settled=one('stage.settled');clean(settled);assert.equal(settled.finishEnabled,true);assert(settled.elapsedMs-noResurrection.elapsedMs>=500);
  const snapshots=all('explorer.snapshot');assert.equal(snapshots.length,6);
  assert.deepEqual(snapshots.map(r=>r.step),['initial.before-install','first-restart.before-dispatch','remove.before-call','second-restart.before-dispatch','settle.before-finish-enable','finish.before-close']);
  for(const r of snapshots)assert(r.explorerPid>0&&/^[0-9]{10,20}$/.test(r.explorerBirthFileTime)&&/\\explorer\.exe$/i.test(r.imagePath)&&r.waitResult===258&&r.waitError===0&&r.sourceVerified===false);
  const same=(a,b)=>a.explorerPid===b.explorerPid&&a.explorerBirthFileTime===b.explorerBirthFileTime&&a.imagePath.toLowerCase()===b.imagePath.toLowerCase();
  assert(!same(snapshots[0],snapshots[1]));assert(same(snapshots[1],snapshots[2]));assert(!same(snapshots[1],snapshots[3]));assert(same(snapshots[3],snapshots[4])&&same(snapshots[3],snapshots[5]));
  const restarts=all('explorer.restart-observed');assert.equal(restarts.length,2);
  for(const [i,r]of restarts.entries()){const old=snapshots[i===0?0:1];assert(r.oldPid===old.explorerPid&&r.oldBirthFileTime===old.explorerBirthFileTime&&r.oldWaitResult===0&&r.oldWaitError===0&&r.oldExited&&r.differentIdentity&&r.sameImagePath&&r.sourceVerified===false&&r.externalRestartProofRequired);}
  assert.equal(all('explorer.continuity').length,3);assert(all('explorer.continuity').every(r=>r.sameIdentity));
  const handles=all('explorer.handle-closed');assert.equal(handles.length,6);assert(handles.every(r=>r.ok&&r.win32Error===0));assert.equal(handles.filter(r=>r.step==='final-cleanup').length,3);
  const exit=one('exit.requested');assert(exit.successRequested&&!exit.failureLatched&&exit.requestedQuitCode===0&&exit.finalExitPendingCleanup);
  const cleanup=one('tray.cleanup-state');clean(cleanup);assert(cleanup.confirmed);
  const service=one('service.destroyed'),owner=one('owner.destroyed');assert(service.beforeOwnerDestruction&&owner.ok&&one('timer.stopped').ok&&one('owner.class-unregistered').ok);
  const stages=[installed,one('stage.await-first-restart'),notifications[0],restarts[0],dispatched[0],recovered,removed,one('stage.await-second-restart'),notifications[1],restarts[1],dispatched[1],noResurrection,settled,exit,cleanup,service,owner,last];
  for(let i=1;i<stages.length;i++)before(stages[i-1],stages[i]);
  assert(last.exitCode===0&&last.allStagesComplete&&last.cleanupConfirmed&&last.taskbarCreatedCount===2&&last.removeCalls===1&&last.messageSourceVerified===false&&last.externalRestartProofRequired&&last.focusVerified===false);
  return {records:rows.length,runId:first.runId,pid:first.pid,birthFileTime:first.birthFileTime,elapsedMs:last.elapsedMs,explorerSnapshots:snapshots,observedRestarts:restarts,removeCalls:1,taskbarCreatedCount:2,nativeProtocolPassed:true,externalRestartProofRequired:true,explorerRestartIndependentlyQualified:false,focusQualified:false,win7Qualified:false};
}
async function main(){
  const [mode,architecture,manifestFile,manifestSha,machine,output,gate,stageFile,stageSha]=process.argv.slice(2);
  assert.equal(process.argv.length,mode==='stage'?8:11);assert(['stage','run'].includes(mode));assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');assert.equal(root.toLowerCase(),'c:\\trayrc-a1');
  assert(/^[a-f0-9]{64}$/.test(manifestSha));assert.equal(pin(manifestFile).sha256,manifestSha);const manifest=json(manifestFile);
  assert.equal(manifest.schema,'soui.current-tray-recovery-light-payload/v1');assert.equal(manifest.version,'0.6.0');assert.deepEqual(manifest.arms.map(a=>a.architecture),['x64','x86']);
  assert.equal(machine,'SOUI-7AEMMK5Q0T');assert.equal(os.hostname().toLowerCase(),machine.toLowerCase());assert.notEqual(os.hostname().toLowerCase(),manifest.preparationHost.toLowerCase());
  assert.equal(path.resolve(output).toLowerCase(),`c:\\issue-closeout-evidence\\trayrc-a1-${mode==='stage'?'stage':architecture}`);assert(fs.statSync(output).isDirectory());
  assert(mode==='stage'?architecture==='stage':['x64','x86'].includes(architecture));
  for(const name of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'])assert(!process.env[name]);
  const verifyInputs=()=>{assert.equal(pin(manifestFile).sha256,manifestSha);assert.equal(manifest.payloadFiles.length,8);for(const p of manifest.payloadFiles)check({...p,path:path.join(root,relative(p.relative))});for(const p of Object.values(manifest.tools))check(p);};
  assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(manifest.tools.node.path).toLowerCase());verifyInputs();
  const lease=json(output+'/Lease.actual.json');assert(lease.acquired&&lease.ownerProcessId===process.ppid&&lease.name==='Local\\SouiDevKit.DesktopValidation');
  write(output+'/Inputs.actual.json',{schema:'soui.current-tray-recovery-inputs/v1',controllerPid:process.pid,controllerParentPid:process.ppid,controllerBirthNotCollected:true,manifest:pin(manifestFile),controller:pin(__filename),tools:manifest.tools,architecture,mode,inputInjection:false,externalRestartProofRequired:true,releaseReady:false});
  if(mode==='stage'){
    const copies=[];let failure=null;
    try{for(const arm of manifest.arms){
      assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);const d=path.join(root,arm.runtimeDirectory);assert(!fs.existsSync(d),'Preserve prior runtime');fs.mkdirSync(d);
      for(const f of arm.files){const source=f.fromPayload?path.join(root,relative(f.source)):f.source;assert(path.isAbsolute(source));check({...f,path:source});const target=path.join(d,relative(f.relative));fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);check({...f,path:target});check({...f,path:source});copies.push({source:pin(source),copy:pin(target)});}
      verifyRuntime(arm);
    }verifyInputs();}catch(error){failure={message:error.message,stack:error.stack};}
    write(output+'/Stage.actual.json',{schema:'soui.current-tray-recovery-stage/v1',passed:!failure,machine:os.hostname(),controllerPid:process.pid,manifest:pin(manifestFile),copies,failure,version:'0.6.0',productExecuted:false,oldRuntimeChanged:false,releaseReady:false});
    if(failure)throw Error(failure.message);console.log(JSON.stringify({stage:pin(output+'/Stage.actual.json'),productExecuted:false}));return;
  }
  assert.equal(gate,'ROOT_REVIEWED_TRAY_RECOVERY_STAGE_V1');assert(/^[a-f0-9]{64}$/.test(stageSha));assert.equal(pin(stageFile).sha256,stageSha);const staged=json(stageFile);assert(staged.passed&&!staged.productExecuted&&staged.manifest.sha256===manifestSha&&staged.machine===os.hostname());
  const arm=manifest.arms.find(a=>a.architecture===architecture);assert(arm);const {launchOwned}=require(manifest.tools.ownedModule.path);
  let child=null,result=null,ready=null,protocol=null,failure=null;
  const runtime=path.join(root,arm.runtimeDirectory),executable=path.join(runtime,'SouiTrayRecoveryConsumer.exe'),ownedLog=path.join(output,'owned.ndjson');
  const stdout=fs.openSync(path.join(output,'stdout.bin'),'wx'),stderr=fs.openSync(path.join(output,'stderr.bin'),'wx');
  try{
    verifyRuntime(arm);verifyInputs();
    write(output+'/Native.Invocation.actual.json',{version:'0.6.0',architecture,executable:pin(executable),args:[],cwd:runtime,timeoutMs:100000,drainMs:2000,innerDeadlineMs:90000,stage:pin(stageFile),sourcePins:manifest.sourcePins,inputInjection:false,explorerRestartPerformedByController:false,externalRestartProofRequired:true});
    child=launchOwned(executable,[],{runner:manifest.tools.runner.path,cwd:runtime,report:ownedLog,timeoutMs:100000,drainMs:2000,stdout,stderr});
    try{ready=await child.ready;write(output+'/ready.json',ready);console.log(JSON.stringify({architecture,event:'native-subject-ready',...ready,applicationReadyEstablished:false}));}catch(error){write(output+'/ready-error.json',{message:error.message});}
    result=await child.finished;write(output+'/finished.json',result);natural(result);
    const context=json(ownedLog+'.context.json');assert.deepEqual(context.args,[]);assert.equal(context.timeoutMs,100000);assert.equal(context.drainMs,2000);assert.equal(context.runnerSha256,manifest.tools.runner.sha256);assert.equal(context.executableSha256,pin(executable).sha256);assert(ready&&ready.pid===result.pid&&ready.creationFileTime===result.creationFileTime);
    protocol=validateNative(path.join(output,'stdout.bin'),architecture,ready);assert.equal(fs.statSync(path.join(output,'stderr.bin')).size,0);
  }catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    if(child&&!result){child.stop();result=await child.finished;write(output+'/unfinished-cleanup.json',result);failure||={message:'Owned cleanup required after incomplete result; never a pass'};}
    fs.closeSync(stdout);fs.closeSync(stderr);
    try{verifyInputs();for(const a of manifest.arms)verifyRuntime(a);}catch(error){failure||={message:error.message};}
    write(output+'/Summary.actual.json',{schema:'soui.current-tray-recovery-finite-result/v1',version:'0.6.0',machine:os.hostname(),controllerPid:process.pid,architecture,manifest:pin(manifestFile),stage:pin(stageFile),ready,result,protocol,failure,nativeRecoveryStateMachinePassed:!failure&&!!protocol,wholeOwnedJobNatural:!failure&&!!protocol,oneArchitectureOnly:true,automaticOtherArchitecture:false,externalRestartProofRequired:true,explorerRestartIndependentlyQualified:false,focusQualified:false,win7Qualified:false,productionQualificationComplete:false,releaseReady:false});
  }
  if(failure)throw Error(failure.message);console.log(JSON.stringify({summary:pin(output+'/Summary.actual.json'),architecture,nativeRecoveryStateMachinePassed:true,explorerRestartIndependentlyQualified:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
