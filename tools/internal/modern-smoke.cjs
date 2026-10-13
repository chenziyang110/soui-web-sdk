'use strict';
// The PS entry holds DesktopValidation. The nonce is an accidental-entry guard,
// not authentication against an editable source tree.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isUtf8}=require('node:buffer'),{isDeepStrictEqual}=require('node:util');
const id=require('./delivery-identity.cjs'),evidence=require('./build-evidence.cjs');
const {launchOwned}=require('./owned-process.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const {validateStage}=require('./devkit-command.cjs');
const {inspectTool,verifyTool}=require('./standard-build.cjs');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const encoded=v=>JSON.stringify(v,null,2)+'\n';
const epoch=116444736000000000n;
const timeNow=()=>((BigInt(Date.now())+1n)*10000n+epoch).toString();
const fileTime=ns=>(ns/100n+epoch).toString();
function need(ok,message){if(!ok)throw new Error(message);}
function write(file,value){fs.writeFileSync(file,encoded(value),{flag:'wx'});}
function relative(root,file){return path.relative(root,file).replaceAll('\\','/');}
const definitions={'--llvm-root':'SOUI_LLVM_ROOT','--msvc-root':'SOUI_MSVC142_ROOT',
  '--windows-sdk-root':'SOUI_WINDOWS_SDK_ROOT','--ninja-exe':'SOUI_NINJA_EXECUTABLE',
  '--windows-cmd-exe':'SOUI_WINDOWS_CMD_EXECUTABLE','--powershell-exe':'SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE'};
function settings(args){
  need(Array.isArray(args)&&args.length>=7&&args[0]==='run'&&['x86','x64'].includes(args[1])&&
    ['Debug','Release'].includes(args[2]),'Invalid Modern smoke arguments');
  const value={architecture:args[1],configuration:args[2],toolsRoot:absolute(args[3]),localAppData:absolute(args[4])},flags={};
  for(let i=5;i<args.length;i+=2){
    const key=args[i];need(i+1<args.length&&!Object.hasOwn(flags,key)&&
      ['--native-toolchain','--toolchain-request',...Object.keys(definitions)].includes(key),'Unknown, duplicate or incomplete Modern flag');
    flags[key]=args[i+1];
  }
  value.nativeToolchain=flags['--native-toolchain'];
  need(['MSVC143','LLVM22MSVC142'].includes(value.nativeToolchain),'Explicit native toolchain is required');
  if(value.nativeToolchain==='MSVC143')need(Object.keys(flags).length===1,'LLVM inputs require LLVM22MSVC142');
  else{
    need(Object.keys(flags).length===8,'LLVM Modern smoke requires request and all six explicit paths');
    value.toolchainRequest=absolute(flags['--toolchain-request']);
    value.externalInputs=Object.fromEntries(Object.keys(definitions).map(key=>[definitions[key],absolute(flags[key]).replaceAll('\\','/').replace(/\/$/,'')]));
  }
  return value;
}
function absolute(value){
  need(typeof value==='string'&&/^[A-Za-z]:[\\/]/.test(value)&&!/[<>\"|?*\x00-\x1f]/.test(value)&&!value.slice(2).includes(':'),'Absolute ordinary Windows path required');
  return id.safePath(value);
}
function selection(value){return value.nativeToolchain==='LLVM22MSVC142'?
  {nativeToolchain:value.nativeToolchain,profileRequest:value.toolchainRequest}:{nativeToolchain:'MSVC143'};}
function profileFor(root,value){
  if(value.nativeToolchain!=='LLVM22MSVC142')return null;
  need(value.externalInputs&&isDeepStrictEqual(Object.keys(value.externalInputs).sort(),Object.values(definitions).sort()),
    'LLVM selection must bind the exact six external definition names');
  const profile=require('./llvm-profile-inputs.cjs').readRequest(value.toolchainRequest,value.architecture,root);
  for(const [key,input] of Object.entries(value.externalInputs))
    need(profile.value.discovery.CMakeDefinitions[key]===input,'Explicit LLVM path differs from existing Build request: '+key);
  return {profile:'LLVM22MSVC142',requestFile:profile.pin,inputIdentity:profile.inputIdentity,bridgeIdentity:profile.bridgeIdentity};
}
function buildReceiptPath(root,architecture,configuration){
  return id.safePath(root,'out/'+architecture+'/build-'+configuration+'.json');
}
function assertBuildUnchanged(before,current,beforePin,currentPin){
  need(before&&current&&beforePin&&currentPin&&isDeepStrictEqual(before,current)&&isDeepStrictEqual(beforePin,currentPin),
    'Accepted Modern Build changed during smoke; original receipt/identity must remain exact');
}
function captureBuildReceipt(root,value,build){
  const file=buildReceiptPath(root,value.architecture,value.configuration);
  need(fs.statSync(file).size<=16777216,'Build receipt exceeds the finite bound');
  const pin=inspectTool(file),bytes=fs.readFileSync(file);
  need(bytes.length===pin.bytes&&hash(bytes)===pin.sha256&&isUtf8(bytes)&&
    isDeepStrictEqual(parseStrictJson(bytes.toString('utf8')),build),'Verified Build and captured raw receipt differ');
  verifyTool(pin);return {pin,bytes};
}
function prepared(value){
  // toolsRoot is an explicit caller path. Never load code from a receipt-selected path.
  const helper=inspectTool(id.safePath(value.toolsRoot,'tools/internal/measurement-validation.cjs'));
  const result=require(helper.path).prepared(value.architecture,value.toolchainRequest?{toolchainRequest:value.toolchainRequest}:{});
  return {...result,helper,runner:inspectTool(result.tools.OwnedProcessRunner)};
}
function planFor(root,build,value){
  const generated=fs.existsSync(id.safePath(root,'project.json'));
  const project=generated?parseStrictJson(fs.readFileSync(id.safePath(root,'project.json'),'utf8')):null;
  need(!generated||project.runtimeProfile==='modern','Generated project is not Modern');
  const target=generated?project.nativeTarget:'SouiModernStarter';
  need(typeof target==='string'&&/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(target),'Invalid Modern target');
  const payload=generated?build.payloads?.[target]:build.optionalPayloads?.[target];
  need(build.optionalLibraries?.webview2&&payload,'Current Build must contain the real WebView2 payload');
  const subject=payload.files?.filter(file=>file.path===target+'.exe');
  need(subject?.length===1,'Missing or ambiguous Modern executable payload');
  const cwd=id.safePath(root,'out/'+build.architecture+'/bin/'+build.configuration+'/'+target);
  return {target,payload,subject:subject[0],executable:path.join(cwd,target+'.exe'),cwd,args:['--smoke-test'],
    logRoot:id.safePath(value.localAppData,target),timeoutMs:40000,drainMs:2000};
}
function snapshot(logRoot){
  if(!fs.existsSync(logRoot))return {logRoot,existed:false,names:[]};
  need(fs.statSync(logRoot).isDirectory(),'Modern profile root is not a directory');
  const names=fs.readdirSync(logRoot).filter(name=>/^smoke-[1-9]\d*$/.test(name)).sort();
  need(names.length<=8192,'Modern profile directory inventory exceeds the finite bound');
  return {logRoot,existed:true,names};
}
function statValue(s){
  return {kind:s.isFile()?'file':s.isDirectory()?'directory':'other',
    dev:s.dev.toString(),ino:s.ino.toString(),bytes:s.size.toString(),
    creationFileTime:fileTime(s.birthtimeNs),mtimeFileTime:fileTime(s.mtimeNs)};
}
function ordinaryStat(file){absolute(file);return statValue(fs.statSync(file,{bigint:true}));}
function actualLog(plan,before,identity){
  const name='smoke-'+identity.pid;
  need(!before.names.includes(name),'Modern PID profile already existed before launch; old bytes are preserved');
  const directory=id.safePath(plan.logRoot,name),file=id.safePath(directory,'smoke.log');
  const dirBefore=ordinaryStat(directory),statBefore=ordinaryStat(file);
  need(dirBefore.kind==='directory'&&statBefore.kind==='file'&&BigInt(statBefore.bytes)<=1048576n,'Invalid or excessive actual Modern log');
  const handle=fs.openSync(file,'r');let bytes,statAfter;
  try{
    need(isDeepStrictEqual(statValue(fs.fstatSync(handle,{bigint:true})),statBefore),'Modern log opened another file identity');
    bytes=fs.readFileSync(handle);statAfter=statValue(fs.fstatSync(handle,{bigint:true}));
  }finally{fs.closeSync(handle);}
  const dirAfter=ordinaryStat(directory),upper=timeNow();
  need(isDeepStrictEqual(statBefore,statAfter)&&isDeepStrictEqual(statAfter,ordinaryStat(file))&&
    isDeepStrictEqual(dirBefore,dirAfter)&&BigInt(bytes.length)===BigInt(statAfter.bytes)&&isUtf8(bytes),
    'Modern log path/held identity/bytes changed during capture');
  for(const stamp of [dirAfter.creationFileTime,statAfter.creationFileTime,statAfter.mtimeFileTime])
    need(BigInt(stamp)>=BigInt(identity.creationFileTime)&&BigInt(stamp)<=BigInt(upper),'Modern filesystem times do not follow the actual native process creation');
  return {bytes,witness:{schema:'soui.modern-log-origin/v1',source:'LocalApplicationData-known-folder',
    directory,file,beforeSnapshot:before,process:identity,directoryStat:dirAfter,fileStat:statAfter,captureUpperFileTime:upper,
    ownershipScope:'fresh filesystem path correlated with native process; log contains no owner/browser FILETIME'}};
}
function lifecycle(log){
  need(typeof log==='string'&&log.endsWith('\n'),'Actual Modern log is truncated');
  const rows=log.replaceAll('\r\n','\n').split('\n');rows.pop();
  need(rows.length===4&&rows[0]==='process-start'&&/^host-started;runtime=[^\x00-\x1f]+$/.test(rows[1])&&
    rows[2]==='application-ready'&&rows[3]==='host-close-complete','Modern log lacks the actual ordered startup/ready/close trace');
}
function attemptFile(root,attempt,file){
  need(typeof file==='string'&&file.startsWith(attempt+'/'),'Modern evidence escapes this attempt');
  return id.safePath(root,file);
}
function validateReport(root,report,build,payload,selected={},toolsRoot=root){
  need(report.schema==='soui.modern-smoke/v2'&&report.version===build.version&&report.buildId===build.buildId&&
    report.sourceSha256===build.source.sha256&&report.architecture===build.architecture&&report.configuration===build.configuration&&
    report.finishedAt&&report.releaseReady===false,'Modern report differs from the current finite build scope');
  const x=report.ownedSupervision;
  need(x?.schema==='soui.modern-smoke-owned/v1'&&/^out\/(x86|x64)\/modern-owned-[0-9a-f-]{36}$/.test(x.attempt)&&
    x.attempt.startsWith('out/'+report.architecture+'/')&&x.toolsRoot===absolute(toolsRoot)&&
    x.controllerClosure==='external-caller-evidence-required'&&x.noninterferenceObservation===null,'Invalid Modern supervision scope');
  const inputBytes=fs.readFileSync(attemptFile(root,x.attempt,x.inputs));
  need(hash(inputBytes)===x.inputsSha256,'Modern inputs changed');
  const input=parseStrictJson(inputBytes.toString('utf8'));
  need(input.schema==='soui.modern-smoke-inputs/v1'&&input.buildId===build.buildId&&input.sourceSha256===build.source.sha256&&
    input.architecture===report.architecture&&input.configuration===report.configuration&&input.toolsRoot===x.toolsRoot&&
    isDeepStrictEqual(input.runner,x.runner)&&input.timeoutMs===40000&&input.drainMs===2000,'Modern inputs identify another build/runner/budget');
  const original=input.buildReceipt,after=x.buildReceiptAfter;
  const fixedBuildPath=buildReceiptPath(root,report.architecture,report.configuration);
  need(original?.pin&&after?.pin&&original.pin.path===fixedBuildPath&&original.pin.requestedPath===fixedBuildPath&&
    after.pin.path===fixedBuildPath&&after.pin.requestedPath===fixedBuildPath&&
    original.archive===x.attempt+'/before-build.json'&&after.archive===x.attempt+'/after-build.json',
    'Modern evidence must retain the original and current fixed-path Build receipt');
  const originalBytes=fs.readFileSync(attemptFile(root,x.attempt,original.archive));
  const afterBytes=fs.readFileSync(attemptFile(root,x.attempt,after.archive));
  need(originalBytes.length<=16777216&&afterBytes.length<=16777216&&isUtf8(originalBytes)&&isUtf8(afterBytes)&&
    originalBytes.length===original.pin.bytes&&afterBytes.length===after.pin.bytes&&
    hash(originalBytes)===original.pin.sha256&&hash(afterBytes)===after.pin.sha256,'Retained Build receipt bytes changed');
  const originalBuild=parseStrictJson(originalBytes.toString('utf8')),afterBuild=parseStrictJson(afterBytes.toString('utf8'));
  assertBuildUnchanged(originalBuild,afterBuild,original.pin,after.pin);
  need(isDeepStrictEqual(afterBuild,build),'Modern reader does not identify the current accepted Build');
  verifyTool(original.pin);verifyTool(after.pin);
  const value={architecture:report.architecture,configuration:report.configuration,toolsRoot:absolute(toolsRoot),
    localAppData:absolute(input.localAppData),nativeToolchain:selected.nativeToolchain||'MSVC143',
    toolchainRequest:selected.profileRequest,externalInputs:input.externalInputs};
  if(value.nativeToolchain==='LLVM22MSVC142'){
    need(value.toolchainRequest&&value.externalInputs&&Object.keys(value.externalInputs).length===6,'Missing explicit LLVM selection');
    const profile=profileFor(root,value);
    need(isDeepStrictEqual(input.nativeToolchain,profile)&&isDeepStrictEqual(report.nativeToolchain,profile),'Modern profile differs from selected Build');
  }else need(input.nativeToolchain===undefined&&report.nativeToolchain===undefined&&input.externalInputs===undefined,'Unselected LLVM profile');
  const setup=prepared(value),plan=planFor(root,build,value);
  need(isDeepStrictEqual(setup.runner,x.runner)&&isDeepStrictEqual(setup.helper,input.preparedHelper)&&
    isDeepStrictEqual(setup.report,input.preparedTools)&&isDeepStrictEqual(input.plan,plan)&&isDeepStrictEqual(payload,plan.payload)&&
    report.target===plan.target&&isDeepStrictEqual(report.payload,plan.payload),'Genuine prepared tool/payload/plan changed');
  const raw=fs.readFileSync(attemptFile(root,x.attempt,x.report)),context=fs.readFileSync(attemptFile(root,x.attempt,x.context));
  const finishedBytes=fs.readFileSync(attemptFile(root,x.attempt,x.finished));
  need(hash(finishedBytes)===x.finishedSha256,'Actual finished receipt changed');
  const finished=parseStrictJson(finishedBytes.toString('utf8'));
  need(x.expected.executable===plan.executable&&x.expected.cwd===plan.cwd&&isDeepStrictEqual(x.expected.args,plan.args)&&
    x.expected.timeoutMs===40000&&x.expected.drainMs===2000&&x.expected.executableSha256===plan.subject.sha256&&
    x.expected.runnerSha256===setup.runner.sha256,'Modern actual launch differs from current executable/argv');
  const identity=validateStage(raw,context,x.ready,finished,x.expected);
  need(report.pid===identity.pid&&report.creationFileTime===identity.creationFileTime&&report.exitCode===0&&
    report.timedOut===false&&report.passed===true,'Modern outcome is not native natural closure');
  const beforeBytes=fs.readFileSync(attemptFile(root,x.attempt,x.before));
  need(hash(beforeBytes)===x.beforeSha256,'Prelaunch profile snapshot changed');
  const before=parseStrictJson(beforeBytes.toString('utf8'));
  need(before.logRoot===plan.logRoot&&Array.isArray(before.names)&&before.names.length<=8192&&
    before.names.every(name=>typeof name==='string'&&/^smoke-[1-9]\d*$/.test(name))&&
    new Set(before.names).size===before.names.length,'Invalid finite prelaunch profile snapshot');
  const logBytes=fs.readFileSync(attemptFile(root,x.attempt,x.log)),witnessBytes=fs.readFileSync(attemptFile(root,x.attempt,x.origin));
  need(isUtf8(logBytes)&&logBytes.length<=1048576&&hash(logBytes)===x.logSha256&&hash(witnessBytes)===x.originSha256&&
    logBytes.toString('utf8')===report.log,'Captured Modern log bytes changed');
  const witness=parseStrictJson(witnessBytes.toString('utf8')),name='smoke-'+identity.pid;
  need(witness.schema==='soui.modern-log-origin/v1'&&witness.source==='LocalApplicationData-known-folder'&&
    isDeepStrictEqual(witness.beforeSnapshot,before)&&isDeepStrictEqual(witness.process,identity)&&!before.names.includes(name)&&
    witness.directory===id.safePath(plan.logRoot,name)&&witness.file===id.safePath(plan.logRoot,name+'/smoke.log')&&
    witness.directoryStat.kind==='directory'&&witness.fileStat.kind==='file'&&witness.fileStat.bytes===String(logBytes.length),
    'Log origin is not the fresh native PID profile');
  for(const stamp of [witness.directoryStat.creationFileTime,witness.fileStat.creationFileTime,witness.fileStat.mtimeFileTime])
    need(typeof stamp==='string'&&/^[1-9]\d{0,19}$/.test(stamp)&&typeof witness.captureUpperFileTime==='string'&&
      /^[1-9]\d{0,19}$/.test(witness.captureUpperFileTime)&&BigInt(stamp)>=BigInt(identity.creationFileTime)&&
      BigInt(stamp)<=BigInt(witness.captureUpperFileTime)&&BigInt(witness.captureUpperFileTime)<=18446744073709551615n,
      'Log creation interval differs from native process');
  lifecycle(report.log);
  return {applicationNaturalClosure:true,logScope:'native-template startup/ready/close only',
    browserIdentity:null,controllerClosure:'external-caller-evidence-required',noninterferenceObservation:null,releaseReady:false};
}
async function runSmoke(root,value){
  root=absolute(root);need(process.platform==='win32','Modern smoke requires Windows');
  const output=id.safePath(root,'out/'+value.architecture);fs.mkdirSync(output,{recursive:true});
  const folder=id.safePath(output,'modern-owned-'+crypto.randomUUID());fs.mkdirSync(folder);
  const reportPath=path.join(output,'modern-smoke-'+value.configuration+'.json');
  if(fs.existsSync(reportPath))fs.copyFileSync(reportPath,path.join(folder,'previous-modern-smoke.json'),fs.constants.COPYFILE_EXCL);
  let active=null,finished=null,report={schema:'soui.modern-smoke/v2',architecture:value.architecture,
    configuration:value.configuration,passed:false,releaseReady:false},interrupted=false;
  fs.writeFileSync(reportPath,encoded(report));
  const stop=()=>{interrupted=true;active?.stop();};process.on('SIGINT',stop);process.on('SIGTERM',stop);
  let stdout=null,stderr=null;
  try{
    const selected=selection(value),nativeToolchain=profileFor(root,value),setup=prepared(value);
    const {build}=evidence.verifyAcceptedBuild(root,value.architecture,value.configuration,false,selected),plan=planFor(root,build,value);
    const originalReceipt=captureBuildReceipt(root,value,build),beforeBuildPath=path.join(folder,'before-build.json');
    fs.writeFileSync(beforeBuildPath,originalReceipt.bytes,{flag:'wx'});
    const before=snapshot(plan.logRoot),beforePath=path.join(folder,'before-profile.json');write(beforePath,before);
    const input={schema:'soui.modern-smoke-inputs/v1',buildId:build.buildId,sourceSha256:build.source.sha256,
      architecture:value.architecture,configuration:value.configuration,toolsRoot:value.toolsRoot,localAppData:value.localAppData,
      runner:setup.runner,preparedHelper:setup.helper,preparedTools:setup.report,plan,timeoutMs:40000,drainMs:2000,
      buildReceipt:{pin:originalReceipt.pin,archive:relative(root,beforeBuildPath)},
      ...(nativeToolchain?{nativeToolchain,externalInputs:value.externalInputs}:{}),releaseReady:false};
    const inputPath=path.join(folder,'inputs.json');write(inputPath,input);
    const subject=inspectTool(plan.executable);
    need(subject.sha256===plan.subject.sha256&&subject.bytes===plan.subject.bytes,'Modern executable differs from current payload');
    verifyTool(setup.runner);verifyTool(setup.helper);
    const nativePath=path.join(folder,'owned.ndjson');stdout=fs.openSync(path.join(folder,'stdout.raw'),'wx');stderr=fs.openSync(path.join(folder,'stderr.raw'),'wx');
    const expected={executable:plan.executable,args:plan.args,cwd:plan.cwd,timeoutMs:40000,drainMs:2000,
      executableSha256:subject.sha256,runnerSha256:setup.runner.sha256};
    verifyTool(originalReceipt.pin);
    need(!interrupted,'Modern controller interrupted before launch');
    active=launchOwned(plan.executable,plan.args,{runner:setup.runner.path,cwd:plan.cwd,report:nativePath,timeoutMs:40000,drainMs:2000,stdout,stderr});
    expected.contextSha256=hash(fs.readFileSync(nativePath+'.context.json'));
    let ready=null;try{ready=await active.ready;}catch{/* Preserve the real failed finished/raw records. */}
    if(interrupted)active.stop();
    finished=await active.finished;write(path.join(folder,'finished.json'),finished);
    const identity=validateStage(fs.readFileSync(nativePath),fs.readFileSync(nativePath+'.context.json'),ready,finished,expected);
    const log=actualLog(plan,before,identity);fs.writeFileSync(path.join(folder,'app.log'),log.bytes,{flag:'wx'});
    write(path.join(folder,'log-origin.json'),log.witness);lifecycle(log.bytes.toString('utf8'));
    verifyTool(subject);verifyTool(setup.runner);verifyTool(setup.helper);
    const {build:currentBuild}=evidence.verifyAcceptedBuild(root,value.architecture,value.configuration,false,selected);
    const currentReceipt=captureBuildReceipt(root,value,currentBuild);
    const afterBuildPath=path.join(folder,'after-build.json');fs.writeFileSync(afterBuildPath,currentReceipt.bytes,{flag:'wx'});
    assertBuildUnchanged(build,currentBuild,originalReceipt.pin,currentReceipt.pin);
    const currentPlan=planFor(root,currentBuild,value);
    need(isDeepStrictEqual(currentPlan,plan),'Modern target or payload plan changed during smoke');
    need(!interrupted&&isDeepStrictEqual(prepared(value).report,setup.report),'Modern interrupted or prepared tools changed');
    report={schema:'soui.modern-smoke/v2',version:currentBuild.version,buildId:currentBuild.buildId,sourceSha256:currentBuild.source.sha256,
      architecture:value.architecture,configuration:value.configuration,target:plan.target,payload:plan.payload,
      pid:identity.pid,creationFileTime:identity.creationFileTime,exitCode:0,timedOut:false,log:log.bytes.toString('utf8'),
      passed:true,finishedAt:new Date().toISOString(),releaseReady:false,...(nativeToolchain?{nativeToolchain}:{}),
      ownedSupervision:{schema:'soui.modern-smoke-owned/v1',attempt:relative(root,folder),toolsRoot:value.toolsRoot,runner:setup.runner,
        inputs:relative(root,inputPath),inputsSha256:hash(fs.readFileSync(inputPath)),before:relative(root,beforePath),beforeSha256:hash(fs.readFileSync(beforePath)),
        report:relative(root,nativePath),context:relative(root,nativePath+'.context.json'),ready,expected,
        finished:relative(root,path.join(folder,'finished.json')),finishedSha256:hash(fs.readFileSync(path.join(folder,'finished.json'))),
        log:relative(root,path.join(folder,'app.log')),logSha256:hash(log.bytes),
        origin:relative(root,path.join(folder,'log-origin.json')),originSha256:hash(fs.readFileSync(path.join(folder,'log-origin.json'))),
        buildReceiptAfter:{pin:currentReceipt.pin,archive:relative(root,afterBuildPath)},
        controllerClosure:'external-caller-evidence-required',noninterferenceObservation:null}};
    validateReport(root,report,currentBuild,currentPlan.payload,selected,value.toolsRoot);
    verifyTool(originalReceipt.pin);verifyTool(currentReceipt.pin);
    need(!interrupted,'Modern controller interrupted during final validation');
    fs.writeFileSync(reportPath,encoded(report));write(path.join(folder,'modern-smoke.json'),report);
    return {schema:'soui.modern-smoke-controller/v1',passed:true,report:relative(root,reportPath),attempt:relative(root,folder),
      applicationNaturalClosure:true,controllerClosure:'external-caller-evidence-required',noninterferenceObservation:null,releaseReady:false};
  }catch(error){
    report.passed=false;report.failure=error.message;
    fs.writeFileSync(reportPath,encoded(report));write(path.join(folder,'modern-smoke.json'),report);
    const failure={schema:'soui.modern-smoke-controller/v1',passed:false,failure:error.message,attempt:relative(root,folder),
      controllerClosure:'external-caller-evidence-required',noninterferenceObservation:null,releaseReady:false};
    write(path.join(folder,'failure.json'),failure);return failure;
  }finally{
    if(active&&!finished){active.stop();finished=await active.finished;write(path.join(folder,'finished.json'),finished);}
    if(stdout!==null)fs.closeSync(stdout);if(stderr!==null)fs.closeSync(stderr);
    process.off('SIGINT',stop);process.off('SIGTERM',stop);
  }
}
module.exports={settings,planFor,assertBuildUnchanged,validateReport,runSmoke};
if(require.main===module)(async()=>{
  need(/^[a-f0-9]{32}$/.test(process.env.SOUI_TEST_MODERN_NONCE||''),'Use the lease-holding Test-Modern.ps1 entry');
  const result=await runSmoke(path.resolve(__dirname,'../..'),settings(process.argv.slice(2)));
  console.log(encoded(result));if(!result.passed)process.exitCode=1;
})().catch(error=>{console.error(error.message);process.exitCode=1;});
