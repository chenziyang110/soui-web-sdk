'use strict';
// Test-Auto.ps1 holds the desktop lease. Every successful report is backed by
// current build bytes, native Owned records and the actual application log.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util'),{isUtf8}=require('node:buffer');
const id=require('./delivery-identity.cjs'),evidence=require('./build-evidence.cjs');
const {launchOwned}=require('./owned-process.cjs');
const {validateStage}=require('./devkit-command.cjs');
const {inspectTool,verifyTool}=require('./standard-build.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const need=(ok,message)=>{if(!ok)throw new Error(message);};
const write=(p,v)=>fs.writeFileSync(p,id.encoded(v),{flag:'wx'});
const rel=(root,p)=>path.relative(root,p).replaceAll('\\','/');
function absolute(p){need(typeof p==='string'&&/^[A-Za-z]:[\\/]/.test(p),'Absolute Windows path required');return id.safePath(p);}
function settings(args){
  need(args.length===4&&args[0]==='run'&&['x86','x64'].includes(args[1])&&['Debug','Release'].includes(args[2]),
    'Usage: auto-smoke.cjs run x86|x64 Debug|Release ABSOLUTE_DEVKIT_ROOT');
  return {architecture:args[1],configuration:args[2],toolsRoot:absolute(args[3])};
}
function planFor(root,build){
  const generated=fs.existsSync(id.safePath(root,'project.json'));
  const project=generated?id.readJson(id.safePath(root,'project.json')):null;
  need(!generated||project.runtimeProfile==='auto','Generated project is not auto');
  const target=generated?project.nativeTarget:'SouiAutoStarter';
  need(typeof target==='string'&&/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(target),'Invalid auto target');
  const payload=generated?build.payloads?.[target]:build.optionalPayloads?.[target];
  need(payload&&build.optionalLibraries?.webview2,'Build lacks the real auto/WebView2 payload');
  const exe=payload.files?.filter(f=>f.path===target+'.exe');need(exe?.length===1,'Missing auto executable');
  const cwd=id.safePath(root,`out/${build.architecture}/bin/${build.configuration}/${target}`);
  return {target,payload,cwd,executable:path.join(cwd,target+'.exe'),subject:exe[0]};
}
function lifecycle(log,subject,backend){
  need(typeof log==='string'&&log.endsWith('\n'),'Truncated auto application log');
  const rows=log.replaceAll('\r\n','\n').trimEnd().split('\n');
  need(rows[0]===`process-start;pid=${subject.pid};creationFileTime=${subject.creationFileTime}`,
    'Auto log does not identify the actual process');
  const chosen=rows.filter(r=>r.startsWith('backend-selected;'));
  need(chosen.length===1&&chosen[0].startsWith(`backend-selected;backend=${backend};reason=`),'Wrong or duplicate selected backend');
  const probes=rows.filter(r=>r.startsWith('runtime-discovery;'));
  need(probes.length===1&&(backend==='webview2'?
    /^runtime-discovery;attempted=true;available=true;hresult=0;version=[^\r\n]+$/.test(probes[0]):
    probes[0]==='runtime-discovery;attempted=true;available=false;hresult=-2147024894;version='),
    'Missing actual Runtime discovery result for the selected scenario');
  const start=rows.findIndex(r=>r.startsWith('host-started;runtime='));
  const actions=rows.filter(r=>/^action-roundtrip;method=app\.greet;calls=[1-9]\d*$/.test(r));
  need(actions.length===1,'Missing or duplicate native action roundtrip');
  const action=rows.indexOf(actions[0]),ready=rows.indexOf('application-ready'),close=rows.indexOf('host-close-complete');
  need(start>rows.indexOf(chosen[0])&&action>start&&ready>action&&close>ready&&close===rows.length-2&&
    rows.at(-1)==='process-complete;exit=0',
    'Auto log lacks ordered actual backend/start/action/ready/close');
  for(const event of ['application-ready','host-close-complete'])
    need(rows.filter(r=>r===event).length===1,'Duplicate auto lifecycle event');
  need(!rows.some(r=>r.startsWith('error;')||/^E_[A-Z0-9_]+(?::|$)/.test(r)),'Application reported an error');
  return {backend,reason:chosen[0].slice(chosen[0].indexOf(';reason=')+8)};
}
function prepared(toolsRoot,architecture){
  const helper=inspectTool(id.safePath(toolsRoot,'tools/internal/measurement-validation.cjs'));
  const setup=require(helper.path).prepared(architecture);
  return {helper,report:setup.report,runner:inspectTool(setup.tools.OwnedProcessRunner)};
}
function pinned(file){const bytes=fs.readFileSync(file);return {path:file,bytes:bytes.length,sha256:hash(bytes)};}
function readPinned(root,attempt,pin){
  need(pin&&typeof pin.path==='string'&&pin.path.startsWith(attempt+'/'),'Auto record escapes its attempt');
  const file=id.safePath(root,pin.path),stat=fs.statSync(file);
  need(stat.isFile()&&stat.size<=16777216&&stat.size===pin.bytes,'Invalid auto evidence file');
  const bytes=fs.readFileSync(file);need(hash(bytes)===pin.sha256,'Auto evidence bytes changed');return bytes;
}
function validateReport(root,report,build,payload,selected={},toolsRoot=root){
  need(!selected.nativeToolchain||selected.nativeToolchain==='MSVC143','Auto smoke currently requires MSVC143');
  need(report?.schema==='soui.auto-smoke/v1'&&report.passed===true&&report.releaseReady===false&&report.finishedAt&&
    report.version===build.version&&report.buildId===build.buildId&&report.sourceSha256===build.source.sha256&&
    report.architecture===build.architecture&&report.configuration===build.configuration,
    'Auto smoke differs from the current build');
  const plan=planFor(root,build);
  need(report.target===plan.target&&isDeepStrictEqual(report.payload,payload)&&isDeepStrictEqual(payload,plan.payload),
    'Auto payload identity changed');
  need(typeof report.attempt==='string'&&new RegExp(`^out/${build.architecture}/auto-owned-[0-9a-f-]{36}$`).test(report.attempt)&&
    report.toolsRoot===absolute(toolsRoot),'Invalid auto attempt/tools scope');
  const input=parseStrictJson(readPinned(root,report.attempt,report.inputs).toString('utf8'));
  need(input.schema==='soui.auto-smoke-inputs/v1'&&input.buildId===build.buildId&&
    input.sourceSha256===build.source.sha256&&isDeepStrictEqual(input.plan,plan),'Auto inputs differ from build');
  const original=readPinned(root,report.attempt,report.buildBefore),after=readPinned(root,report.attempt,report.buildAfter);
  const current=fs.readFileSync(id.safePath(root,`out/${build.architecture}/build-${build.configuration}.json`));
  need(original.equals(after)&&after.equals(current)&&isDeepStrictEqual(parseStrictJson(current.toString('utf8')),build),
    'Auto Build changed during verification');
  const setup=prepared(report.toolsRoot,report.architecture);
  need(isDeepStrictEqual(input.setup,setup),'Prepared auto tools changed');
  need(Array.isArray(report.runs)&&report.runs.length===2,'Both real auto backend runs are required');
  const backends=['webview2','miniblink'];
  for(let i=0;i<2;i++){
    const run=report.runs[i],backend=backends[i];
    need(run.backend===backend&&run.passed===true,'Missing real preferred/fallback startup');
    const runRoot=id.safePath(root,report.attempt+'/'+backend);
    const logFile=path.join(runRoot,'application.log'),data=path.join(runRoot,'profile');
    const args=['--smoke-test','--smoke-log='+logFile,'--user-data='+data];
    if(i===1)args.push('--webview2-runtime='+path.join(runRoot,'empty-runtime'));
    need(isDeepStrictEqual(run.expected.args,args)&&run.expected.executable===plan.executable&&run.expected.cwd===plan.cwd&&
      run.expected.timeoutMs===45000&&run.expected.drainMs===2000&&run.expected.executableSha256===plan.subject.sha256&&
      run.expected.runnerSha256===setup.runner.sha256,'Auto actual command differs from fixed scenario');
    const finished=parseStrictJson(readPinned(root,report.attempt,run.finished).toString('utf8'));
    const subject=validateStage(readPinned(root,report.attempt,run.raw),readPinned(root,report.attempt,run.context),run.ready,finished,run.expected);
    const log=readPinned(root,report.attempt,run.log);need(isUtf8(log),'Invalid auto log encoding');
    lifecycle(log.toString('utf8'),subject,backend);
    need(run.log.path===rel(root,logFile),'Auto log has another source');
    if(i===1){const missing=path.join(runRoot,'empty-runtime');
      need(fs.statSync(id.safePath(missing)).isDirectory()&&fs.readdirSync(missing).length===0,'Runtime discovery scope is no longer empty');}
  }
  return {passed:true,backends,scope:'actual local startup plus empty Fixed Runtime search; not whole-machine uninstallation',releaseReady:false};
}
async function runSmoke(root,value){
  root=absolute(root);need(process.platform==='win32','Auto smoke requires Windows');
  const output=id.safePath(root,'out/'+value.architecture);fs.mkdirSync(output,{recursive:true});
  const folder=id.safePath(output,'auto-owned-'+crypto.randomUUID());fs.mkdirSync(folder);
  const reportFile=path.join(output,'auto-smoke-'+value.configuration+'.json');
  if(fs.existsSync(reportFile))fs.copyFileSync(reportFile,path.join(folder,'previous.json'),fs.constants.COPYFILE_EXCL);
  let report={schema:'soui.auto-smoke/v1',passed:false,releaseReady:false},active=null,finished=null,interrupted=false;
  fs.writeFileSync(reportFile,id.encoded(report));
  const stop=()=>{interrupted=true;active?.stop();};process.on('SIGINT',stop);process.on('SIGTERM',stop);
  const record=p=>{const pin=pinned(p);return {...pin,path:rel(root,p)};};
  try{
    const selected={nativeToolchain:'MSVC143'};
    const {build}=evidence.verifyAcceptedBuild(root,value.architecture,value.configuration,false,selected);
    const plan=planFor(root,build),setup=prepared(value.toolsRoot,value.architecture);
    const before=path.join(folder,'before-build.json');
    fs.copyFileSync(id.safePath(root,`out/${value.architecture}/build-${value.configuration}.json`),before,fs.constants.COPYFILE_EXCL);
    const inputFile=path.join(folder,'inputs.json');
    write(inputFile,{schema:'soui.auto-smoke-inputs/v1',buildId:build.buildId,sourceSha256:build.source.sha256,plan,setup});
    report={...report,version:build.version,buildId:build.buildId,sourceSha256:build.source.sha256,
      architecture:value.architecture,configuration:value.configuration,target:plan.target,payload:plan.payload,
      toolsRoot:absolute(value.toolsRoot),attempt:rel(root,folder),inputs:record(inputFile),buildBefore:record(before),runs:[]};
    for(const backend of ['webview2','miniblink']){
      need(!interrupted,'Auto smoke interrupted');
      const dir=path.join(folder,backend);fs.mkdirSync(dir);
      const log=path.join(dir,'application.log'),args=['--smoke-test','--smoke-log='+log,'--user-data='+path.join(dir,'profile')];
      if(backend==='miniblink'){const missing=path.join(dir,'empty-runtime');fs.mkdirSync(missing);args.push('--webview2-runtime='+missing);}
      const raw=path.join(dir,'owned.ndjson'),out=fs.openSync(path.join(dir,'stdout.raw'),'wx'),err=fs.openSync(path.join(dir,'stderr.raw'),'wx');
      const subject=inspectTool(plan.executable);need(subject.sha256===plan.subject.sha256,'Auto executable changed');
      const expected={executable:plan.executable,args,cwd:plan.cwd,timeoutMs:45000,drainMs:2000,
        executableSha256:subject.sha256,runnerSha256:setup.runner.sha256};
      let ready=null;finished=null;
      try{
        active=launchOwned(plan.executable,args,{runner:setup.runner.path,cwd:plan.cwd,report:raw,timeoutMs:45000,drainMs:2000,stdout:out,stderr:err});
        expected.contextSha256=hash(fs.readFileSync(raw+'.context.json'));
        try{ready=await active.ready;}catch{/* Actual raw/finished failure stays retained. */}
        if(interrupted)active.stop();finished=await active.finished;write(path.join(dir,'finished.json'),finished);
        const identity=validateStage(fs.readFileSync(raw),fs.readFileSync(raw+'.context.json'),ready,finished,expected);
        lifecycle(fs.readFileSync(log,'utf8'),identity,backend);verifyTool(subject);verifyTool(setup.runner);
        report.runs.push({backend,passed:true,expected,ready,raw:record(raw),context:record(raw+'.context.json'),
          finished:record(path.join(dir,'finished.json')),log:record(log)});
      }finally{
        if(active&&!finished){active.stop();finished=await active.finished;write(path.join(dir,'finished.json'),finished);}
        fs.closeSync(out);fs.closeSync(err);active=null;
      }
    }
    const {build:current}=evidence.verifyAcceptedBuild(root,value.architecture,value.configuration,false,selected);
    need(isDeepStrictEqual(current,build),'Auto Build changed during execution');
    const after=path.join(folder,'after-build.json');fs.copyFileSync(id.safePath(root,`out/${value.architecture}/build-${value.configuration}.json`),after,fs.constants.COPYFILE_EXCL);
    report.buildAfter=record(after);report.finishedAt=new Date().toISOString();report.passed=true;
    validateReport(root,report,current,plan.payload,selected,value.toolsRoot);
    need(!interrupted,'Auto smoke interrupted during final validation');
  }catch(error){report.passed=false;report.failure=error.message;}
  finally{process.off('SIGINT',stop);process.off('SIGTERM',stop);}
  fs.writeFileSync(reportFile,id.encoded(report));write(path.join(folder,'result.json'),report);
  return {passed:report.passed,report:rel(root,reportFile),attempt:rel(root,folder),failure:report.failure,releaseReady:false};
}
module.exports={settings,planFor,lifecycle,validateReport,runSmoke};
if(require.main===module)(async()=>{
  need(/^[a-f0-9]{32}$/.test(process.env.SOUI_TEST_AUTO_NONCE||''),'Use the lease-holding Test-Auto.ps1 entry');
  const result=await runSmoke(path.resolve(__dirname,'../..'),settings(process.argv.slice(2)));
  console.log(id.encoded(result));if(!result.passed)process.exitCode=1;
})().catch(error=>{console.error(error.message);process.exitCode=1;});
