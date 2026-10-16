'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',[mode,arch,referenceAttempt,controllerAttempt,runAttempt]=process.argv.slice(2);
assert(['outer','inner'].includes(mode)&&['x64','x86'].includes(arch)&&[referenceAttempt,controllerAttempt,runAttempt].every(x=>/^[1-9][0-9]*$/.test(x)));
const id=require(root+'/tools/internal/delivery-identity.cjs'),reader=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const appsFile=__dirname+'/reference-native-'+arch+'-'+referenceAttempt+'/ARTIFACTS.json',controllerFile=__dirname+'/reference-gui-build-'+arch+'-'+controllerAttempt+'/ARTIFACTS.json';
const apps=JSON.parse(fs.readFileSync(appsFile,'utf8')),controller=JSON.parse(fs.readFileSync(controllerFile,'utf8'));
const request=root+'/out/sdk-'+arch+'/llvm-profile-request-Release.json';
const sdk=require(root+'/tools/internal/build-evidence.cjs').verifyAcceptedBuild(root,arch,'Release',true,{nativeToolchain:'LLVM22MSVC142',profileRequest:request});
assert.equal(sdk.build.buildId,controller.sdkBuildId);
const folder=__dirname+'/reference-gui-'+arch+'-'+runAttempt,lease=__dirname+'/reference-gui-desktop-lease.ps1';
const pins=[...apps.pins,...controller.pins,controller.executable,controller.map,...[__filename,lease,appsFile,controllerFile,root+'/source-manifest.json'].map(reader.pin)];
const records=[];
function check(){pins.forEach(reader.checkPin);for(const app of apps.apps)assert.deepEqual(id.snapshot(app.root,id.walk(app.root)),app.payload);assert.deepEqual(id.snapshot(path.dirname(controller.executable.path),id.walk(path.dirname(controller.executable.path))),controller.payload);}
function lines(raw){assert(raw.length>0&&raw.length<=4*1024*1024&&raw.toString('utf8').endsWith('\n'));return raw.toString('utf8').trimEnd().split('\n').map(x=>JSON.parse(x));}
async function capture(name,executable,args,cwd,env,timeoutMs){
 check();const c=await standard.capture(folder,name,executable,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 records.push({name,natural,result:c.result,terminal:v.terminal,subject:c.ready});fs.writeFileSync(folder+'/'+mode+'-RESULT.json',JSON.stringify({arch,records,pins,physicalInputQualified:false,releaseReady:false},null,2));
 assert(natural,name+' did not close naturally; original records retained');check();return c;
}
function applicationLog(app,pid,ft){
 const profile=folder+'/local-app-data/SouiReferenceWorkbench/'+app.name,log=profile+'/app-observation.ndjson';
 const rows=lines(fs.readFileSync(log));fs.copyFileSync(log,folder+'/'+app.name+'-app-observation.ndjson');
 fs.copyFileSync(profile+'/host.log',folder+'/'+app.name+'-host.log');let previous=0n;
 rows.forEach((row,index)=>{assert.equal(row.schema,'soui.reference-application-observation/1');assert.equal(row.pid,String(pid));assert.equal(row.creationFileTime,ft);assert.equal(row.sequence,index+1);assert.equal(row.exitCode,0);assert(BigInt(row.qpc)>=previous);previous=BigInt(row.qpc);});
 const phases=['native.begin','application-ready','gui-native-document-bound','agent-ready-native-document-bound','gui-stop-complete','agent-stop-complete','host-close-complete','bridge-join-complete',...(app.name==='SouiReferenceModern'?[]:['runtime-shutdown-complete']),'native.end'];
 let at=-1;for(const phase of phases){const found=rows.flatMap((row,i)=>row.phase===phase?[i]:[]);assert.equal(found.length,1,phase);assert(found[0]>at,phase);at=found[0];}
 const bound=rows.find(x=>x.phase==='gui-native-document-bound'),agent=rows.find(x=>x.phase==='agent-ready-native-document-bound');
 for(const row of [bound,agent]){assert.equal(row.ownerValid,true);assert.equal(row.guiBound,true);assert.equal(row.ownerProcess,String(pid));}
 for(const key of ['sessionId','contextReceipt','sessionDigest'])assert.equal(bound[key],agent[key]);assert.equal(rows.at(-1).ownerValid,false);
 if(app.name==='SouiReferenceModern'){
  const file=profile+'/runtime-exit.ndjson',exits=lines(fs.readFileSync(file));fs.copyFileSync(file,folder+'/'+app.name+'-runtime-exit.ndjson');
  const end=exits.filter(x=>x.phase==='finished');assert.equal(end.length,1);assert.equal(end[0].pid,String(pid));assert.equal(end[0].creationFileTime,ft);assert.equal(end[0].eventObserved,true);assert.equal(end[0].processSignaled,true);assert.equal(end[0].registered,false);assert.equal(end[0].activeCallbacks,0);
 }
 return {bound,agent,phases};
}
(async()=>{
 if(mode==='outer'){
  fs.mkdirSync(folder);fs.mkdirSync(folder+'/local-app-data');check();fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,referenceAttempt,controllerAttempt,runAttempt,pins,apps,controller},null,2));
  const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
  await capture('desktop',ps,['-NoProfile','-NonInteractive','-File',lease,'-Node',process.execPath,'-Controller',__filename,'-Architecture',arch,'-ReferenceAttempt',referenceAttempt,'-ControllerAttempt',controllerAttempt,'-RunAttempt',runAttempt],root,process.env,300000);
 }else{
  for(const app of apps.apps){
   const c=await capture(app.name,controller.executable.path,[app.root+'/'+app.name+'.exe'],path.dirname(controller.executable.path),{...process.env,LOCALAPPDATA:folder+'/local-app-data',SOUI_REFERENCE_OBSERVATION:'1'},80000);
   const rows=lines(fs.readFileSync(folder+'/'+app.name+'-stdout.bin')),created=rows.filter(x=>x.event==='app-created'),complete=rows.filter(x=>x.event==='complete');
   assert.equal(created.length,1);assert.equal(complete.length,1);const child=created[0];assert.equal(child.coordinatorPid,c.ready.pid);assert.equal(child.coordinatorCreationFileTime,c.ready.creationFileTime);assert.equal(child.owned,true);
   assert.equal(complete[0].appPid,child.appPid);assert.equal(complete[0].appCreationFileTime,child.appCreationFileTime);assert.equal(complete[0].appExitCode,0);assert.equal(complete[0].finiteGuiWorkflowPassed,true);
   const owned=lines(c.rawReport).filter(x=>x.event==='owned-member'&&x.pid===child.appPid&&x.creationFileTime===child.appCreationFileTime);assert(owned.length>0,'actual app identity absent from original Owned history');
   const families=new Set(rows.filter(x=>x.event==='gui-confirmed').map(x=>x.family));assert.equal(families.size,9);
   const terminal=rows.filter(x=>x.event==='business-terminal');assert.equal(terminal.length,2);assert(terminal.some(x=>x.cancel===false&&x.native.state==='succeeded'));assert(terminal.some(x=>x.cancel===true&&x.native.state==='cancelled'));
   const observations=applicationLog(app,child.appPid,child.appCreationFileTime);fs.writeFileSync(folder+'/'+app.name+'-VERIFIED.json',JSON.stringify({app:app.name,child,families:[...families],terminal,observations,finiteGuiWorkflowPassed:true,physicalInputQualified:false,releaseReady:false},null,2),{flag:'wx'});
   console.log(JSON.stringify({app:app.name,finiteGuiWorkflowPassed:true,physicalInputQualified:false}));
  }
 }
 check();console.log(JSON.stringify({mode,arch,natural:true,releaseReady:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
