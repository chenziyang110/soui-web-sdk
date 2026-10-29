'use strict';
// Run only in the task VM, after Root reviews this exact payload and command.
// Never injects input, restarts Shell, changes foreground policy, or infers UI from return codes.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};};
const write=(d,n,v)=>fs.writeFileSync(path.join(d,n),JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function walk(d,prefix=''){const result=[];for(const e of fs.readdirSync(d,{withFileTypes:true})){assert(!e.isSymbolicLink());const p=prefix+e.name;if(e.isDirectory())result.push(...walk(path.join(d,e.name),p+'/'));else{assert(e.isFile());result.push(p);}}return result.sort();}
function relative(p){assert(typeof p==='string'&&!path.isAbsolute(p)&&p&&!p.includes('\\')&&!p.split('/').some(s=>!s||s==='.'||s==='..'||s.includes(':')));return p;}
function verifyFiles(directory,rows,ignored=[]){
  assert.equal(new Set(rows.map(r=>r.path)).size,rows.length);assert.deepEqual(walk(directory).filter(p=>!ignored.includes(p)),rows.map(r=>r.path).sort());
  for(const row of rows){const p=pin(path.join(directory,relative(row.path)));assert.equal(p.bytes,row.bytes);assert.equal(p.sha256,row.sha256);}
}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
async function main(){
  assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');
  assert(/^C:\\issue[a-z0-9-]{1,40}$/i.test(root),'Task VM short C:/issue... payload root required');
  const args=process.argv.slice(2);assert.equal(args.length,6);assert.equal(args[0],'--cases');assert.equal(args[2],'--output');assert.equal(args[4],'--manifest-sha256');assert(/^[a-f0-9]{64}$/.test(args[5]));
  const selected=args[1].split(','),output=path.resolve(args[3]);
  assert(/^C:\\issue-closeout-evidence\\[a-z0-9-]{1,80}$/i.test(output),'Fresh outside-payload evidence root required');assert(!fs.existsSync(output));
  const manifestFile=path.join(root,'Payload.manifest.json'),manifestBytes=fs.readFileSync(manifestFile);assert.equal(hash(manifestBytes),args[5]);
  const manifest=JSON.parse(manifestBytes);assert.equal(manifest.schema,'soui.current-public-tray-vm-payload-manifest/v1');
  const verify=()=>{assert.equal(hash(fs.readFileSync(manifestFile)),args[5]);verifyFiles(root,manifest.files,['Payload.manifest.json']);};verify();
  assert.equal(new Set(selected).size,selected.length);assert(selected.length>0&&selected.every(n=>manifest.cases.some(c=>c.name===n)));
  assert.equal(path.resolve(process.execPath).toLowerCase(),path.join(root,'tools/node.exe').toLowerCase());
  fs.mkdirSync(path.dirname(output),{recursive:true});fs.mkdirSync(output);
  const {launchOwned}=require('./tools/owned-process.cjs'),runner=path.join(root,'tools/OwnedProcessRunner.exe');
  const outcomes=[];let failure=null;
  write(output,'Inputs.actual.json',{schema:'soui.current-public-desktop-short-inputs/v1',manifest:pin(manifestFile),controller:pin(__filename),node:pin(process.execPath),runner:pin(runner),selected,productExecutedOnHost:false,uiInteractionQualified:false});
  try{for(const name of selected){
    verify();const plan=manifest.cases.find(c=>c.name===name),folder=path.join(output,name);fs.mkdirSync(folder);
    let runtime,executable,nativeArgs;const ignored=['resources/source-navigation.log','resources/webview-smoke.log'];
    if(plan.kind==='tray'){runtime=path.join(root,plan.runtime);executable=path.join(runtime,'SouiTrayConsumer.exe');nativeArgs=['--interactive'];}
    else{
      runtime=plan.runtime;verifyFiles(runtime,plan.files,ignored);executable=path.join(runtime,'SouiWorkbench.exe');
      nativeArgs=['--resident','--resident-smoke-test','--react','--page=data','--resident-log='+path.join(folder,'resident.ndjson')];
      write(folder,'ResidentPayload.before.json',{root:runtime,files:plan.files,allowedGeneratedLogObservations:ignored.filter(p=>fs.existsSync(path.join(runtime,p))).map(p=>pin(path.join(runtime,p)))});
    }
    const report=path.join(folder,'owned.ndjson'),out=fs.openSync(path.join(folder,'stdout.bin'),'wx'),err=fs.openSync(path.join(folder,'stderr.bin'),'wx');
    const environment={...process.env};for(const k of ['SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'])delete environment[k];
    let run=null,result=null,ready=null;
    try{
      write(folder,'Invocation.actual.json',{executable,args:nativeArgs,cwd:runtime,timeoutMs:plan.timeoutMs,drainMs:2000,nativeInteractiveMs:plan.kind==='tray'?180000:null,residentReadyMs:plan.kind==='resident'?45000:null,residentSmokeMs:plan.kind==='resident'?180000:null,inputInjection:false});
      run=launchOwned(executable,nativeArgs,{runner,cwd:runtime,report,timeoutMs:plan.timeoutMs,drainMs:2000,env:environment,stdout:out,stderr:err});
      try{ready=await run.ready;write(folder,'ready.json',ready);console.log(JSON.stringify({case:name,event:'owned-subject-ready',...ready,applicationReadyEstablished:false}));}
      catch(error){write(folder,'ready-error.json',{message:error.message});}
      result=await run.finished;write(folder,'finished.json',result);natural(result);
      const context=JSON.parse(fs.readFileSync(report+'.context.json'));assert.deepEqual(context.args,nativeArgs);assert.equal(context.timeoutMs,plan.timeoutMs);assert.equal(context.drainMs,2000);assert.equal(context.runnerSha256,pin(runner).sha256);assert.equal(context.executableSha256,pin(executable).sha256);assert(ready&&ready.pid===result.pid&&ready.creationFileTime===result.creationFileTime);
      const logFile=plan.kind==='tray'?path.join(folder,'stdout.bin'):path.join(folder,'resident.ndjson'),raw=fs.readFileSync(logFile);assert(raw.length&&raw.at(-1)===10);
      const rows=raw.toString('utf8').trim().split('\n').map(JSON.parse);
      if(plan.kind==='tray'){const first=rows[0],last=rows.at(-1);assert.equal(first.event,'run.started');assert.equal(first.sdkVersion,'0.6.0');assert.equal(first.architecture,plan.architecture);assert.equal(first.timeoutMs,180000);assert.equal(last.event,'run.finished');assert.equal(last.exitCode,0);assert.equal(last.cleanupConfirmed,true);}
      else{const first=rows.find(r=>r.event==='resident.started'),last=rows.findLast(r=>r.event==='resident.finished');assert(first&&first.readyTimeoutMs===45000&&first.smokeTimeoutMs===180000);assert(rows.some(r=>r.event==='smoke.final-three-recreated'));assert(rows.some(r=>r.event==='exit.cleanup-finished'&&r.exitCode===0));assert(last&&last.exitCode===0&&last.orderedShutdown===true&&last.smokeCompleted===true);}
      outcomes.push({name,ready,result,log:pin(logFile),naturalProcessClose:true,uiInteractionIndependentlyQualified:false});
    }finally{if(run&&!result){run.stop();await run.finished;}fs.closeSync(out);fs.closeSync(err);}
    if(plan.kind==='resident'){verifyFiles(runtime,plan.files,ignored);write(folder,'ResidentPayload.after.json',{root:runtime,files:plan.files,allowedGeneratedLogObservations:ignored.filter(p=>fs.existsSync(path.join(runtime,p))).map(p=>pin(path.join(runtime,p)))});}
    verify();
  }}catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    try{verify();}catch(error){failure||={message:error.message};}
    write(output,'Summary.actual.json',{schema:'soui.current-public-desktop-short-execution/v1',selected,outcomes,failure,nativeExecutionAndNaturalClosePassed:!failure&&outcomes.length===selected.length,uiInteractionIndependentlyQualified:false,explorerRestartQualified:false,win7Qualified:false,productionQualificationComplete:false,releaseReady:false});
    const files=walk(output).map(p=>pin(path.join(output,p)));
    write(output,'ExportFiles.actual.json',{schema:'soui.issue-short-export-files/v1',files,totalBytes:files.reduce((n,f)=>n+f.bytes,0),scope:'fresh actual runtime/Owned/raw files only; no UI success inferred'});
  }
  if(failure)throw Error(failure.message);
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
