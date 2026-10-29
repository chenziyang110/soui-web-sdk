'use strict';
// One task-VM native diagnostic only. Preparation does not execute this script.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const machine='SOUI-7AEMMK5Q0T',outer='C:/issue-closeout-evidence/focusc-a2-run',inner='C:/issue-closeout-evidence/focusc-a2';
const prepSha='6b5fd304995a766913b5d6d765b7a71e4eb5755a6043a05c36865cbb27e609e6';
const tools={node:{path:'C:/issues8/tools/node.exe',bytes:91380224,sha256:'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'},
  runner:{path:'C:/issues8/tools/OwnedProcessRunner.exe',bytes:170496,sha256:'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83'},
  owned:{path:'C:/issues8/tools/owned-process.cjs',bytes:8386,sha256:'8ccec5802c793b45990d2d7585f2c29fb618a7b170adebd5d270a7c291757ce7'}};
const powershell='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(file){const before=fs.lstatSync(file,{bigint:true});assert(before.isFile()&&!before.isSymbolicLink());const bytes=fs.readFileSync(file),after=fs.lstatSync(file,{bigint:true});assert(['dev','ino','size','mtimeNs','ctimeNs'].every(k=>before[k]===after[k]));assert.equal(BigInt(bytes.length),before.size);return {path:file,bytes:bytes.length,sha256:sha(bytes)};}
function check(expected){const actual=pin(expected.path);assert.equal(actual.bytes,expected.bytes);assert.equal(actual.sha256,expected.sha256);}
function write(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
function samePath(a,b){return path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();}
function closedWithoutForce(r){const t=r&&r.terminal;return !!(r&&Number.isInteger(r.exitCode)&&r.supervisorExitCode===r.exitCode&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed&&t&&t.reason===(r.exitCode===0?'completed':'root-failed')&&t.rootExited&&t.rootExitCode===r.exitCode&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&t.totalProcesses>=1&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed);}
async function main(){
  assert.equal(process.argv.length,2);assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');assert.equal(os.hostname().toUpperCase(),machine);assert(samePath(__dirname,'D:/'));
  assert(samePath(process.execPath,tools.node.path));for(const p of Object.values(tools))check(p);
  for(const name of ['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'])assert(!process.env[name]);
  const crtFile=path.join(__dirname,'Loader-Crt.inputs.json'),crtFilePin=pin(crtFile);
  assert.equal(crtFilePin.sha256,'1a6c1c0987af3e259aff0c2f9615dce8b893267efd4e1fdb6de4d5f625ac90ce');
  const crt=JSON.parse(fs.readFileSync(crtFile,'utf8'));
  assert.equal(crt.schema,'soui.native-focus-child-crt-inputs/v1');assert.equal(crt.guestDirectory,'C:/issues8/tray-x64');
  assert.equal(crt.files.length,16);check(crt.manifest);
  function checkCrtDirectory(){
    const directory=fs.lstatSync(crt.guestDirectory);assert(directory.isDirectory()&&!directory.isSymbolicLink());
    const expected=crt.files.map(p=>{assert(samePath(path.dirname(p.path),crt.guestDirectory));check(p);return path.basename(p.path);}).sort();
    assert.deepEqual(fs.readdirSync(crt.guestDirectory).sort(),expected,'Original transported x64 CRT prefix closed set differs');
  }
  checkCrtDirectory();
  const parentEnvironment={...process.env},pathKeys=Object.keys(parentEnvironment).filter(k=>k.toLowerCase()==='path');
  assert(pathKeys.length<=1,'Ambiguous inherited Windows PATH keys');
  const inheritedPath=pathKeys.length?parentEnvironment[pathKeys[0]]:undefined,childEnvironment={...parentEnvironment};
  for(const key of pathKeys)delete childEnvironment[key];
  childEnvironment.PATH=crt.guestDirectory+(inheritedPath?';'+inheritedPath:'');
  const environmentPin=value=>({present:value!==undefined,utf8Bytes:value===undefined?0:Buffer.byteLength(value,'utf8'),sha256:value===undefined?null:sha(Buffer.from(value,'utf8'))});
  const childPathEvidence={scope:'Only launchOwned supervisor and its PowerShell descendant inherit this PATH; parent process.env is never assigned',crtDirectory:crt.guestDirectory,
    inheritedPath:environmentPin(inheritedPath),childPath:environmentPin(childEnvironment.PATH),environmentValuesPersisted:false,productExecuted:false};
  let parentEnvironmentUnchanged=null;
  const prepFile=path.join(__dirname,'Preparation.actual.json'),prepPin=pin(prepFile);assert.equal(prepPin.sha256,prepSha);const prep=JSON.parse(fs.readFileSync(prepFile,'utf8'));
  assert.equal(prep.schema,'soui.native-tray-focus-control-preparation/v1');assert.equal(prep.expectedGuest,machine);assert.equal(prep.compiled,false);assert.equal(prep.executed,false);
  const sourcePins=[pin(__filename),prepPin,pin(path.join(__dirname,'FocusExport-a2.ps1')),crtFilePin,crt.manifest,...crt.files,pin(powershell),...Object.values(tools).map(p=>pin(p.path))];
  for(const f of prep.files){assert(/^[A-Za-z0-9][A-Za-z0-9_.-]+$/.test(f.relative));const p=pin(path.join(__dirname,f.relative));assert.equal(p.bytes,f.bytes);assert.equal(p.sha256,f.sha256);sourcePins.push(p);}
  assert(!fs.existsSync(outer)&&!fs.existsSync(inner),'Preserve every previous diagnostic/evidence directory');fs.mkdirSync(outer);
  const args=['-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',path.join(__dirname,'Invoke-NativeFocusControl.ps1'),
    '-ExpectedMachine',machine,'-EvidenceDirectory',inner,'-PreparationSha256',prepSha,'-RootGate','ROOT_REVIEWED_NATIVE_FOCUS_CONTROL_V1'];
  const invocation={schema:'soui.native-focus-owned-invocation/v1',machine,controllerPid:process.pid,controllerBirthNotCollected:true,
    controllerActivePidMustBeAbsentBeforeExport:true,executable:powershell,args,cwd:__dirname,timeoutMs:90000,drainMs:2000,innerDeadlineMs:60000,
    sourcePins,childPathEvidence,innerDirectory:inner,outerDirectory:outer,inputInjection:false,oldTrayConsumerRerun:false,sdkExecuted:false,releaseReady:false};
  write(path.join(outer,'Invocation.actual.json'),invocation);
  let run=null,ready=null,result=null,failure=null,readyError=null;const copies=[],missing=[];
  const out=fs.openSync(path.join(outer,'stdout.raw'),'wx'),err=fs.openSync(path.join(outer,'stderr.raw'),'wx');
  try{
    const {launchOwned}=require(tools.owned.path);
    run=launchOwned(powershell,args,{runner:tools.runner.path,cwd:__dirname,report:path.join(outer,'owned.ndjson'),env:childEnvironment,timeoutMs:90000,drainMs:2000,stdout:out,stderr:err});
    try{ready=await run.ready;write(path.join(outer,'ready.json'),ready);console.log(JSON.stringify({event:'focus-powershell-ready',...ready,nativeControlReadyEstablished:false}));}
    catch(error){readyError=error.message;write(path.join(outer,'ready-error.json'),{message:error.message});}
    result=await run.finished;write(path.join(outer,'finished.json'),result);
  }catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    if(run&&!result){run.stop();result=await run.finished;write(path.join(outer,'unfinished-cleanup.json'),result);}
    fs.closeSync(out);fs.closeSync(err);
    try{
      const afterEnvironment={...process.env};assert(Object.keys(afterEnvironment).length===Object.keys(parentEnvironment).length&&Object.keys(parentEnvironment).every(k=>afterEnvironment[k]===parentEnvironment[k]),'Parent environment changed');parentEnvironmentUnchanged=true;
      for(const expected of sourcePins)check(expected);checkCrtDirectory();
      // Only these five explicit inner records may be copied; all are read
      // after the original Owned root and its entire Job have closed.
      assert(result&&result.closureConfirmed&&result.terminal&&result.terminal.activeProcesses===0,'Do not collect/export while the native diagnostic job is live or closure unknown');
      const names=['Invocation.actual.json','Compilation.actual.json','Native.events.ndjson','PowerShell.error.json','PowerShell.finished.json'];
      if(fs.existsSync(inner)){
        const d=fs.lstatSync(inner);assert(d.isDirectory()&&!d.isSymbolicLink());fs.mkdirSync(path.join(outer,'native'));
        for(const name of names){const source=path.join(inner,name);if(!fs.existsSync(source)){missing.push(name);continue;}
          const before=pin(source);assert(before.bytes<=8388608);const target=path.join(outer,'native',name);fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);const copy=pin(target);assert.equal(copy.bytes,before.bytes);assert.equal(copy.sha256,before.sha256);check(before);copies.push({source:before,copy});}
      }else missing.push(...names);
    }catch(error){failure??={message:error.message,stack:error.stack};}
    const psFile=path.join(outer,'native/PowerShell.finished.json');let ps=null;
    if(fs.existsSync(psFile)){try{ps=JSON.parse(fs.readFileSync(psFile,'utf8').replace(/^\uFEFF/,''));}catch(error){failure??={message:error.message};}}
    write(path.join(outer,'Summary.actual.json'),{schema:'soui.native-focus-owned-result/v1',machine,controllerPid:process.pid,controllerBirthNotCollected:true,
      invocation:pin(path.join(outer,'Invocation.actual.json')),ready,readyError,result,nativeRootExitCode:result?result.exitCode:null,
      innerLauncherExitCode:ps?ps.exitCode:null,observedProbeExitCode:ps?ps.probeExitCode:null,
      wholeJobClosedWithoutForce:closedWithoutForce(result),jobClosureDoesNotEstablishSuccess:true,probeExitMeaning:'0 accepted native control;1 nativeFALSE;2 invalid. Job closure is separate from that outcome. Setup may fail before a probe exit exists; never infer API FALSE from root exit alone.',
      childPathEvidence,parentEnvironmentUnchanged,copies,missingKnownRecords:missing,failure,inputInjection:false,sdkFixed:false,sdkSupervisionQualified:false,testPassed:null,productionQualificationComplete:false,releaseReady:false});
  }
  const wholeJobClosedWithoutForce=closedWithoutForce(result);console.log(JSON.stringify({outer,inner,nativeRootExitCode:result?result.exitCode:null,wholeJobClosedWithoutForce,failure,testPassed:null}));
  process.exitCode=failure||!wholeJobClosedWithoutForce||readyError?2:(Number.isInteger(result.exitCode)&&result.exitCode>=0&&result.exitCode<=255?result.exitCode:2);
}
main().catch(error=>{console.error(error.stack);process.exitCode=2;});
