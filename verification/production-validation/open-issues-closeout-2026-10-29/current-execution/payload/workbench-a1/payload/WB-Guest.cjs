'use strict';
// Task-VM-only current Workbench consumers. Original release trees are sources,
// never destinations. No input injection, foreground change or Shell restart.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};}
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);}
const json=f=>JSON.parse(fs.readFileSync(f,'utf8'));
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
function names(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?names(d,n):(assert(e.isFile()),[n]);}).sort();}
function relative(p){assert(typeof p==='string'&&!path.isAbsolute(p)&&!p.includes('\\')&&p.split('/').every(s=>s&&s!=='.'&&s!=='..'&&!s.includes(':')));return p;}
function logs(d){return names(d).filter(n=>/\.log$/i.test(n)).map(relative=>{const f=path.join(d,relative),s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink()&&s.size<=16777216);return {relative,...pin(f)};});}
function runtimeCheck(arm){assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);const d=path.join(root,arm.runtimeDirectory);assert.deepEqual(names(d).filter(n=>n!=='webview-smoke.log'),arm.files.map(f=>relative(f.relative)).sort());for(const f of arm.files)check({...f,path:path.join(d,relative(f.relative))});}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
function records(f){const b=fs.readFileSync(f);assert(b.length>0&&b.length<=16777216&&b.at(-1)===10);return b.toString('utf8').trimEnd().split('\n').map(JSON.parse);}
async function main(){
  const [mode,manifestFile,manifestSha,machine,output,gate,stageFile,stageSha]=process.argv.slice(2);
  assert(['stage','run'].includes(mode));assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');assert(/^C:\\issue31c[a-z0-9-]*$/i.test(root));
  assert(/^[a-f0-9]{64}$/.test(manifestSha));assert.equal(pin(manifestFile).sha256,manifestSha);const manifest=json(manifestFile);
  assert.equal(manifest.schema,'soui.current-workbench-light-payload/v1');assert.equal(manifest.version,'0.6.0');assert.deepEqual(manifest.arms.map(a=>a.architecture),['x64','x86']);assert.equal(os.hostname().toLowerCase(),machine.toLowerCase());assert.notEqual(os.hostname().toLowerCase(),manifest.preparationHost.toLowerCase());
  assert(/^C:\\issue-closeout-evidence\\wb31c-[a-z0-9-]+$/i.test(output));assert(fs.statSync(output).isDirectory());
  for(const n of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'])assert(!process.env[n]);
  const verify=()=>{assert.equal(pin(manifestFile).sha256,manifestSha);for(const p of manifest.payloadFiles)check({...p,path:path.join(root,relative(p.relative))});for(const p of manifest.readerPins)check(p);for(const p of Object.values(manifest.tools))check(p);};
  assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(manifest.tools.node.path).toLowerCase());verify();
  const lease=json(output+'/Lease.actual.json');assert(lease.acquired&&lease.ownerProcessId===process.ppid&&lease.name==='Local\\SouiDevKit.DesktopValidation');
  if(mode==='stage'){
    const copies=[],sourceLogs=[];
    for(const arm of manifest.arms){
      assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);assert.equal(arm.files.length,671);
      assert.deepEqual(names(arm.originalRuntime).filter(n=>n!=='webview-smoke.log'),arm.originalFiles.map(f=>relative(f.path)).sort());sourceLogs.push({architecture:arm.architecture,before:logs(arm.originalRuntime)});
      for(const f of arm.originalFiles)check({...f,path:path.join(arm.originalRuntime,relative(f.path))});
      const d=path.join(root,arm.runtimeDirectory);assert(!fs.existsSync(d),'Preserve prior new runtime');fs.mkdirSync(d);
      for(const f of arm.files){const source=f.fromPayload?path.join(root,relative(f.source)):f.source;assert(path.isAbsolute(source));check({...f,path:source});const target=path.join(d,relative(f.relative));fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);check({...f,path:target});copies.push({source:pin(source),copy:pin(target)});}
      runtimeCheck(arm);for(const f of arm.originalFiles)check({...f,path:path.join(arm.originalRuntime,relative(f.path))});
      assert.deepEqual(names(arm.originalRuntime).filter(n=>n!=='webview-smoke.log'),arm.originalFiles.map(f=>relative(f.path)).sort());sourceLogs.at(-1).after=logs(arm.originalRuntime);assert.deepEqual(sourceLogs.at(-1).after,sourceLogs.at(-1).before,'Original runtime diagnostic changed during source-only staging');
    }
    verify();write(output+'/Stage.actual.json',{schema:'soui.current-workbench-stage/v1',passed:true,machine:os.hostname(),manifest:pin(manifestFile),copies,sourceLogs,originalRuntimeChanged:false,productExecuted:false,releaseReady:false});console.log(JSON.stringify({stage:pin(output+'/Stage.actual.json'),productExecuted:false}));return;
  }
  assert.equal(gate,'ROOT_REVIEWED_CURRENT_WORKBENCH_STAGE_V1');assert(/^[a-f0-9]{64}$/.test(stageSha));assert.equal(pin(stageFile).sha256,stageSha);const staged=json(stageFile);assert(staged.passed&&!staged.productExecuted&&staged.manifest.sha256===manifestSha&&staged.machine===os.hostname());
  const {launchOwned}=require(manifest.tools.ownedModule.path),reader=require(manifest.standardReader),results=[],attempted=[];let failure=null;
  try{for(const arm of manifest.arms){
    verify();runtimeCheck(arm);attempted.push(arm.architecture);const folder=path.join(output,arm.architecture);fs.mkdirSync(folder);
    const runtime=path.join(root,arm.runtimeDirectory),executable=path.join(runtime,'SouiWorkbench.exe'),report=path.join(folder,'owned.ndjson'),log=path.join(folder,'resident.ndjson');
    const args=['--resident','--resident-smoke-test','--react','--page=data','--resident-log='+log];
    const out=fs.openSync(path.join(folder,'stdout.raw'),'wx'),err=fs.openSync(path.join(folder,'stderr.raw'),'wx');let run=null,result=null,ready=null,caseError=null;
    write(folder+'/Runtime.before.json',{runtime,mutableRuntimeLogs:['webview-smoke.log'],observedLogs:logs(runtime)});
    try{
      write(folder+'/Invocation.actual.json',{version:'0.6.0',architecture:arm.architecture,executable:pin(executable),args,cwd:runtime,timeoutMs:200000,drainMs:2000,residentReadyMs:45000,residentSmokeMs:180000,sourceProducer:manifest.currentConsumers[arm.architecture],originalReleaseRelabeled:false,inputInjection:false});
      run=launchOwned(executable,args,{runner:manifest.tools.runner.path,cwd:runtime,report,timeoutMs:200000,drainMs:2000,stdout:out,stderr:err});
      try{ready=await run.ready;write(folder+'/ready.json',ready);console.log(JSON.stringify({architecture:arm.architecture,event:'native-subject-ready',...ready,applicationReadyEstablished:false}));}catch(error){write(folder+'/ready-error.json',{message:error.message});}
      result=await run.finished;write(folder+'/finished.json',result);natural(result);
      const context=json(report+'.context.json');reader.verifyStream(fs.readFileSync(report),context,ready,result);
      assert.deepEqual(context.args,args);assert.equal(context.timeoutMs,200000);assert.equal(context.drainMs,2000);assert.equal(context.executableSha256,pin(executable).sha256);assert.equal(context.runnerSha256,manifest.tools.runner.sha256);assert(ready&&ready.pid===result.pid&&ready.creationFileTime===result.creationFileTime);
      const rows=records(log),first=rows.find(r=>r.event==='resident.started'),last=rows.findLast(r=>r.event==='resident.finished');
      assert(first&&first.sdkVersion==='0.6.0'&&first.architecture===arm.architecture&&first.readyTimeoutMs===45000&&first.smokeTimeoutMs===180000);assert(rows.every((r,i)=>r.runId===first.runId&&r.sequence===i+1&&Number.isSafeInteger(r.elapsedMs)&&r.elapsedMs>=0&&(i===0||r.elapsedMs>=rows[i-1].elapsedMs)));
      assert(rows.some(r=>r.event==='smoke.final-three-recreated'));assert(rows.some(r=>r.event==='exit.cleanup-finished'&&r.exitCode===0));assert(last&&last.exitCode===0&&last.orderedShutdown===true&&last.smokeCompleted===true);
      runtimeCheck(arm);verify();const answer={architecture:arm.architecture,ready,result,residentLog:pin(log),records:rows.length,passed:true,wholeJobNatural:true,original45And180InnerBudgetsUnchanged:true,originalReleaseRelabeled:false,genuineShellGestureQualified:false,longResidencyQualified:false,productionQualificationComplete:false,releaseReady:false};write(folder+'/Analysis.actual.json',answer);results.push(answer);
    }catch(error){caseError=error;throw error;}finally{
      if(run&&!result){run.stop();result=await run.finished;write(folder+'/unfinished-cleanup.json',result);}fs.closeSync(out);fs.closeSync(err);
      try{const observedLogs=logs(runtime),copies=[];for(const p of observedLogs){const target=path.join(folder,'runtime-'+p.relative.replaceAll('/','-')+'.raw');fs.copyFileSync(p.path,target,fs.constants.COPYFILE_EXCL);const copied=pin(target);assert.equal(copied.sha256,p.sha256);assert.equal(copied.bytes,p.bytes);copies.push({source:p,copy:copied});}write(folder+'/Runtime.after.json',{runtime,observedLogs,copies});runtimeCheck(arm);}catch(error){if(caseError)write(folder+'/Runtime.postcheck-error.json',{message:error.message,primaryFailure:caseError.message});else throw error;}
    }
  }}catch(error){failure={message:error.message,stack:error.stack};}
  finally{try{verify();for(const arm of manifest.arms)runtimeCheck(arm);}catch(error){failure||={message:error.message};}write(output+'/Summary.actual.json',{schema:'soui.current-workbench-finite-result/v1',version:'0.6.0',manifest:pin(manifestFile),stage:pin(stageFile),results,failure,attempted,remaining:manifest.arms.filter(a=>!attempted.includes(a.architecture)).map(a=>a.architecture),bothCurrentArchitectureFiniteCasesPassed:!failure&&results.length===2,originalReleaseRelabeled:false,genuineShellGestureQualified:false,longResidencyQualified:false,currentSdk060Qualified:false,productionQualificationComplete:false,releaseReady:false});}
  if(failure)throw Error(failure.message);console.log(JSON.stringify({summary:pin(output+'/Summary.actual.json'),bothCurrentArchitectureFiniteCasesPassed:true,releaseReady:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
