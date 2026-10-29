'use strict';
// VM-only metadata preflight, followed by a separately authorized, serial finite run.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:sha(b)}};
const json=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function check(p){const a=pin(p.path);assert.equal(a.bytes,p.bytes);assert.equal(a.sha256,p.sha256);}
function files(root,prefix=''){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=prefix+e.name,p=path.join(root,e.name);return e.isDirectory()?files(p,n+'/'):(assert(e.isFile()),[n]);}).sort();}
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
async function main(){
 const [mode,manifestFile,manifestSha,expectedMachine,output,gate,preflightFile,preflightSha]=process.argv.slice(2);
 assert(['preflight','run'].includes(mode));assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');
 assert(/^[a-f0-9]{64}$/.test(manifestSha));assert.equal(sha(fs.readFileSync(manifestFile)),manifestSha);
 const manifest=json(manifestFile);assert.equal(manifest.schema,'soui.message-port-vm-carrier/v1');
 assert.equal(os.hostname().toLowerCase(),expectedMachine.toLowerCase());assert.notEqual(os.hostname().toLowerCase(),manifest.preparationHost.toLowerCase(),'Never run this controller on the build host');
 assert(/^C:\\issue-closeout-evidence\\mp77-[a-z0-9-]+$/i.test(output));assert(fs.statSync(output).isDirectory());
 for(const name of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE'])assert(!process.env[name],'Node/product injection environment must be cleared');
 const cfgFile=manifest.executionInput.path;check(manifest.executionInput);const cfg=json(cfgFile);
 assert(cfg.executionEnabled&&!cfg.rootIndependentReviewPending);assert.equal(cfg.sourceVersion,'0.5.1-preview.1');
 assert.equal(cfg.originalProcessTimeoutMs,214500);assert.equal(cfg.originalOwnedTimeoutMs,394500);assert.equal(cfg.originalDrainMs,2000);
 assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(cfg.requiredExecutionNode.path).toLowerCase());check({...cfg.requiredExecutionNode,path:process.execPath});
 assert.equal(process.env.SOUI_OWNED_PROCESS_RUNNER,cfg.subjectRunner.path);check(cfg.subjectRunner);
 const runner=require(cfg.originalRunner.path);
 const checkInputs=()=>{check(manifest.executionInput);for(const p of cfg.pins)check(p);for(const arm of cfg.arms){const b=runner.verify(arm.suite);assert.equal(b.consumerBuildId,arm.consumerBuildId);assert.equal(b.version,'0.5.1-preview.1');assert.equal(b.sourceSha256,cfg.sourceSha256);}};
 if(mode==='preflight'){
  // This occurs before any browser is launched and checks every transported raw input.
  for(const row of manifest.files)check({...row,path:row.guestPath});
  for(const root of manifest.closedReaderRoots){const names=files(root);const expected=manifest.files.filter(p=>p.guestPath.toLowerCase().startsWith(root.toLowerCase()+'/')).map(p=>p.guestPath.slice(root.length+1));if(manifestFile.toLowerCase().startsWith(root.toLowerCase()+'/'))expected.push(manifestFile.slice(root.length+1));assert.deepEqual(names,expected.sort(),'Copied reader root closed set differs');}
  checkInputs();
  write(output+'/Preflight.actual.json',{schema:'soui.message-port-guest-preflight/v1',passed:true,
   machine:os.hostname(),osRelease:os.release(),node:pin(process.execPath),manifest:pin(manifestFile),executionInput:pin(cfgFile),
   verifiedRawFiles:manifest.files.length,verifiedRawBytes:manifest.files.reduce((sum,p)=>sum+p.bytes,0),
   suites:cfg.arms.map(arm=>({key:arm.key,suite:arm.suite,consumerBuildId:arm.consumerBuildId,build:pin(arm.suite+'/build.json')})),
   sourceVersion:'0.5.1-preview.1',sourceSha256:cfg.sourceSha256,browserBehaviorExecuted:false,currentSdk060Qualified:false,releaseReady:false});
  console.log(JSON.stringify({preflight:pin(output+'/Preflight.actual.json'),browserBehaviorExecuted:false}));return;
 }
 assert.equal(gate,'ROOT_REVIEWED_GUEST_PREFLIGHT_V1');assert(/^[a-f0-9]{64}$/.test(preflightSha));assert.equal(pin(preflightFile).sha256,preflightSha);
 const preflight=json(preflightFile);assert(preflight.passed&&!preflight.browserBehaviorExecuted);assert.equal(preflight.machine,os.hostname());
 assert.equal(preflight.manifest.sha256,manifestSha);assert.equal(preflight.executionInput.sha256,manifest.executionInput.sha256);
 const lease=json(output+'/Lease.actual.json');assert.equal(lease.name,'Local\\SouiDevKit.DesktopValidation');assert.equal(lease.acquired,true);
 assert.equal(lease.ownerProcessId,process.ppid);assert.equal(lease.mode,'run');
 checkInputs();const {launchOwned}=require(cfg.sourceRoot+'/tools/internal/owned-process.cjs');
 const results=[],copies=[],attemptedArms=[];let failure=null;
 try{
  for(const arm of cfg.arms){
   const folder=output+'/'+arm.key;fs.mkdirSync(folder);const before=new Set(fs.readdirSync(arm.suite).filter(n=>/^reload-[a-f0-9-]{36}$/.test(n)));
   const args=[cfg.controller.path,'ROOT_CURRENT_MESSAGE_PORT_AB_V1',cfgFile,manifest.executionInput.sha256,arm.key];
   const stdout=fs.openSync(folder+'/stdout.raw','wx'),stderr=fs.openSync(folder+'/stderr.raw','wx');let child,result,ready;
   write(folder+'/Invocation.actual.json',{key:arm.key,executable:pin(process.execPath),args,cwd:cfg.sourceRoot,
    outerInitialTimeoutMs:420000,outerDrainMs:2000,originalNativeProcessTimeoutMs:214500,originalNativeOwnedTimeoutMs:394500,
    originalNativeDrainMs:2000,sourceVersion:'0.5.1-preview.1',sourceSha256:cfg.sourceSha256,heldDesktopLease:lease});
   try{
    attemptedArms.push(arm.key);child=launchOwned(process.execPath,args,{runner:cfg.subjectRunner.path,cwd:cfg.sourceRoot,report:folder+'/owned.ndjson',timeoutMs:420000,drainMs:2000,stdout,stderr});
    try{ready=await child.ready;write(folder+'/ready.json',ready);console.log(JSON.stringify({key:arm.key,event:'controller-ready',...ready}));}catch(error){write(folder+'/ready-error.json',{message:error.message});}
    result=await child.finished;write(folder+'/finished.json',result);
   }finally{if(child&&!result){child.stop();result=await child.finished;write(folder+'/unfinished-cleanup.json',result);}fs.closeSync(stdout);fs.closeSync(stderr);}
   // Preserve actual newly created original evidence even when its controller failed.
   const newRuns=fs.readdirSync(arm.suite).filter(n=>/^reload-[a-f0-9-]{36}$/.test(n)&&!before.has(n));
   const origins=[...newRuns.map(n=>({root:arm.suite+'/'+n,name:'original-'+n})),...(fs.existsSync(arm.observationOutput)?[{root:arm.observationOutput,name:'supplemental'}]:[])];
   for(const origin of origins)for(const name of files(origin.root)){
    const source=origin.root+'/'+name,target=folder+'/'+origin.name+'/'+name,original=pin(source);
    fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
    const copied=pin(target);assert.equal(copied.sha256,original.sha256);assert.equal(copied.bytes,original.bytes);check(original);
    copies.push({source:original,copied});
   }
   write(folder+'/CopiedOriginals.actual.json',copies.filter(c=>c.copied.path.startsWith(folder+'/')));
   natural(result);assert.equal(newRuns.length,1,'Expected one actual original run');
   const answer=JSON.parse(fs.readFileSync(folder+'/stdout.raw','utf8').trim());assert.equal(answer.key,arm.key);assert.equal(answer.passed,true);
   assert.equal(answer.releaseReady,false);checkInputs();results.push({key:arm.key,answer,ready,result});
  }
 }catch(error){failure={message:error.message,stack:error.stack};}
 finally{
  try{checkInputs();}catch(error){failure||={message:error.message,stack:error.stack};}
  write(output+'/Summary.actual.json',{schema:'soui.message-port-guest-four-arm-result/v1',
   sourceVersion:'0.5.1-preview.1',sourceSha256:cfg.sourceSha256,preflight:pin(preflightFile),manifest:pin(manifestFile),
   results,failure,attemptedArms,untouchedArmsNotRun:cfg.arms.filter(row=>!attemptedArms.includes(row.key)).map(row=>row.key),
   allFourFiniteArmsPassed:!failure&&results.length===4,currentSdk060Qualified:false,
   completeKernelToolchainQualified:false,resourcePassed:false,qualificationPassed:false,releaseReady:false});
 }
 if(failure)throw Error(failure.message);
 console.log(JSON.stringify({summary:pin(output+'/Summary.actual.json'),allFourFiniteArmsPassed:true,currentSdk060Qualified:false,releaseReady:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1});
