'use strict';
// Internal callers hold the one DesktopValidation lease. A nonce prevents an
// accidental direct launch; it is not authentication against a writable tree.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isUtf8}=require('node:buffer');
const {isDeepStrictEqual}=require('node:util');
const id=require('./delivery-identity.cjs');
const evidence=require('./build-evidence.cjs');
const {launchOwned}=require('./owned-process.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const {validateStage}=require('./devkit-command.cjs');
const {inspectTool,verifyTool}=require('./standard-build.cjs');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const encoded=value=>JSON.stringify(value,null,2)+'\n';
function need(ok,message){if(!ok)throw new Error(message);}
function settings(args){
  need(Array.isArray(args)&&(args.length===4||(args.length===6&&args[4]==='--toolchain-request'&&path.isAbsolute(args[5])))&&args[0]==='run'&&['x86','x64'].includes(args[1])&&
    ['Debug','Release'].includes(args[2])&&typeof args[3]==='string'&&/^[A-Za-z]:[\\/]/.test(args[3])&&
    !/[\x00-\x1f<>"|?*]/.test(args[3])&&!args[3].slice(2).includes(':'),'Invalid desktop smoke arguments');
  return {architecture:args[1],configuration:args[2],toolsRoot:id.safePath(args[3]),
    ...(args.length===6?{toolchainRequest:id.safePath(args[5])}:{})};
}
function selectedProfile(value){return value.toolchainRequest?
  {nativeToolchain:'LLVM22MSVC142',profileRequest:value.toolchainRequest}:{};}
function profileSelection(root,value,measurement){
  const selected=selectedProfile(value);
  if(!value.toolchainRequest)return {selected,profile:null,measurementOptions:{}};
  const inputs=require('./llvm-profile-inputs.cjs'),toolsRoot=id.safePath(value.toolsRoot);
  const profile=inputs.readRequest(value.toolchainRequest,value.architecture,root);
  const directory=id.safePath(measurement.buildDirectory(value.architecture,toolsRoot));
  need(directory===id.safePath(toolsRoot,'out/'+(value.architecture==='x64'?'benchmark-protocol':'benchmark-protocol-x86')),
    'Smoke validation tools must own their selected architecture build directory');
  const measurementOptions={toolchainRequest:path.join(directory,'llvm-profile-request-Release.json')};
  const validationProfile=inputs.readRequest(measurementOptions.toolchainRequest,value.architecture,toolsRoot);
  inputs.sameRelocatedProfile(profile,validationProfile);
  return {selected,profile,validationProfile,measurementOptions};
}
function plans(root,metadata){
  need(Array.isArray(metadata.scenarios)&&metadata.scenarios.length>0,'Missing current smoke scenarios');
  return metadata.scenarios.map(item=>{
    need(typeof item.target==='string'&&/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(item.target)&&
      ['default','react'].includes(item.scenario)&&
      (item.scenario!=='react'||item.target==='SouiWorkbench'),'Invalid current smoke scenario');
    const cwd=id.safePath(root,`out/${metadata.architecture}/bin/${metadata.configuration}/${item.target}`);
    const args=item.target==='SouiWorkbench'?['--webview-smoke-test','--arch='+metadata.architecture]:['--smoke-test'];
    if(item.scenario==='react')args.push('--react');
    return {...item,executable:path.join(cwd,item.target+'.exe'),cwd,args,
      log:path.join(cwd,item.target==='SouiWorkbench'?'webview-smoke.log':'smoke.log'),timeoutMs:45000,drainMs:2000};
  });
}
function write(file,value){fs.writeFileSync(file,encoded(value),{flag:'wx'});}
function relative(root,file){return path.relative(root,file).replaceAll('\\','/');}
function attemptFile(root,attempt,relativePath){
  need(typeof relativePath==='string'&&relativePath.startsWith(attempt+'/'),'Smoke evidence is outside this attempt');
  return id.safePath(root,relativePath);
}
function appReceipt(root,report,run,plan,build){
  const native=run.ownedSupervision;
  need(native?.schema==='soui.native-smoke-app-owned/v1','Missing actual application supervision');
  const raw=fs.readFileSync(attemptFile(root,report.ownedSupervision.attempt,native.report));
  const contextBytes=fs.readFileSync(attemptFile(root,report.ownedSupervision.attempt,native.context));
  const finishedBytes=fs.readFileSync(attemptFile(root,report.ownedSupervision.attempt,native.finished));
  need(hash(finishedBytes)===native.finishedSha256,'Application finished receipt changed');
  const finished=parseStrictJson(finishedBytes.toString('utf8'));
  const payload=build.payloads[plan.target],subject=payload?.files?.find(file=>file.path===plan.target+'.exe');
  need(subject&&native.expected.executableSha256===subject.sha256&&
    native.expected.executable===plan.executable&&native.expected.cwd===plan.cwd&&
    isDeepStrictEqual(native.expected.args,plan.args)&&native.expected.timeoutMs===45000&&native.expected.drainMs===2000&&
    native.expected.runnerSha256===report.ownedSupervision.runner.sha256,'Application launch differs from current build/scenario');
  const identity=validateStage(raw,contextBytes,native.ready,finished,native.expected);
  need(run.pid===identity.pid&&run.creationFileTime===identity.creationFileTime&&run.exitCode===finished.exitCode&&
    run.timedOut===false&&run.passed===true,'Application identity or terminal outcome differs');
  const logBytes=fs.readFileSync(attemptFile(root,report.ownedSupervision.attempt,native.log));
  need(logBytes.length<=1048576&&isUtf8(logBytes)&&hash(logBytes)===native.logSha256&&
    logBytes.toString('utf8')===run.log&&run.log.includes('application-ready')&&run.log.includes('host-close-complete'),
    'Application ready/close log differs from actual fresh bytes');
}
function validateReport(root,report,build,selected={}){
  const extension=report.ownedSupervision;
  need(extension?.schema==='soui.native-smoke-owned/v1'&&
    /^out\/(x86|x64)\/smoke-owned-[0-9a-f-]{36}$/.test(extension.attempt)&&
    extension.attempt.startsWith('out/'+report.architecture+'/')&&
    /^[a-f0-9]{64}$/.test(extension.runner?.sha256||''),'Invalid smoke Owned extension');
  const inputs=fs.readFileSync(attemptFile(root,extension.attempt,extension.inputs));
  need(hash(inputs)===extension.inputsSha256,'Smoke input receipt changed');
  const input=parseStrictJson(inputs.toString('utf8'));
  need(input.schema==='soui.native-smoke-inputs/v1'&&input.buildId===build.buildId&&input.sourceSha256===build.source.sha256&&
    input.architecture===report.architecture&&input.configuration===report.configuration&&
    isDeepStrictEqual(input.runner,extension.runner),'Smoke inputs identify another build or runner');
  if(selected.nativeToolchain==='LLVM22MSVC142'){
    const profile=require('./llvm-profile-inputs.cjs').readRequest(selected.profileRequest,report.architecture,root);
    const expected={profile:'LLVM22MSVC142',requestFile:profile.pin,inputIdentity:profile.inputIdentity,bridgeIdentity:profile.bridgeIdentity};
    need(isDeepStrictEqual(input.nativeToolchain,expected)&&isDeepStrictEqual(report.nativeToolchain,expected),
      'Smoke profile differs from the current selected Build request');
    const stockPin=input.preparedTools?.nativeToolchain?.requestFile;
    need(typeof stockPin?.path==='string'&&path.isAbsolute(stockPin.path),'Missing measurement-owned smoke request receipt');
    const toolsRoot=id.safePath(input.preparedToolsOwner);
    const actual=require('./llvm-build-observer.cjs').verifyStandardBuild(root,report.architecture,report.configuration,false,selected);
    need(toolsRoot===id.safePath(actual.accepted.validationTools.root),'Smoke stock owner differs from the accepted actual Build');
    const measurement=require('./measurement-validation.cjs');
    const choice=profileSelection(root,{architecture:report.architecture,toolchainRequest:selected.profileRequest,toolsRoot},measurement);
    const setup=measurement.preparedAt(toolsRoot,report.architecture,choice.measurementOptions);
    need(isDeepStrictEqual(input.preparedTools,setup.report)&&
      isDeepStrictEqual(extension.runner,inspectTool(setup.tools.OwnedProcessRunner)),
      'Smoke prepared validation tools differ from their current owned request or runner');
  }else need(input.nativeToolchain===undefined&&report.nativeToolchain===undefined,'LLVM smoke requires an explicit profile selection');
  const metadata={...report,scenarios:evidence.targets(root).flatMap(target=>target==='SouiWorkbench'?
    [{target,scenario:'default'},{target,scenario:'react'}]:[{target,scenario:'default'}])};
  const expected=plans(root,metadata);
  need(report.runs.length===expected.length&&input.plans.length===expected.length,'Smoke scenario count differs');
  const runnerBytes=fs.readFileSync(id.safePath(extension.runner.path));
  need(runnerBytes.length===extension.runner.bytes&&hash(runnerBytes)===extension.runner.sha256,'Prepared runner bytes changed');
  for(let i=0;i<expected.length;i++){
    const run=report.runs[i],plan=expected[i];
    need(run.target===plan.target&&run.scenario===plan.scenario&&isDeepStrictEqual(input.plans[i],plan),'Smoke order or planned argv differs');
    appReceipt(root,report,run,plan,build);
  }
  return {applications:expected.length,applicationNaturalClosure:true};
}
async function runSmoke(root,value){
  root=id.safePath(root);
  need(process.platform==='win32','Desktop smoke requires Windows');
  const measurement=require('./measurement-validation.cjs');
  const {selected,profile,measurementOptions}=profileSelection(root,value,measurement);
  const nativeToolchain=profile?{profile:'LLVM22MSVC142',requestFile:profile.pin,inputIdentity:profile.inputIdentity,bridgeIdentity:profile.bridgeIdentity}:null;
  const prepare=()=>measurement.preparedAt(id.safePath(value.toolsRoot),value.architecture,measurementOptions);
  if(profile){const actual=require('./llvm-build-observer.cjs').verifyStandardBuild(root,value.architecture,value.configuration,false,selected);
    need(id.safePath(value.toolsRoot)===id.safePath(actual.accepted.validationTools.root),'Smoke stock owner differs from the accepted actual Build');}
  const setup=prepare(),runner=inspectTool(setup.tools.OwnedProcessRunner);
  const output=id.safePath(root,'out/'+value.architecture);
  const folder=id.safePath(output,'smoke-owned-'+crypto.randomUUID());fs.mkdirSync(folder);
  const old=path.join(output,'smoke-'+value.configuration+'.json');
  if(fs.existsSync(old))fs.copyFileSync(old,path.join(folder,'previous-smoke.json'),fs.constants.COPYFILE_EXCL);
  let metadata,report=null,active=null,interrupted=false;
  const stop=()=>{interrupted=true;active?.stop();};
  process.on('SIGINT',stop);process.on('SIGTERM',stop);
  try{
    metadata=evidence.beginSmoke(root,value.architecture,value.configuration,selected);
    const invocations=plans(root,metadata),inputPath=path.join(folder,'inputs.json');
    const input={schema:'soui.native-smoke-inputs/v1',buildId:metadata.buildId,sourceSha256:metadata.sourceSha256,
      architecture:value.architecture,configuration:value.configuration,runner,plans:invocations,
      preparedTools:setup.report,...(nativeToolchain?{nativeToolchain,preparedToolsOwner:id.safePath(value.toolsRoot)}:{}),releaseReady:false};write(inputPath,input);
    report={schema:metadata.schema,buildId:metadata.buildId,sourceSha256:metadata.sourceSha256,version:metadata.version,
      startedAt:metadata.startedAt,architecture:value.architecture,configuration:value.configuration,
      ...(nativeToolchain?{nativeToolchain}:{}),releaseReady:false,runs:[],passed:false,
      ownedSupervision:{schema:'soui.native-smoke-owned/v1',attempt:relative(root,folder),runner,
        inputs:relative(root,inputPath),inputsSha256:hash(fs.readFileSync(inputPath)),
        controllerClosure:'external-caller-evidence-required'}};
    for(const plan of invocations){
      if(interrupted)break;
      const stage=path.join(folder,plan.target+'-'+plan.scenario);fs.mkdirSync(stage);
      const nativePath=path.join(stage,'owned.ndjson'),stdout=fs.openSync(path.join(stage,'stdout.raw'),'wx'),stderr=fs.openSync(path.join(stage,'stderr.raw'),'wx');
      const run={target:plan.target,scenario:plan.scenario,passed:false,exitCode:null,timedOut:false,log:''};
      report.runs.push(run);
      let finished=null,ready=null,expected=null;
      try{
        if(fs.existsSync(plan.log)){
          id.safePath(root,relative(root,plan.log));
          fs.copyFileSync(plan.log,path.join(stage,'previous-app.log'),fs.constants.COPYFILE_EXCL);
          fs.unlinkSync(plan.log); // Exact owned application diagnostic only; original bytes retained above.
        }
        verifyTool(runner);const subject=inspectTool(plan.executable);
        expected={...plan,executableSha256:subject.sha256,runnerSha256:runner.sha256};
        active=launchOwned(plan.executable,plan.args,{runner:runner.path,cwd:plan.cwd,report:nativePath,
          timeoutMs:45000,drainMs:2000,stdout,stderr});
        expected.contextSha256=hash(fs.readFileSync(nativePath+'.context.json'));
        try{ready=await active.ready;}catch{/* Keep the actual finished and raw failure. */}
        if(interrupted)active.stop();
        finished=await active.finished;write(path.join(stage,'finished.json'),finished);
        run.exitCode=finished.exitCode;run.timedOut=finished.terminal?.reason==='timeout';
        run.pid=finished.pid;run.creationFileTime=finished.creationFileTime;
        const bytes=fs.existsSync(plan.log)?fs.readFileSync(plan.log):Buffer.alloc(0);
        fs.writeFileSync(path.join(stage,'app.log'),bytes,{flag:'wx'});
        need(bytes.length<=1048576&&isUtf8(bytes),'App diagnostic is excessive or not UTF-8');run.log=bytes.toString('utf8');
        run.ownedSupervision={schema:'soui.native-smoke-app-owned/v1',ready,expected,
          report:relative(root,nativePath),context:relative(root,nativePath+'.context.json'),finished:relative(root,path.join(stage,'finished.json')),
          finishedSha256:hash(fs.readFileSync(path.join(stage,'finished.json'))),log:relative(root,path.join(stage,'app.log')),logSha256:hash(bytes)};
        validateStage(fs.readFileSync(nativePath),fs.readFileSync(nativePath+'.context.json'),ready,finished,expected);
        verifyTool(subject);verifyTool(runner);prepare();
        need(!interrupted&&run.log.includes('application-ready')&&run.log.includes('host-close-complete'),'Actual application did not complete ready/close');
        evidence.verifyBuild(root,value.architecture,value.configuration,false,selected);run.passed=true;
      }catch(error){run.failure=error.message;}
      finally{
        if(active&&!finished){active.stop();finished=await active.finished;write(path.join(stage,'finished.json'),finished);}
        active=null;fs.closeSync(stdout);fs.closeSync(stderr);
      }
      fs.writeFileSync(old,encoded(report)); // Invalidate/record every attempt before progressing.
    }
    need(!interrupted,'Desktop smoke interrupted; closure receipts do not convert cancellation into success');
    verifyTool(runner);prepare();
    evidence.finishSmoke(root,value.architecture,value.configuration,selected);
    report=id.readJson(old);write(path.join(folder,'smoke.json'),report);
    return {schema:'soui.desktop-smoke-controller/v1',passed:true,smokeReport:relative(root,old),attempt:relative(root,folder),
      applicationNaturalClosure:true,controllerNaturalClosure:'external-caller-evidence-required',releaseReady:false};
  }catch(error){
    if(report){report.passed=false;report.failure=error.message;fs.writeFileSync(old,encoded(report));write(path.join(folder,'smoke.json'),report);}
    const failure={schema:'soui.desktop-smoke-controller/v1',passed:false,failure:error.message,
      smokeReport:relative(root,old),attempt:relative(root,folder),controllerNaturalClosure:'external-caller-evidence-required',releaseReady:false};
    write(path.join(folder,'failure.json'),failure);return failure;
  }finally{process.off('SIGINT',stop);process.off('SIGTERM',stop);}
}
module.exports={settings,profileSelection,plans,validateReport,runSmoke};
if(require.main===module)(async()=>{
  need([process.env.SOUI_DEVKIT_ENTRY_NONCE,process.env.SOUI_TEST_DESKTOP_NONCE].some(value=>/^[a-f0-9]{32}$/.test(value||'')),
    'Use the actual lease-holding Test.ps1 or Soui.ps1 entry');
  const result=await runSmoke(path.resolve(__dirname,'../..'),settings(process.argv.slice(2)));
  console.log(encoded(result));if(!result.passed)process.exitCode=1;
})().catch(error=>{console.error(error.message);process.exitCode=1;});
