'use strict';
// SOURCE draft. A fresh Root-reviewed derivative must explicitly enable execution.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const sha=raw=>crypto.createHash('sha256').update(raw).digest('hex');
const insist=(value,message)=>{if(!value)throw Error(message);};
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const normal=file=>path.resolve(file).toLowerCase();
function pin(file){
  const stat=fs.lstatSync(file);insist(stat.isFile()&&!stat.isSymbolicLink(),'Plain file required: '+file);
  const raw=fs.readFileSync(file);return {path:file,bytes:raw.length,sha256:sha(raw)};
}
function check(expected){const actual=pin(expected.path);insist(actual.bytes===expected.bytes&&actual.sha256===expected.sha256,'Input drift: '+expected.path);}
function members(root){
  const visit=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{
    const file=path.join(dir,entry.name);insist(!entry.isSymbolicLink(),'Reparse member');
    return entry.isDirectory()?visit(file):entry.isFile()?[path.relative(root,file).replaceAll('\\','/')]:[];
  });return visit(root).sort();
}
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
function jsonLines(file){
  const raw=fs.readFileSync(file);insist(raw.length>0&&raw.at(-1)===10,'Missing or truncated raw: '+file);
  return raw.toString('utf8').trimEnd().split('\n').map(line=>JSON.parse(line));
}
function runtime(arm){
  const root=path.join(arm.suite,'runtime'),allowed='resources/source-navigation.log';
  insist(same(members(root).filter(name=>name!==allowed),arm.payload.files.map(row=>row.path)), 'Runtime membership drift');
  for(const row of arm.payload.files)check({...row,path:path.join(root,row.path)});
  const extra=path.join(root,allowed);return fs.existsSync(extra)?pin(extra):null;
}
function pair(arms){
  insist(arms.length===2&&arms[0].arm==='baseline'&&arms[1].arm==='candidate','Exact paired arms required');
  const left=arms[0],right=arms[1];
  insist(left.architecture===right.architecture&&left.sdkBuildId===right.sdkBuildId&&
    left.sourceSha256===right.sourceSha256&&left.consumerSource.sha256===right.consumerSource.sha256&&
    left.installedSdk.sha256===right.installedSdk.sha256&&left.webSdk.sha256===right.webSdk.sha256&&
    same(left.web,right.web),'A/B production source, SDK, consumer or Web differs');
  insist(same(left.payload.files.map(row=>row.path),right.payload.files.map(row=>row.path)),'A/B runtime membership differs');
  const differences=left.payload.files.filter((row,index)=>row.bytes!==right.payload.files[index].bytes||row.sha256!==right.payload.files[index].sha256).map(row=>row.path);
  insist(same(differences,['node.dll']),'Only actual kernel DLL may differ');
}
async function main(){
  insist(process.argv.length===6&&process.argv[2]==='ROOT_CURRENT_MESSAGE_PORT_AB_V1','Explicit frozen gate required');
  const inputFile=process.argv[3],expected=process.argv[4],key=process.argv[5],raw=fs.readFileSync(inputFile);
  insist(sha(raw)===expected,'Input contract changed');
  const cfg=JSON.parse(raw);
  insist(cfg.executionEnabled===true,'SOURCE draft refuses execution; freeze a fresh reviewed derivative');
  insist(cfg.schema==='soui.current-message-port-ab-inputs/v1','Input schema differs');
  const arm=cfg.arms.find(row=>row.key===key);insist(arm,'Unknown arm');
  const verify=()=>{
    insist(sha(fs.readFileSync(inputFile))===expected,'Input contract drift');cfg.pins.forEach(check);
    for(const row of cfg.arms)runtime(row);
    for(const architecture of ['x64','x86'])pair(cfg.arms.filter(row=>row.architecture===architecture));
  };
  verify();
  insist(normal(process.env.SOUI_OWNED_PROCESS_RUNNER||'')===normal(cfg.subjectRunner.path),'Actual prepared subject supervisor required');
  insist(!fs.existsSync(arm.observationOutput),'Fresh observation output required');
  const runner=require(cfg.originalRunner.path),reader=require(cfg.supplementalReader.path);
  const config=runner.settings('reload',3,0,500,20000);
  insist(same(config,cfg.protocol)&&runner.expectedSteps(config).length===9,'Original settings/steps changed');
  for(const row of cfg.arms){const build=runner.verify(row.suite);insist(build.consumerBuildId===row.consumerBuildId&&build.sourceSha256===cfg.sourceSha256,'Actual suite identity differs');}
  fs.mkdirSync(arm.observationOutput,{recursive:true});
  let answer=null,resultPath=null,failure=null,analysis=null;
  try{
    answer=await runner.run(arm.suite,config);
    // Preserve the original answer before any supplementary validation can fail.
    write(path.join(arm.observationOutput,'original-answer.json'),answer);
    insist(typeof answer.runId==='string'&&/^[0-9a-f-]{36}$/.test(answer.runId)&&typeof answer.result==='string','Original result identity absent');
    resultPath=path.resolve(cfg.sourceRoot,answer.result);
    insist(normal(resultPath)===normal(path.join(arm.suite,'reload-'+answer.runId,'result.json')),'Result is not the chosen actual suite/run');
    const result=JSON.parse(fs.readFileSync(resultPath));
    insist(answer.passed===true&&result.passed===true&&result.runId===answer.runId&&
      result.consumerBuildId===arm.consumerBuildId&&result.sourceSha256===arm.sourceSha256&&
      result.payloadSha256===arm.payload.sha256&&same(result.config,config),'Original run did not pass unchanged validation');
    const owned=result.outcome?.ownedProcess,terminal=owned?.terminal;
    insist(owned?.passed===true&&owned.exitCode===0&&owned.supervisorExitCode===0&&
      !owned.signal&&!owned.spawnError&&!owned.reportError&&!owned.stopRequested&&!owned.fallbackUsed&&
      owned.closureConfirmed===true&&terminal?.reason==='completed'&&terminal.rootExited===true&&
      terminal.rootExitCode===0&&terminal.activeBeforeCleanup===0&&terminal.activeProcesses===0&&
      terminal.terminatedOwnedJob===false&&terminal.limitTerminatedProcesses===0&&
      terminal.closureConfirmed===true&&terminal.passed===true,'Whole Native Job was not naturally closed');
    insist(result.outcome.timedOut===false&&result.outcome.processTimeoutMs===214500&&same(answer.outcome,result.outcome),'Original outcome/deadline differs');
    const dir=path.dirname(resultPath),events=path.join(dir,'events.ndjson'),ownedFile=path.join(dir,'owned-process.ndjson');
    insist(normal(owned.report)===normal(ownedFile)&&pin(ownedFile).sha256===owned.reportSha256&&
      owned.runnerSha256===cfg.subjectRunner.sha256&&owned.executableSha256===arm.executable.sha256,'Native raw image/runner binding differs');
    const ownRows=jsonLines(ownedFile),contextFile=ownedFile+'.context.json',context=JSON.parse(fs.readFileSync(contextFile));
    insist(ownRows[0].event==='start'&&ownRows[0].pid===owned.pid&&ownRows[0].creationFileTime===owned.creationFileTime&&
      same(ownRows.at(-1),terminal),'Native start/terminal identity differs');
    const args=['--mode=reload','--cycles=3','--warmup=0','--settle-ms=500','--timeout-ms=20000','--report='+events];
    insist(context.schema==='soui.owned-process-context/v1'&&normal(context.executable)===normal(arm.executable.path)&&
      normal(context.cwd)===normal(path.join(arm.suite,'runtime'))&&same(context.args,args)&&
      context.timeoutMs===394500&&context.drainMs===2000&&context.runnerSha256===cfg.subjectRunner.sha256&&
      context.executableSha256===arm.executable.sha256,'Original Native argv/context/budget differs');
    const records=jsonLines(events);insist(pin(events).sha256===result.logSha256,'Original events hash differs');
    const actualIdentity=runner.verifyNativeIdentity(records,result.outcome);
    const lifecycle=runner.validateRecords(records,config,result.outcome);
    insist(lifecycle.completedSteps===9&&lifecycle.measuredCycles===3&&lifecycle.samples.length===7,'Original finite lifecycle differs');
    const sidecar=events+'.message-port.ndjson',rows=jsonLines(sidecar);
    analysis=reader.validateMessagePortReports(rows,actualIdentity,{'1':4,'2':0,'3':0});
    insist(rows.length===40&&analysis.observedDocuments===4&&analysis.observedCases===32,'Supplementary finite document count differs');
    const navigationLog=runtime(arm);verify();
    write(path.join(arm.observationOutput,'analysis.json'),{schema:'soui.current-message-port-ab-observation/v1',key,
      originalAnswer:pin(path.join(arm.observationOutput,'original-answer.json')),originalResult:pin(resultPath),
      nativeEvents:pin(events),ownedRaw:pin(ownedFile),ownedContext:pin(contextFile),sidecar:pin(sidecar),
      stdout:pin(path.join(dir,'stdout.log')),stderr:pin(path.join(dir,'stderr.log')),actualIdentity,lifecycle,analysis,
      navigationLog,passed:true,resourcePassed:false,completeKernelToolchainQualified:false,qualificationPassed:false,releaseReady:false});
  }catch(error){
    failure=error.message;
    write(path.join(arm.observationOutput,'failure.json'),{schema:'soui.current-message-port-ab-failure/v1',key,
      originalAnswer:answer?pin(path.join(arm.observationOutput,'original-answer.json')):null,
      originalResult:resultPath&&fs.existsSync(resultPath)?pin(resultPath):null,failure,passed:false,
      resourcePassed:false,qualificationPassed:false,releaseReady:false});
  }
  try{verify();}catch(error){failure=failure?failure+'; post-input: '+error.message:error.message;write(path.join(arm.observationOutput,'post-input-failure.json'),{failure,passed:false,releaseReady:false});}
  process.stdout.write(JSON.stringify({key,result:resultPath,passed:failure===null,failure,analysis,releaseReady:false})+'\n');
  process.exitCode=failure?1:0;
}
main().catch(error=>{process.stderr.write(error.stack+'\n');process.exitCode=1;});
