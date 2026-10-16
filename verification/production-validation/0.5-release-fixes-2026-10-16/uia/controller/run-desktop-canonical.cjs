'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs'),s=require(root+'/tools/internal/standard-build.cjs');
const [mode,arch,buildAttempt,runAttempt]=process.argv.slice(2);
assert(['outer','inner'].includes(mode)&&['x64','x86'].includes(arch)&&[buildAttempt,runAttempt].every(x=>/^[a-z0-9-]+$/.test(x)));
const coreFile='C:/work/sm05-source28-independent-1/DESKTOP-REFERENCE-PREREQUISITES.json',core=JSON.parse(fs.readFileSync(coreFile));assert(core.passed&&core.withUia);e.checkPin(core.sourceManifest);
const builtFile=__dirname+'/build-'+arch+'-'+buildAttempt+'/BUILT.json',built=JSON.parse(fs.readFileSync(builtFile));assert(built.built);
const readerRoot='C:/work/sm05-screenreader-1',readerFile=readerRoot+'/CONTROLLER-PREPARED.json',reader=JSON.parse(fs.readFileSync(readerFile));
const nvda=readerRoot+'/extracted/$PLUGINSDIR/app',folder=__dirname+'/desktop-'+arch+'-'+runAttempt,config=folder+'/nvda-config/nvda.ini';
const pins=[core.sourceManifest,e.pin(coreFile),e.pin(builtFile),e.pin(readerFile),...built.pins,...built.executables,built.result,
 e.pin(__filename),e.pin(__dirname+'/desktop-lease.ps1'),e.pin(__dirname+'/nvda-canonical.ini')];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(x=>x.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Windows/System32;C:/Windows';
function check(){pins.forEach(e.checkPin);e.verifySource(built.native,__dirname+'/native');e.verifySource(built.installed,__dirname+'/sdk-'+arch);e.verifySource(reader.runtime,nvda);}
function lines(bytes){assert(bytes.length>0&&bytes.length<8*1024*1024&&bytes.toString('utf8').endsWith('\n'));return bytes.toString('utf8').trimEnd().split('\n').map(x=>JSON.parse(x));}
(async()=>{
 check();
 if(mode==='outer'){
  fs.mkdirSync(folder);fs.mkdirSync(folder+'/nvda-config');fs.copyFileSync(__dirname+'/nvda-canonical.ini',config,fs.constants.COPYFILE_EXCL);pins.push(e.pin(config));
  fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,built,readerRuntime:reader.runtime,pins,scope:'Private native UIA owner-thread synchronous command fix with diagnostic tracing',diagnosticOnly:true,installedProviderSourceOverlaid:true,physicalInputQualified:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
 }
 const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
 const host=built.executables.find(x=>path.basename(x.path)==='NativeUiaHost.exe'),client=built.executables.find(x=>path.basename(x.path)==='NativeUiaClient.exe');assert(host&&client);
 const executable=mode==='outer'?ps:client.path;
 const args=mode==='outer'?['-NoProfile','-NonInteractive','-File',__dirname+'/desktop-lease.ps1','-Node',process.execPath,'-Controller',__filename,'-Architecture',arch,'-BuildAttempt',buildAttempt,'-RunAttempt',runAttempt]:
  [host.path,folder+'/host.ndjson',nvda+'/nvda.exe',folder+'/nvda-config',folder+'/nvda.log'];
 const c=await s.capture(folder,mode,executable,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:mode==='outer'?root:path.dirname(client.path),env,timeoutMs:mode==='outer'?150000:80000,drainMs:2000});
 const v=s.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 fs.writeFileSync(folder+'/'+mode+'-RESULT.json',JSON.stringify({natural,result:c.result,terminal:v.terminal,releaseReady:false},null,2)+'\n',{flag:'wx'});
 assert(natural,'Native UIA desktop workflow failed; preserve original attempt');check();
 if(mode==='inner'){
  const rows=lines(fs.readFileSync(folder+'/inner-stdout.bin')),hostRows=lines(fs.readFileSync(folder+'/host.ndjson')),owned=lines(c.rawReport);
  function one(name){const matches=rows.filter(x=>x.event===name);assert.equal(matches.length,1,name);return matches[0];}
  const created=one('app-created'),complete=one('complete');assert.equal(created.coordinatorPid,c.ready.pid);assert.equal(created.coordinatorCreationFileTime,c.ready.creationFileTime);
  assert(created.owned&&complete.nativeProviderWorkflowPassed&&complete.actualSpeechTextPassed&&complete.appExitCode===0);
  assert.equal(complete.appPid,created.appPid);assert.equal(complete.appCreationFileTime,created.appCreationFileTime);
  assert(owned.some(x=>x.event==='owned-member'&&x.pid===created.appPid&&x.creationFileTime===created.appCreationFileTime));
  assert.equal(hostRows[0].event,'ready');assert.equal(hostRows.at(-1).event,'closed');let tick=0n;
  for(const row of hostRows){assert.equal(row.pid,created.appPid);assert.equal(row.creationFileTime,created.appCreationFileTime);assert(BigInt(row.qpc)>=tick);tick=BigInt(row.qpc);}
  assert.equal(hostRows.at(-1).receipts,2);assert.equal(hostRows.at(-1).keyboardOperations,3);assert(hostRows.at(-1).uiaOperations>=7);
  for(const [input,title,receipt]of [['uia','Native accepted',1],['keyboard','Native acceptedK',2]])assert(hostRows.some(x=>x.event==='business'&&x.method==='form.commit'&&x.input===input&&x.title===title&&x.choice==='second'&&x.receipts===receipt));
  assert(!hostRows.some(x=>x.title==='stale mutation'));
  assert.equal(one('uia-form-committed').receipt,1);assert.equal(one('keyboard-form-committed').receipt,2);
  assert([0x80040201,0x80131509].includes(one('stale-result-diagnostic').hresult),'Removed control must return a known rejection; successful or unrelated errors cannot pass');
  assert.equal(one('secret-redacted').valuePatternExposed,false);assert.equal(one('removed-control-rejected').staleMutationAccepted,false);
  assert.deepEqual(rows.filter(x=>x.event==='keyboard-sent').map(x=>[x.code,x.unicode]),[[75,true],[9,false],[13,false]]);
  const nvdaCreated=one('screenreader-created'),ready=one('screenreader-ready'),stopped=one('screenreader-stopped');
  assert(nvdaCreated.owned&&nvdaCreated.configuredSynth==='silence');assert.equal(ready.pid,nvdaCreated.pid);assert.equal(stopped.pid,nvdaCreated.pid);assert.equal(stopped.creationFileTime,nvdaCreated.creationFileTime);assert.equal(stopped.exitCode,0);
  assert(owned.some(x=>x.event==='owned-member'&&x.pid===nvdaCreated.pid&&x.creationFileTime===nvdaCreated.creationFileTime));
  const spoken=rows.filter(x=>x.event==='screenreader-spoken');assert.deepEqual(spoken.map(x=>x.expectedLabel),['Commit form','Native title','Commit form','Remove title']);
  const raw=fs.readFileSync(folder+'/nvda.log');let offset=-1,lastQpc=BigInt(ready.qpc);
  for(const row of spoken){assert.equal(row.pid,nvdaCreated.pid);assert(row.logOffset>offset&&row.logEnd>row.logOffset&&row.logEnd<raw.length);assert.equal(raw.subarray(row.logOffset,row.logEnd).toString('utf8'),row.actualLine);assert(row.actualLine.startsWith('Speaking [')&&row.actualLine.includes(row.expectedLabel));assert(BigInt(row.qpc)>lastQpc);offset=row.logEnd;lastQpc=BigInt(row.qpc);}
  assert(BigInt(stopped.qpc)>lastQpc);
  fs.writeFileSync(folder+'/VERIFIED.json',JSON.stringify({passed:true,sourceManifest:core.sourceManifest,arch,created,complete,
   hostLog:e.pin(folder+'/host.ndjson'),nvdaLog:e.pin(folder+'/nvda.log'),spoken,actualNativeProvider:true,keyboardInjection:true,
   actualSpeechText:true,diagnosticOnly:true,installedProviderSourceOverlaid:true,audibleSpeechValidated:false,referenceBusinessWorkflowQualified:false,physicalInputQualified:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
 }
 console.log(JSON.stringify({mode,arch,completed:true,actualNativeProvider:true,releaseReady:false}));
})().catch(error=>{console.error(error);process.exitCode=1;});
