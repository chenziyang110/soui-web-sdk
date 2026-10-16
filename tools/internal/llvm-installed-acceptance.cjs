'use strict';
// Fixed local readers consume independently owned request and capture data.
const fs=require('node:fs'),path=require('node:path');
const {isDeepStrictEqual}=require('node:util');
const id=require('./delivery-identity.cjs');
const profiles=require('./llvm-profile-inputs.cjs');
const llvm=require('./llvm-build-observer.cjs');
const standard=require('./standard-build.cjs');
const debug=require('./host64-build-debug-observer.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const profile='LLVM22MSVC142',owner=path.resolve(__dirname,'../..');
const need=(value,message)=>{if(!value)throw new Error(message);};
const norm=value=>path.resolve(value).toLowerCase();
const codeNames=[...new Set([...llvm.codeNames,'tools/Test-Acceptance.ps1','tests/acceptance/runner.cjs',
  'tools/internal/llvm-installed-acceptance.cjs','tools/internal/standard-build.cjs','tools/internal/CMake-Configure.ps1',
  'tools/internal/build-evidence.cjs','tools/internal/delivery-identity.cjs','tools/internal/owned-process.cjs',
  'tools/internal/owned-tail-evidence.cjs'])];
function pin(filename){const full=id.safePath(path.resolve(filename)),stat=fs.statSync(full);need(stat.isFile(),'Plain file required');
  const bytes=fs.readFileSync(full);return {path:full,bytes:bytes.length,sha256:id.hash(bytes)};}
function samePin(a,b){return !!a&&!!b&&norm(a.path)===norm(b.path)&&a.bytes===b.bytes&&a.sha256===b.sha256;}
function match(row){need(row&&typeof row.path==='string'&&Number.isSafeInteger(row.bytes)&&row.bytes>=0&&/^[a-f0-9]{64}$/.test(row.sha256||''),'Exact raw input pin required');
  const actual=pin(row.path);need(samePin(actual,row),'Pinned installed input changed: '+row.path);return actual;}
function read(filename){const row=pin(filename);need(row.bytes>0&&row.bytes<=67108864,'Bounded JSON required');return parseStrictJson(fs.readFileSync(row.path,'utf8'));}
function write(filename,value){id.safePath(filename);fs.writeFileSync(filename,Buffer.isBuffer(value)?value:id.encoded(value),{flag:'wx'});return pin(filename);}
function tree(directory){id.safePath(directory);need(fs.statSync(directory).isDirectory(),'Plain input directory required');
  return id.walk(directory).map(name=>pin(id.safePath(directory,name))).sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);}
function releasePaths(root,suite,architecture,configuration){
  root=id.safePath(path.resolve(root));need(root===owner,'Release consumer must belong to this SDK owner');
  need(['x86','x64'].includes(architecture)&&['Debug','Release'].includes(configuration),'Invalid release consumer configuration');
  suite=id.safePath(path.resolve(suite));need(path.dirname(suite)===path.join(root,'out/release-validation',architecture)&&
    /^[a-f0-9]{32}$/.test(path.basename(suite)),'Fresh owned release consumer output required');
  const sdk=path.join(root,'out','sdk-'+architecture),build=path.join(suite,'consumer'),install=path.join(root,'out/install',architecture);
  return {root,suite,architecture,configuration,build,install,source:path.join(root,'tests/package-consumer'),
    evidence:path.join(suite,'consumer-llvm-observer'),pending:path.join(suite,'consumer-pending.json'),
    command:path.join(suite,'consumer-llvm-configure.json'),consumerRequest:path.join(build,`llvm-profile-request-${configuration}.json`),
    sdkRequest:path.join(sdk,`llvm-profile-request-${configuration}.json`),sdkAccepted:path.join(sdk,`standard-llvm-${configuration}.json`),
    sdkReport:path.join(sdk,`build-${configuration}.json`),installedArchive:path.join(install,'lib',configuration,'soui_sdk.lib'),
    executable:path.join(build,configuration,'soui_consumer.exe'),record:path.join(suite,'consumer-llvm-build.json'),nativeLog:path.join(suite,'consumer-build.log')};
}
function paths(root,suite,architecture,configuration,kind='acceptance'){
  if(kind==='release')return releasePaths(root,suite,architecture,configuration);
  need(kind==='acceptance','Explicit fixed consumer kind required');
  root=id.safePath(path.resolve(root));need(root===owner,'Installed caller must belong to this SDK owner');
  need(['x86','x64'].includes(architecture)&&['Debug','Release'].includes(configuration),'Invalid installed configuration');
  suite=id.safePath(path.resolve(suite));need(path.dirname(suite)===path.join(root,'out/acceptance')&&
    /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(path.basename(suite)),'Fresh owned acceptance suite required');
  const sdk=path.join(root,'out','sdk-'+architecture),build=path.join(suite,'consumer');
  return {root,suite,architecture,configuration,build,source:path.join(suite,'consumer-source'),install:path.join(suite,'install'),
    nativeLog:path.join(suite,'compile.log'),evidence:path.join(suite,'consumer-llvm-observer'),
    pending:path.join(suite,'pending.json'),command:path.join(suite,'consumer-llvm-configure.json'),
    consumerRequest:path.join(build,`llvm-profile-request-${configuration}.json`),
    sdkRequest:path.join(sdk,`llvm-profile-request-${configuration}.json`),sdkAccepted:path.join(sdk,`standard-llvm-${configuration}.json`),
    sdkReport:path.join(sdk,`build-${configuration}.json`),installedArchive:path.join(suite,'install/lib',configuration,'soui_sdk.lib'),
    executable:path.join(build,'bin',configuration,'SouiAcceptance.exe'),record:path.join(suite,'consumer-llvm-build.json')};
}
function select(root,architecture,configuration,toolsRoot){
  root=id.safePath(path.resolve(root));need(root===owner,'SDK owner differs');toolsRoot=id.safePath(path.resolve(toolsRoot));
  const v={architecture,configuration,sdkOnly:true},requestFile=llvm.profileRequestPath(root,v);
  const current=llvm.verifyStandardBuild(root,architecture,configuration,true,{nativeToolchain:profile,profileRequest:requestFile});
  const prepared=llvm.preparation(toolsRoot,architecture),pair=llvm.validatePreparedProfile(root,v,prepared,requestFile);
  const validationTools={root:toolsRoot,preparation:prepared.reportPin,requestFile:pair.validation.pin,
    inputIdentity:pair.validation.inputIdentity,bridgeIdentity:pair.validation.bridgeIdentity,sourceFingerprint:prepared.setup.report.source.sha256};
  need(isDeepStrictEqual(current.accepted.validationTools,validationTools),'SDK accepted stock owner/profile differs from this selected preparation');
  const selection={profile,requestFile:pair.project.pin,sdkAccepted:pin(path.join(root,'out','sdk-'+architecture,`standard-llvm-${configuration}.json`)),validationTools};
  return {selection,build:current.build,accepted:current.accepted,prepared,project:pair.project};
}
function current(root,architecture,configuration,selection){
  need(selection&&selection.profile===profile&&Object.keys(selection).every(key=>['profile','requestFile','sdkAccepted','validationTools','consumer'].includes(key)),
    'Explicit installed LLVM selection required; no profile fallback');
  need(selection.validationTools?.root,'Selected stock owner required');
  const actual=select(root,architecture,configuration,selection.validationTools.root);
  const selected={...selection};delete selected.consumer;
  need(isDeepStrictEqual(actual.selection,selected),'Current SDK/stock selection differs from frozen acceptance');
  match(selection.requestFile);match(selection.sdkAccepted);return actual;
}
function configureArguments(root,suite,request,kind='acceptance'){
  const d=request.value.discovery,c=request.value.architecture,p=paths(root,suite,c,'Release',kind);
  return ['-S',p.source,'-B',p.build,'-G','Ninja Multi-Config',
    '-DCMAKE_TOOLCHAIN_FILE:FILEPATH='+path.join(root,'cmake/toolchains/SouiLLVM22.cmake').replaceAll('\\','/'),
    '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING='+path.join(root,'cmake/SouiWindowsSdk.cmake').replaceAll('\\','/'),
    '-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON',...Object.entries(d.CMakeDefinitions).map(([key,value])=>`-D${key}:STRING=${value}`),
    '--fresh',`-DSouiSdk_DIR=${p.install}/lib/cmake/SouiSdk`,...(kind==='acceptance'?[`-DSOUI_ACCEPTANCE_WEB_ROOT=${p.suite}/web-output`]:[])];
}
function natural(capture,expected){
  need(capture.ready&&capture.result&&capture.contextBytes&&capture.rawReport,'Original Owned publication and terminal evidence required');
  const context=parseStrictJson(capture.contextBytes.toString('utf8'));
  need(context.executable===expected.executable&&isDeepStrictEqual(context.args,expected.args)&&context.cwd===owner&&
    context.timeoutMs===expected.timeoutMs&&context.drainMs===2000&&context.runnerSha256===expected.runner.sha256&&
    context.executableSha256===expected.tool.sha256,'Actual Owned invocation differs from fixed installed stage');
  const result=standard.verifyStream(capture.rawReport,context,capture.ready,capture.result),t=result.terminal;
  need(capture.result.passed===true&&capture.result.exitCode===0&&capture.result.supervisorExitCode===0&&
    t.reason==='completed'&&t.passed===true&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&t.terminatedOwnedJob===false,
    'Installed stage requires natural whole-Job closure; forced cleanup stays failed');return result;
}
function captures(folder,name){return {raw:pin(path.join(folder,name+'.ndjson')),context:pin(path.join(folder,name+'.ndjson.context.json')),
  ready:pin(path.join(folder,name+'-ready.json')),finished:pin(path.join(folder,name+'-finished.json')),
  stdout:pin(path.join(folder,name+'-stdout.bin')),stderr:pin(path.join(folder,name+'-stderr.bin'))};}
function recorded(rows){Object.values(rows).forEach(match);return {ready:read(rows.ready.path),result:read(rows.finished.path),
  contextBytes:fs.readFileSync(rows.context.path),rawReport:fs.readFileSync(rows.raw.path)};}
function environment(cmd){const env={...process.env};for(const name of Object.keys(env))if([...llvm.providerNames,'ComSpec','VcpkgEnabled',
  'VcpkgEnableManifest','MSBUILDDISABLENODEREUSE'].some(key=>key.toLowerCase()===name.toLowerCase()))delete env[name];
  return {...env,ComSpec:cmd,VcpkgEnabled:'false',VcpkgEnableManifest:'false',MSBUILDDISABLENODEREUSE:'1'};}
function dependencies(root,suite,sdk,kind='acceptance'){
  const frozen=read(sdk.accepted.inputs.path),inventories=frozen.llvm.dependencyInventories;
  need(Array.isArray(inventories)&&inventories.length>0,'Accepted SDK tool/CRT inventories required');
  const p=paths(root,suite,sdk.build.architecture,sdk.build.configuration,kind);
  const roots=kind==='release'?[p.source,p.install]:['consumer-source','install','web-sdk','web-output','kernel-input'].map(name=>path.join(suite,name));
  const trees=roots.map(directory=>({root:directory,files:tree(directory)}));
  const rows=[...frozen.llvm.dependencyPins,...trees.flatMap(item=>item.files),pin(p.pending),
    ...(kind==='acceptance'?[pin(path.join(suite,'web-build.json')),pin(path.join(suite,'web-typecheck.tsconfig.json')),pin(path.join(suite,'web-typecheck.log'))]:[]),
    sdk.selection.requestFile,sdk.selection.sdkAccepted,...[sdk.accepted.observation,sdk.accepted.inputs,sdk.accepted.observed,sdk.accepted.buildReportSnapshot],
    ...Object.values(sdk.accepted.buildCapture),...Object.values(sdk.accepted.observerCapture)];
  const unique=new Map();for(const value of rows){const row=match(value),key=norm(row.path);if(unique.has(key))need(samePin(unique.get(key),row),'Conflicting frozen input');else unique.set(key,row);}
  return {inventories:[...inventories,...trees],pins:[...unique.values()]};
}
function recheckFrozen(frozen){match(frozen.pending);match(frozen.request);match(frozen.command);
  frozen.dependencies.pins.forEach(match);frozen.controls.forEach(match);
  for(const item of frozen.dependencies.inventories)need(isDeepStrictEqual(tree(item.root),item.files),'Installed/tool input inventory changed');
  frozen.tools.forEach(standard.verifyTool);}
function validateObservationFacts(row,cfg,subject,build,archive){
  const dual=cfg.schema==='soui.standard-llvm-observer-inputs/v2',version=dual?'v2':'v1';
  need((cfg.releaseConsumer===undefined)!==(cfg.installedConsumer===undefined),'One explicit fixed imported consumer context required');
  const release=cfg.releaseConsumer!==undefined,context=cfg.releaseConsumer||cfg.installedConsumer,
    binding=release?row?.releaseConsumer:row?.installedConsumer;
  need((release?row?.installedConsumer:row?.releaseConsumer)===undefined,
    'Imported observation cannot substitute or mix consumer context roles');
  need(row?.schema===(release?'soui.installed-release-llvm-build-observation/':'soui.installed-acceptance-llvm-build-observation/')+version&&row.profile===profile&&norm(row.root)===norm(cfg.root)&&
    row.architecture===cfg.architecture&&row.configuration===cfg.configuration&&row.sdkOnly===false&&row.compilerVerified===true&&
    row.inputIdentity===cfg.inputIdentity&&row.bridgeIdentity===cfg.bridgeIdentity&&row.observedCompilerId==='Clang'&&
    row.observedFrontend==='MSVC'&&row.observedVersion==='22.1.3'&&row.observedPointerBytes===(cfg.architecture==='x64'?8:4)&&
    row.completeToolchainProvenance===false&&row.kernelQualified===false&&row.abiQualified===false&&row.win7Qualified===false&&
    row.qualificationPassed===false&&row.releaseReady===false,'Dedicated installed observation identity/scope differs');
  const outer=dual?row.outerObserverCapture:row.buildCapture;
  need(outer?.pid===subject.start.pid&&outer.creationFileTime===subject.start.creationFileTime&&
    outer.wholeJobClosed===true&&outer.forced===false&&
    ['raw','context','ready','finished'].every(key=>samePin(outer[key],cfg.buildCapture[key])), 'Installed observer did not bind the original naturally closed build');
  if(dual)need(row.buildCapture?.role==='inner-build'&&row.buildCapture.pid===cfg.fastToolEvidence?.innerBuild?.pid&&
    row.buildCapture.creationFileTime===cfg.fastToolEvidence.innerBuild.creationFileTime&&row.buildCapture.pid!==outer.pid&&
    row.buildCapture.wholeJobClosed===true&&row.buildCapture.forced===false&&row.buildCapture.naturalRootExitCode===0&&
    samePin(row.buildCapture.raw,cfg.debugCapture.raw)&&samePin(row.buildCapture.input,cfg.debugCapture.input)&&
    isDeepStrictEqual(row.originalPollingCapture,cfg.buildCapture)&&isDeepStrictEqual(row.debugCapture,cfg.debugCapture)&&
    isDeepStrictEqual(row.fastToolEvidence,cfg.fastToolEvidence)&&cfg.fastToolEvidence.experimentalLifecycleObserved===true&&
    cfg.fastToolEvidence.outerObserver.pid===outer.pid&&cfg.fastToolEvidence.outerObserver.creationFileTime===outer.creationFileTime,
    'Installed consumer inner build and outer observer identities differ');
  need(samePin(row.requestFile,cfg.requestFile)&&samePin(row.buildReport,cfg.buildReport)&&
    binding?.sdkBuildId===build.buildId&&binding.sdkSourceSha256===build.source.sha256&&
    samePin(binding.sdkAccepted,context.sdkAccepted)&&samePin(binding.pending,context.pending)&&
    samePin(binding.consumerRequest,context.requestFile)&&samePin(binding.installedSdkArchive,archive),
    'Installed observation SDK/consumer/import bindings differ');
  need(Array.isArray(row.compilerFacts)&&row.compilerFacts.length===1&&row.compilerFacts[0].language==='CXX'&&
    Array.isArray(row.compileRecords)&&row.compileRecords.length===1&&row.currentCompileCount===1&&row.currentBinaryCount===1&&
    row.ninjaCompileEdges?.length===1&&row.ninjaDependencyRecords?.length===1&&row.binaryRecords?.length===1,
    'Fresh CXX consumer compile/dependency/link observations required');
  const link=row.binaryRecords[0];need(link.role==='LINKER'&&norm(link.output.path)===norm(release?path.join(cfg.buildDirectory,cfg.configuration,'soui_consumer.exe'):path.join(cfg.buildDirectory,'bin',cfg.configuration,'SouiAcceptance.exe'))&&
    link.machine===(cfg.architecture==='x64'?34404:332)&&link.reusedFrom===null&&
    link.inputLibraries.filter(item=>samePin(item,archive)).length===1&&row.compileRecords.every(item=>item.reusedFrom===null)&&
    norm(row.compileRecords[0].source.path)===norm(release?path.join(cfg.root,'tests/package-consumer/main.cpp'):path.join(context.suiteDirectory,'consumer-source/main.cpp'))&&
    row.compileRecords[0].machine===(cfg.architecture==='x64'?34404:332)&&link.inputObjects.length===1&&
    samePin(link.inputObjects[0],row.compileRecords[0].object),
    'Current ordinary acceptance EXE must freshly link the separately accepted installed archive');
  need(isDeepStrictEqual(row.frozenDependencyPins,cfg.dependencyPins),'Installed frozen dependency set differs');return true;
}
function validateObservation(row,cfg,subject,sdk,observationPath,observerContextBytes){
  const context=cfg.releaseConsumer||cfg.installedConsumer,kind=cfg.releaseConsumer?'release':'acceptance';
  validateObservationFacts(row,cfg,subject,sdk.build,pin(paths(owner,context.suiteDirectory,cfg.architecture,cfg.configuration,kind).installedArchive));
  if(row.configureLogCapture!==undefined)require('./configure-log-evidence.cjs').validate(row.configureLogCapture,{
    buildDirectory:cfg.buildDirectory,observationPath,observerContextBytes,evidencePins:row.evidencePins});
  for(const item of row.compileRecords){match(item.source);match(item.object);item.headers.forEach(match);}
  for(const item of row.resourceRecords){match(item.source);match(item.output);}
  for(const item of row.binaryRecords){match(item.output);[...item.inputObjects,...item.inputLibraries,...item.inputResources].forEach(match);}
  [...row.evidencePins,...row.dependencyPins,...row.frozenDependencyPins,...row.installedEvidencePins].forEach(match);
}
function controls(root,kind){return [...new Set([...codeNames,...debug.sourceNames,...(kind==='release'?['tools/Verify-Release.ps1','tools/internal/release-verification.cjs','tools/internal/desktop-smoke.cjs']:[])])].map(name=>pin(path.join(root,name)));}
function consumerDebug(binding,capture,root,p,kind,args){
  need(binding&&isDeepStrictEqual(binding.rootArguments,args),'Exact consumer debug build vector required');
  const input=read(binding.input.path);
  need(input.schema==='soui.host64-build-debug-inputs/3'&&input.consumerKind===kind&&norm(input.buildDirectory)===norm(p.build)&&
    input.sdkArchitecture===p.architecture&&input.configuration===p.configuration,'Consumer debug scope differs');
  return debug.recordedEvidence(binding,capture,root,args);
}
async function run(suite,kind='acceptance'){
  const pending=read(path.join(suite,kind==='release'?'consumer-pending.json':'pending.json')),p=paths(owner,suite,pending.architecture,pending.configuration,kind);
  if(kind==='release')need(pending.schema==='soui.release-installed-consumer-pending/v1','Explicit release consumer pending input required');
  need(!fs.existsSync(p.evidence)&&!fs.existsSync(p.record)&&!fs.existsSync(path.join(p.build,'CMakeCache.txt')),'Installed LLVM consumer cannot reuse output');
  const sdk=current(owner,p.architecture,p.configuration,pending.nativeToolchain);
  const request=profiles.readRequest(p.consumerRequest,p.architecture,owner);profiles.sameProfile(sdk.project,request);
  const command=read(p.command),args=configureArguments(owner,suite,request,kind);
  need(command.schema==='soui.installed-acceptance-llvm-configure/v1'&&Object.keys(command).sort().join(',')==='args,cmake,schema'&&
    isDeepStrictEqual(command.args,args),'Only the exact existing fresh CMake helper argv is admitted');
  const sdkFrozen=read(sdk.accepted.inputs.path),cmakeRows=sdkFrozen.tools.filter(row=>row.role==='cmake');
  need(cmakeRows.length===1&&norm(command.cmake)===norm(cmakeRows[0].requestedPath),'Consumer CMake must be the actual SDK-selected tool');
  const cmake=standard.inspectTool(command.cmake),ps=standard.inspectTool(request.value.discovery.ToolPins.POWERSHELL.path),
    runner=standard.inspectTool(sdk.prepared.setup.tools.OwnedProcessRunner),node=standard.inspectTool(process.execPath);
  fs.mkdirSync(p.evidence);
  const controlPins=controls(owner,kind),deps=dependencies(owner,suite,sdk,kind);
  const frozen={schema:'soui.installed-acceptance-llvm-inputs/v1',selection:sdk.selection,pending:pin(p.pending),request:request.pin,
    command:pin(p.command),controls:controlPins,dependencies:deps,tools:[cmake,ps,runner,node],configuration:p.configuration,architecture:p.architecture,
    qualificationPassed:false,releaseReady:false};
  const frozenPin=write(path.join(p.evidence,'inputs.json'),frozen),env=environment(request.value.discovery.NativeProcessPaths.CMD);
  const launch=async(name,executable,argv,tool,timeoutMs)=>{
    recheckFrozen(frozen);match(frozenPin);
    const expected={executable,args:argv,tool,runner,timeoutMs};
    const captured=await standard.capture(p.evidence,name,executable,argv,{runner:runner.path,cwd:owner,timeoutMs,drainMs:2000,env});
    const subject=natural(captured,expected);return {rows:captures(p.evidence,name),subject,expected};
  };
  try{
    const configure=await launch('configure',command.cmake,args,cmake,900000);
    write(path.join(suite,'configure.log'),fs.readFileSync(configure.rows.stdout.path));
    const buildArgs=['--build',p.build,'--config',p.configuration,'--parallel','2','--verbose'];
    const sdkDebug=sdkFrozen.llvm.debugBinding;
    need(sdkDebug?.prepared,'A current SDK with prepared native debug observer is required');
    const prepared=debug.loadPrepared(sdkDebug.prepared.path,sdkDebug.prepared.sha256,owner);
    const buildRunner=standard.inspectTool(prepared.value.outerRunner.path);
    need(samePin(prepared.value.outerRunner,buildRunner),'Consumer debug runner differs from its verified host producer');
    const planned=debug.makeInput(owner,p,p.evidence,request.value.discovery,sdkFrozen.tools,
      [...controlPins,frozenPin],prepared,buildArgs,{kind,buildDirectory:p.build});
    recheckFrozen(frozen);match(frozenPin);
    const captured=await debug.captureBuild(owner,p,p.evidence,request.value.discovery,sdkFrozen.tools,controlPins,prepared,buildArgs,env,standard,planned);
    const debugCapture={input:captured.input,raw:captured.raw,prepared:captured.prepared,observer:prepared.value.executable,rootArguments:buildArgs};
    const expected={executable:prepared.value.executable.path,args:['--input',captured.input.path,'--input-sha256',captured.input.sha256,'--report',captured.raw.path,'--nonce',planned.value.nonce],tool:prepared.value.executable,runner:buildRunner,timeoutMs:900000};
    const built={rows:captures(p.evidence,'build'),subject:natural(captured.capture,expected),expected};
    const fastToolEvidence=consumerDebug(debugCapture,captured.capture,owner,p,kind,buildArgs);
    const nativeLog=write(p.nativeLog,fs.readFileSync(built.rows.stdout.path));
    need(samePin({...nativeLog,path:built.rows.stdout.path},built.rows.stdout),'Compile log must retain exact original build stdout bytes');
    current(owner,p.architecture,p.configuration,pending.nativeToolchain);recheckFrozen(frozen);
    const cfg={schema:'soui.standard-llvm-observer-inputs/v2',profile,root:owner,architecture:p.architecture,configuration:p.configuration,
      sdkOnly:false,withWebView2:false,previousObservation:null,buildDirectory:p.build,evidenceDirectory:p.evidence,
      inputIdentity:request.inputIdentity,bridgeIdentity:request.bridgeIdentity,requestFile:sdk.selection.requestFile,buildReport:pin(p.sdkReport),
      controlPins,dependencyPins:deps.pins,nativeLog,buildCapture:{...built.rows,timeoutMs:900000,drainMs:2000},debugCapture,fastToolEvidence,
      ...{[kind==='release'?'releaseConsumer':'installedConsumer']:{schema:kind==='release'?'soui.installed-release-llvm-context/v1':'soui.installed-acceptance-llvm-context/v1',suiteDirectory:p.suite,pending:frozen.pending,
        requestFile:request.pin,sdkAccepted:sdk.selection.sdkAccepted}},qualificationPassed:false,releaseReady:false};
    const cfgPin=write(path.join(p.evidence,'observer-inputs.json'),cfg),observationPath=path.join(p.evidence,'llvm-observation.json');
    const observerArgs=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(owner,'tools/internal/Observe-LlvmBuild.ps1'),
      '-InputFile',cfgPin.path,'-InputSha',cfgPin.sha256,'-ObservationPath',observationPath];
    const observed=await launch('observer',ps.requestedPath,observerArgs,ps,120000),observation=read(observationPath);
    need(isDeepStrictEqual(consumerDebug(debugCapture,recorded(built.rows),owner,p,kind,buildArgs),fastToolEvidence),'Consumer debug evidence changed during semantic observation');
    validateObservation(observation,cfg,built.subject,sdk,observationPath,fs.readFileSync(observed.rows.context.path));recheckFrozen(frozen);match(frozenPin);match(cfgPin);
    current(owner,p.architecture,p.configuration,pending.nativeToolchain);
    const result={schema:kind==='release'?'soui.installed-release-llvm-build/v2':'soui.installed-acceptance-llvm-build/v2',accepted:true,compilerVerified:true,selection:sdk.selection,
      inputs:frozenPin,observerInputs:cfgPin,observation:pin(observationPath),configure,build:built,observer:observed,
      qualificationPassed:false,releaseReady:false};write(p.record,result);return {accepted:true,record:pin(p.record),qualificationPassed:false,releaseReady:false};
  }catch(error){write(path.join(p.evidence,'failure.json'),{accepted:false,failure:error.message,qualificationPassed:false,releaseReady:false});throw error;}
}
function verifyWithInputs(root,suite,selection,kind='acceptance'){
  const pending=read(path.join(suite,kind==='release'?'consumer-pending.json':'pending.json')),p=paths(root,suite,pending.architecture,pending.configuration,kind);
  if(kind==='release')need(pending.schema==='soui.release-installed-consumer-pending/v1','Explicit release consumer pending input required');
  const sdk=current(root,p.architecture,p.configuration,selection),recordPin=pin(p.record);
  if(selection.consumer)need(samePin(selection.consumer,recordPin),'Staged installed output differs');
  const result=read(p.record),base=kind==='release'?'soui.installed-release-llvm-build/':'soui.installed-acceptance-llvm-build/',dual=result.schema===base+'v2';
  need((dual||result.schema===base+'v1')&&result.accepted===true&&result.compilerVerified===true&&
    result.qualificationPassed===false&&result.releaseReady===false&&isDeepStrictEqual(result.selection,sdk.selection),'Dedicated installed acceptance missing');
  match(result.inputs);match(result.observerInputs);match(result.observation);
  const frozen=read(result.inputs.path),cfg=read(result.observerInputs.path);
  need(frozen.schema==='soui.installed-acceptance-llvm-inputs/v1'&&frozen.architecture===p.architecture&&frozen.configuration===p.configuration&&
    frozen.qualificationPassed===false&&frozen.releaseReady===false&&
    norm(result.inputs.path)===norm(path.join(p.evidence,'inputs.json'))&&norm(result.observerInputs.path)===norm(path.join(p.evidence,'observer-inputs.json'))&&
    norm(result.observation.path)===norm(path.join(p.evidence,'llvm-observation.json'))&&samePin(frozen.pending,pin(p.pending))&&
    isDeepStrictEqual(frozen.selection,sdk.selection),'Installed evidence owner/frozen selection differs');
  const consumer=profiles.readRequest(p.consumerRequest,p.architecture,root);profiles.sameProfile(sdk.project,consumer);
  const context=kind==='release'?cfg.releaseConsumer:cfg.installedConsumer;
  need(samePin(frozen.request,consumer.pin)&&samePin(context.requestFile,consumer.pin),'Current consumer own request differs');
  recheckFrozen(frozen);match(frozen.command);
  const expectedArgs=configureArguments(root,suite,consumer,kind),command=read(p.command);
  need(command.schema==='soui.installed-acceptance-llvm-configure/v1'&&Object.keys(command).sort().join(',')==='args,cmake,schema'&&
    isDeepStrictEqual(command.args,expectedArgs),'Current fixed configure command differs');
  need(isDeepStrictEqual(frozen.controls,controls(root,kind)),'Current local installed reader/control bytes differ');
  need(isDeepStrictEqual(frozen.dependencies,dependencies(root,suite,sdk,kind)),'Complete current installed/SDK/stock dependency inventories differ');
  need(Array.isArray(frozen.tools)&&frozen.tools.length===4,'Exact installed stage tool roles required');
  const [cmake,ps,runner,node]=frozen.tools;
  need(isDeepStrictEqual(cmake,standard.inspectTool(command.cmake))&&
    isDeepStrictEqual(ps,standard.inspectTool(consumer.value.discovery.ToolPins.POWERSHELL.path))&&
    isDeepStrictEqual(runner,standard.inspectTool(sdk.prepared.setup.tools.OwnedProcessRunner))&&
    isDeepStrictEqual(node,standard.inspectTool(process.execPath)),'Current installed caller/tool/Runner roles differ');
  const observerArgs=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/internal/Observe-LlvmBuild.ps1'),
    '-InputFile',result.observerInputs.path,'-InputSha',result.observerInputs.sha256,'-ObservationPath',result.observation.path];
  const expectedStages={configure:{executable:command.cmake,args:expectedArgs,tool:cmake,runner,timeoutMs:900000},
    build:{executable:command.cmake,args:['--build',p.build,'--config',p.configuration,'--parallel','2','--verbose'],tool:cmake,runner,timeoutMs:900000},
    observer:{executable:ps.requestedPath,args:observerArgs,tool:ps,runner,timeoutMs:120000}};
  let fastToolEvidence;
  if(dual){
    const binding=cfg.debugCapture,buildArgs=expectedStages.build.args;
    fastToolEvidence=consumerDebug(binding,recorded(result.build.rows),root,p,kind,buildArgs);
    const input=read(binding.input.path),sdkFrozen=read(sdk.accepted.inputs.path);
    need(samePin(binding.prepared,sdkFrozen.llvm.debugBinding.prepared),'Current SDK observer producer required for consumer');
    const prepared=debug.loadPrepared(binding.prepared.path,binding.prepared.sha256,root),
      buildRunner=standard.inspectTool(prepared.value.outerRunner.path);
    need(samePin(input.outerRunner,prepared.value.outerRunner)&&samePin(input.outerRunner,buildRunner),'Current physical host runner required for consumer build');
    expectedStages.build={executable:binding.observer.path,args:['--input',binding.input.path,'--input-sha256',binding.input.sha256,'--report',binding.raw.path,'--nonce',input.nonce],tool:binding.observer,runner:buildRunner,timeoutMs:900000};
  }
  const expectedCfg={schema:'soui.standard-llvm-observer-inputs/'+(dual?'v2':'v1'),profile,root,architecture:p.architecture,configuration:p.configuration,
    sdkOnly:false,withWebView2:false,previousObservation:null,buildDirectory:p.build,evidenceDirectory:p.evidence,
    inputIdentity:consumer.inputIdentity,bridgeIdentity:consumer.bridgeIdentity,requestFile:sdk.selection.requestFile,buildReport:pin(p.sdkReport),
    controlPins:frozen.controls,dependencyPins:frozen.dependencies.pins,nativeLog:pin(p.nativeLog),
    buildCapture:{...result.build.rows,timeoutMs:900000,drainMs:2000},...(dual?{debugCapture:cfg.debugCapture,fastToolEvidence}:{}),
    ...{[kind==='release'?'releaseConsumer':'installedConsumer']:{schema:kind==='release'?'soui.installed-release-llvm-context/v1':'soui.installed-acceptance-llvm-context/v1',suiteDirectory:p.suite,pending:frozen.pending,
      requestFile:consumer.pin,sdkAccepted:sdk.selection.sdkAccepted}},qualificationPassed:false,releaseReady:false};
  need(isDeepStrictEqual(cfg,expectedCfg),'Dedicated observer input roles/paths/current pins differ');
  const subjects={};for(const name of ['configure','build','observer']){
    const stage=result[name];need(isDeepStrictEqual(stage.rows,captures(p.evidence,name))&&isDeepStrictEqual(stage.expected,expectedStages[name]),
      'Original fixed stage invocation/pin set differs');
    subjects[name]=natural(recorded(stage.rows),expectedStages[name]);
  }
  need(isDeepStrictEqual(result.configure.expected.args,expectedArgs)&&result.configure.expected.timeoutMs===900000&&
    isDeepStrictEqual(dual?cfg.debugCapture.rootArguments:result.build.expected.args,['--build',p.build,'--config',p.configuration,'--parallel','2','--verbose'])&&
    result.build.expected.timeoutMs===900000&&result.observer.expected.timeoutMs===120000,
    'Recorded configure/build budgets/argv differ');
  const log=pin(p.nativeLog);need(log.bytes===result.build.rows.stdout.bytes&&log.sha256===result.build.rows.stdout.sha256&&samePin(log,cfg.nativeLog),
    'Current native log differs from original captured build stdout');
  validateObservation(read(result.observation.path),cfg,subjects.build,sdk,result.observation.path,fs.readFileSync(result.observer.rows.context.path));recheckFrozen(frozen);
  current(root,p.architecture,p.configuration,selection);return {sdk,consumer:recordPin};
}
function verify(root,suite,selection,kind='acceptance'){
  const verified=verifyWithInputs(root,suite,selection,kind);
  return {build:verified.sdk.build,consumer:verified.consumer};
}
function runtimeRunner(root,architecture,configuration,selection){const sdk=current(root,architecture,configuration,selection),runner=sdk.prepared.setup.tools.OwnedProcessRunner;
  need(!process.env.SOUI_OWNED_PROCESS_RUNNER||norm(process.env.SOUI_OWNED_PROCESS_RUNNER)===norm(runner),'LLVM runtime Runner override differs from selected current stock');return runner;}
const runRelease=suite=>run(suite,'release'),verifyRelease=(root,suite,selection)=>verify(root,suite,selection,'release');
module.exports={select,current,configureArguments,validateObservationFacts,run,verify,verifyWithInputs,runtimeRunner,runRelease,verifyRelease,releasePaths};
if(require.main===module)run(process.argv.length===4&&['build','build-release'].includes(process.argv[2])?process.argv[3]:(()=>{throw new Error('Usage: llvm-installed-acceptance.cjs build ABS_SUITE | build-release ABS_RELEASE_OUTPUT');})(),process.argv[2]==='build-release'?'release':'acceptance')
  .then(result=>console.log(id.encoded(result))).catch(error=>{console.error(error.message);process.exitCode=1;});
