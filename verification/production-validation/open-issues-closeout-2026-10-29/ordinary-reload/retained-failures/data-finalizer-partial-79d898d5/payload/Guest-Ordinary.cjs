'use strict';
// Current0.6 installed consumer only. Old0.5.1 suites/readers remain read-only.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {StringDecoder}=require('node:string_decoder');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};}
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);}
function json(f){return JSON.parse(fs.readFileSync(f,'utf8'));}
function write(f,o){fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});}
function names(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?names(d,n):(assert(e.isFile()),[n]);}).sort();}
function relative(p){assert(typeof p==='string'&&!path.isAbsolute(p)&&!p.includes('\\')&&p.split('/').every(s=>s&&s!=='.'&&s!=='..'&&!s.includes(':')));return p;}
function records(f){const b=fs.readFileSync(f);assert(b.length&&b.at(-1)===10,'Missing/truncated NDJSON');return b.toString('utf8').trimEnd().split('\n').map(JSON.parse);}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
function verifyRuntime(arm){
  assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);
  const d=path.join(root,arm.runtimeDirectory),expected=arm.files.map(p=>relative(p.relative)).sort();assert.deepEqual(names(d).filter(n=>n!=='resources/source-navigation.log'),expected);
  for(const p of arm.files)check({...p,path:path.join(d,relative(p.relative))});
}
async function main(){
  const [mode,manifestFile,manifestSha,machine,output,gate,stageFile,stageSha]=process.argv.slice(2);
  assert(['stage','run'].includes(mode));assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');assert(/^C:\\ordc-[a-z0-9-]*$/i.test(root));
  assert(/^[a-f0-9]{64}$/.test(manifestSha));assert.equal(pin(manifestFile).sha256,manifestSha);const manifest=json(manifestFile);
  assert.equal(manifest.schema,'soui.current-ordinary-acceptance-light-payload/v1');assert.equal(manifest.version,'0.6.0');assert.deepEqual(manifest.arms.map(a=>a.architecture),['x64','x86']);assert.equal(os.hostname().toLowerCase(),machine.toLowerCase());assert.notEqual(os.hostname().toLowerCase(),manifest.preparationHost.toLowerCase());
  assert(/^C:\\issue-closeout-evidence\\ordc-[a-z0-9-]+$/i.test(output));assert(fs.statSync(output).isDirectory());
  for(const name of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'])assert(!process.env[name]);
  const verifyInputs=()=>{assert.equal(pin(manifestFile).sha256,manifestSha);for(const p of manifest.payloadFiles)check({...p,path:path.join(root,relative(p.relative))});for(const p of manifest.readerPins)check(p);for(const p of Object.values(manifest.tools))check(p);};
  assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(manifest.tools.node.path).toLowerCase());verifyInputs();
  const lease=json(output+'/Lease.actual.json');assert(lease.acquired&&lease.ownerProcessId===process.ppid&&lease.name==='Local\\SouiDevKit.DesktopValidation');
  if(mode==='stage'){
    const copies=[];
    for(const arm of manifest.arms){
      assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);
      const d=path.join(root,arm.runtimeDirectory);assert(!fs.existsSync(d),'Preserve prior current runtime');fs.mkdirSync(d);
      for(const f of arm.files){const source=f.fromPayload?path.join(root,relative(f.source)):f.source;assert(path.isAbsolute(source));check({...f,path:source});const target=path.join(d,relative(f.relative));fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);check({...f,path:target});check({...f,path:source});copies.push({source:pin(source),copy:pin(target)});}
      verifyRuntime(arm);
    }
    verifyInputs();write(output+'/Stage.actual.json',{schema:'soui.current-ordinary-acceptance-stage/v1',passed:true,machine:os.hostname(),manifest:pin(manifestFile),copies,oldSuitesChanged:false,version:'0.6.0',productExecuted:false,messagePortBehaviorAsserted:false,releaseReady:false});
    console.log(JSON.stringify({stage:pin(output+'/Stage.actual.json'),productExecuted:false}));return;
  }
  assert.equal(gate,'ROOT_REVIEWED_CURRENT_ORDINARY_STAGE_V1');assert(/^[a-f0-9]{64}$/.test(stageSha));assert.equal(pin(stageFile).sha256,stageSha);const staged=json(stageFile);assert(staged.passed&&!staged.productExecuted&&staged.manifest.sha256===manifestSha&&staged.machine===os.hostname());
  const reader=require(manifest.readers.runner),{createSupervisor}=require(manifest.readers.supervisor),{launchOwned}=require(manifest.tools.ownedModule.path);
  const config=reader.settings('reload',3,0,500,20000);assert.equal(reader.expectedSteps(config).length,9);
  const results=[],attempted=[];let failure=null;
  try{for(const arm of manifest.arms){
    verifyInputs();verifyRuntime(arm);attempted.push(arm.architecture);const d=path.join(output,arm.architecture);fs.mkdirSync(d);
    const runtime=path.join(root,arm.runtimeDirectory),executable=path.join(runtime,'SouiAcceptance.exe'),log=path.join(d,'events.ndjson'),ownedLog=path.join(d,'owned.ndjson');
    const args=['--mode=reload','--cycles=3','--warmup=0','--settle-ms=500','--timeout-ms=20000','--report='+log];
    const out=fs.openSync(path.join(d,'stdout.raw'),'wx'),err=fs.openSync(path.join(d,'stderr.raw'),'wx');let child=null,result=null,ready=null,timeoutReason=null,timer=null,monitor=null;
    let observedBytes=0,partial='',supervisor=null;const decoder=new StringDecoder('utf8');
    const expire=reason=>{if(timeoutReason!==null)return;timeoutReason=reason;write(path.join(d,'Timeout.actual.json'),{reason,observedBytes,partialBytes:Buffer.byteLength(partial),...supervisor.snapshot()});child.stop();};
    try{
      write(path.join(d,'Invocation.actual.json'),{version:'0.6.0',architecture:arm.architecture,executable:pin(executable),args,cwd:runtime,processTimeoutMs:214500,ownedTimeoutMs:394500,drainMs:2000,config,installedSdkProducer:manifest.installedSdkProducers[arm.architecture],webRebuilt:false,messagePortBehaviorAsserted:false});
      child=launchOwned(executable,args,{runner:manifest.tools.runner.path,cwd:runtime,report:ownedLog,timeoutMs:394500,drainMs:2000,stdout:out,stderr:err});
      ready=await child.ready;write(path.join(d,'ready.json'),ready);console.log(JSON.stringify({architecture:arm.architecture,event:'native-subject-ready',...ready}));
      supervisor=createSupervisor(config);timer=setTimeout(()=>expire('process-deadline'),214500);
      monitor=setInterval(()=>{try{
        if(fs.existsSync(log)){const size=fs.statSync(log).size;if(size<observedBytes)return expire('native-log-truncated');if(size>observedBytes){const fd=fs.openSync(log,'r'),b=Buffer.alloc(size-observedBytes);let n;try{n=fs.readSync(fd,b,0,b.length,observedBytes);}finally{fs.closeSync(fd);}observedBytes+=n;const lines=(partial+decoder.write(b.subarray(0,n))).split('\n');partial=lines.pop();for(const line of lines)supervisor.observe(JSON.parse(line));}}
        const reason=supervisor.deadlineReason();if(reason)expire(reason);
      }catch(error){expire('native-observation-failed:'+error.message);}},250);
      result=await child.finished;write(path.join(d,'finished.json'),result);
    }finally{clearTimeout(timer);clearInterval(monitor);if(child&&!result){child.stop();result=await child.finished;write(path.join(d,'unfinished-cleanup.json'),result);}fs.closeSync(out);fs.closeSync(err);}
    natural(result);assert.equal(timeoutReason,null);verifyRuntime(arm);verifyInputs();
    const raw=records(log),outcome={pid:result.pid,exitCode:result.exitCode,signal:result.signal,timedOut:false,timeoutReason:null,spawnError:result.spawnError,processTimeoutMs:214500,ownedProcess:result};
    const actual=reader.verifyNativeIdentity(raw,outcome),lifecycle=reader.validateRecords(raw,config,outcome);assert.equal(lifecycle.completedSteps,9);assert.equal(lifecycle.measuredCycles,3);assert.equal(lifecycle.samples.length,7);
    assert.equal(raw.length,27); // Original nine step pairs, seven actual samples, run begin/end.
    const mutable=[];for(const name of ['resources/source-navigation.log'])if(fs.existsSync(path.join(runtime,name))){const source=pin(path.join(runtime,name)),target=path.join(d,'source-navigation.log.raw');fs.copyFileSync(source.path,target,fs.constants.COPYFILE_EXCL);const copied=pin(target);assert.equal(source.sha256,copied.sha256);assert.equal(source.bytes,copied.bytes);mutable.push({source,copied});}
    const answer={architecture:arm.architecture,ready,result,lifecycle,nativeEvents:pin(log),browserBehaviorQualified:false,messagePortBehaviorAsserted:false,mutableRuntimeObservations:mutable,passed:true,wholeJobNatural:true,version:'0.6.0',currentSdk060Qualified:false,productionQualificationComplete:false,releaseReady:false};
    write(path.join(d,'Analysis.actual.json'),answer);results.push(answer);
  }}catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    try{verifyInputs();for(const arm of manifest.arms)verifyRuntime(arm);}catch(error){failure||={message:error.message};}
    write(output+'/Summary.actual.json',{schema:'soui.current-ordinary-acceptance-finite-result/v1',version:'0.6.0',manifest:pin(manifestFile),stage:pin(stageFile),results,failure,attempted,remaining:manifest.arms.filter(a=>!attempted.includes(a.architecture)).map(a=>a.architecture),bothCurrentArchitectureFiniteCasesPassed:!failure&&results.length===2,messagePortBehaviorAsserted:false,currentSdk060Qualified:false,resourcePassed:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false});
  }
  if(failure)throw Error(failure.message);console.log(JSON.stringify({summary:pin(output+'/Summary.actual.json'),bothCurrentArchitectureFiniteCasesPassed:true,messagePortBehaviorAsserted:false,releaseReady:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
