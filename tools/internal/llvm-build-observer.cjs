'use strict';
// Runtime entry: it never imports a caller-supplied observer or identity script.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util');
const id=require('./delivery-identity.cjs');
const {parseStrictJson}=require('./owned-tail-evidence.cjs');
const profileInputs=require('./llvm-profile-inputs.cjs');
const measurement=require('./measurement-validation.cjs');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const need=(value,message)=>{if(!value)throw new Error(message);};
const profileName='LLVM22MSVC142';
const providerNames=['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE',
  'CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH',
  'LIBRARY_PATH','CCC_OVERRIDE_OPTIONS','CLANG_CONFIG_FILE_USER_DIR','CLANG_CONFIG_FILE_SYSTEM_DIR',
  'CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CC','CXX','AR','RC','CFLAGS','CXXFLAGS','LDFLAGS',
  'DESTDIR','NINJA_STATUS','RCFLAGS','EXTERNAL_INCLUDE','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH'];
const codeNames=['tools/internal/Find-LlvmToolchain.ps1','cmake/toolchains/SouiLLVM22.cmake',
  'tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1',
  'tools/internal/Observe-LlvmBuild.ps1','tools/internal/llvm-build-observer.cjs','tools/internal/llvm-profile-inputs.cjs',
  'tools/internal/measurement-validation.cjs','tools/internal/llvm-measurement-observation.cjs',
  'benchmarks/protocol/host64-build-debug-observer.cpp','benchmarks/protocol/host64-build-debug-observer/CMakeLists.txt',
  'tools/internal/Prepare-Host64BuildObserver.ps1','tools/internal/host64-build-debug-observer.cjs','third_party/include/json/json.hpp'];
function pin(filename){
  const full=id.safePath(path.resolve(filename)),s=fs.statSync(full);need(s.isFile(),'Plain input required: '+full);
  const bytes=fs.readFileSync(full);return {path:full,bytes:bytes.length,sha256:sha(bytes)};
}
function match(row){need(row&&typeof row.path==='string'&&Number.isSafeInteger(row.bytes)&&/^[a-f0-9]{64}$/.test(row.sha256),'Invalid raw pin');
  const actual=pin(row.path);need(actual.bytes===row.bytes&&actual.sha256===row.sha256,'Pinned bytes changed: '+row.path);return actual;}
function read(filename){const row=pin(filename);need(row.bytes>0&&row.bytes<=67108864,'Bounded JSON input required');return parseStrictJson(fs.readFileSync(row.path,'utf8'));}
function encoded(value){return Buffer.from(JSON.stringify(value,null,2)+'\n');}
function write(folder,name,value){const p=id.safePath(folder,name);fs.writeFileSync(p,Buffer.isBuffer(value)?value:encoded(value),{flag:'wx'});return p;}
function tree(root){
  id.safePath(root);need(fs.statSync(root).isDirectory(),'Plain inventory directory required');
  return id.walk(root).map(name=>pin(id.safePath(root,name))).sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
}
function assertInventory(rows){for(const row of rows)match(row);return rows;}
function freezeResourceWrapper(cmake){
  const selected=pin(cmake);need(path.basename(selected.path).toLowerCase()==='cmake.exe','Selected CMake executable required for resource wrapper');
  return pin(path.join(path.dirname(selected.path),'cmcldeps.exe'));
}
function validateResourceWrapperBinding(row,frozen){
  const wrapper=frozen.llvm.resourceWrapperPin,cmake=frozen.tools.filter(item=>item.role==='cmake');
  need(cmake.length===1&&wrapper&&path.resolve(wrapper.path)===path.join(path.dirname(cmake[0].path),'cmcldeps.exe'),
    'Resource wrapper must belong to the selected frozen CMake directory');
  match(wrapper);
  need(Array.isArray(frozen.llvm.dependencyPins)&&frozen.llvm.dependencyPins.filter(item=>samePin(item,wrapper)).length===1&&
    row.resourceWrapperPin&&samePin(row.resourceWrapperPin,wrapper),'Resource wrapper is missing from the whole frozen observation envelope');
  return wrapper;
}
function verifyCurrentCodePins(root,rows){
  need(Array.isArray(rows)&&rows.length===codeNames.length,'Complete current LLVM reader code pins required');
  for(const name of codeNames){
    const expected=path.join(root,name),selected=rows.filter(row=>path.resolve(row.path)===expected);
    need(selected.length===1,'Current LLVM reader code membership differs');
    const captured=match(selected[0]),local=pin(path.join(__dirname,'../..',name));
    need(captured.bytes===local.bytes&&captured.sha256===local.sha256,'Current local LLVM reader code differs from captured Source');
  }
}
function profileRequestPath(root,v){return path.join(root,'out',(v.sdkOnly?'sdk-':'')+v.architecture,`llvm-profile-request-${v.configuration}.json`);}
function selected(root,v){return {nativeToolchain:profileName,profileRequest:profileRequestPath(root,v)};}
function environment(base,cmd){
  const env={...base};for(const key of Object.keys(env))if(providerNames.some(name=>name.toLowerCase()===key.toLowerCase()))delete env[key];
  for(const key of Object.keys(env))if(['comspec','vcpkgenabled','vcpkgenablemanifest','msbuilddisablenodereuse'].includes(key.toLowerCase()))delete env[key];
  env.ComSpec=path.win32.normalize(cmd);env.VcpkgEnabled='false';env.VcpkgEnableManifest='false';env.MSBUILDDISABLENODEREUSE='1';return env;
}
function stockDirectory(toolsRoot,arch){
  need(['x64','x86'].includes(arch),'Invalid validation architecture');
  return path.join(toolsRoot,'out',arch==='x64'?'benchmark-protocol':'benchmark-protocol-x86');
}
function samePin(left,right){return path.resolve(left.path)===path.resolve(right.path)&&left.bytes===right.bytes&&left.sha256===right.sha256;}
function preparation(toolsRoot,arch){
  toolsRoot=id.safePath(path.resolve(toolsRoot));
  const directory=stockDirectory(toolsRoot,arch);
  need(path.resolve(measurement.buildDirectory(arch,toolsRoot))===directory,'Validation tools must own the selected architecture build directory');
  const reportFile=id.safePath(directory,'measurement-tools.json'),reportPin=pin(reportFile),raw=read(reportFile);
  const request=raw.nativeToolchain?.requestFile;
  if(request){
    need(path.resolve(request.path)===path.join(directory,'llvm-profile-request-Release.json'),'Validation request must be measurement-owned');
    match(request);profileInputs.readRequest(request.path,arch,toolsRoot);
    return {setup:measurement.preparedAt(toolsRoot,arch,{toolchainRequest:request.path}),request:read(request.path),requestPin:request,reportPin,toolsRoot};
  }
  return {setup:measurement.preparedAt(toolsRoot,arch),request:null,requestPin:null,reportPin,toolsRoot};
}
function validatePreparedProfile(root,v,prepared,requestFile){
  need(prepared?.requestPin&&prepared.request&&prepared.toolsRoot,'LLVM requires current independently owned validation-tool inputs');
  const stockFile=path.join(stockDirectory(prepared.toolsRoot,v.architecture),'llvm-profile-request-Release.json');
  need(path.resolve(prepared.requestPin.path)===stockFile,'Validation request must be measurement-owned');
  const project=profileInputs.readRequest(requestFile,v.architecture,root),validation=profileInputs.readRequest(stockFile,v.architecture,prepared.toolsRoot);
  need(samePin(prepared.requestPin,validation.pin)&&isDeepStrictEqual(prepared.request,validation.value),'Prepared validation request changed');
  profileInputs.sameRelocatedProfile(project,validation);
  return {project,validation};
}
function stockDependencyPins(prepared,validation){
  const report=prepared.setup.report,root=prepared.toolsRoot;
  need(samePin(prepared.reportPin,pin(path.join(stockDirectory(root,validation.value.architecture),'measurement-tools.json')))&&
    isDeepStrictEqual(read(prepared.reportPin.path),report),'Prepared validation report changed');
  const current=measurement.preparedAt(root,validation.value.architecture,{toolchainRequest:validation.pin.path});
  need(isDeepStrictEqual(current.report,report),'Prepared validation fingerprint or profile differs');
  const observation=report.nativeToolchain.observation;
  const observationPins=[...Object.values(observation.records),observation.cache,...observation.compilerRecords.map(row=>row.pin),
    ...observation.bindings.flatMap(row=>[row.source,row.object]),...observation.links.map(row=>row.executable),...observation.dependencyPins];
  return [prepared.reportPin,validation.pin,...[...report.source.files,...report.executables].map(row=>
    ({path:id.safePath(root,row.path),bytes:row.bytes,sha256:row.sha256})),...validation.value.discovery.BridgePins,validation.value.toolchain,...observationPins].map(match);
}
function validateValidationBinding(binding,root,v,requestFile,dependencyPins){
  need(binding?.root&&binding.preparation&&binding.requestFile,'Independent validation-tool binding missing');
  const toolsRoot=id.safePath(path.resolve(binding.root));
  need(path.resolve(binding.preparation.path)===path.join(stockDirectory(toolsRoot,v.architecture),'measurement-tools.json'),
    'Validation preparation must be measurement-owned');
  const prepared={toolsRoot,reportPin:binding.preparation,requestPin:binding.requestFile,request:read(binding.requestFile.path),
    setup:{report:read(binding.preparation.path)}};
  const profiles=validatePreparedProfile(root,v,prepared,requestFile);
  need(binding.inputIdentity===profiles.validation.inputIdentity&&binding.bridgeIdentity===profiles.validation.bridgeIdentity&&
    binding.sourceFingerprint===prepared.setup.report.source.sha256,'Frozen validation identity differs');
  const required=stockDependencyPins(prepared,profiles.validation);
  need(Array.isArray(dependencyPins)&&required.every(row=>dependencyPins.some(item=>samePin(item,row))),
    'Frozen dependencies omit current validation inputs');
  return profiles;
}
const discoverySource=String.raw`param([string]$Root,[string]$Architecture,[string]$LlvmRoot,[string]$MsvcRoot,[string]$WindowsSdkRoot,[string]$NinjaExe,[string]$WindowsCmdExe,[string]$PowerShellExe,[string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $Root 'tools/internal/Find-LlvmToolchain.ps1')
$profile=Get-SouiLlvmToolchain -Architecture $Architecture -LlvmRoot $LlvmRoot -MsvcRoot $MsvcRoot -WindowsSdkRoot $WindowsSdkRoot -NinjaExe $NinjaExe -WindowsCmdExe $WindowsCmdExe -PowerShellExe $PowerShellExe -ToolIdentityBridge (Join-Path $Root 'tools/internal/Invoke-ToolFileIdentity.ps1') -ToolIdentityHelper (Join-Path $Root 'tools/internal/Tool-FileIdentity.ps1')
$tools=[ordered]@{}
foreach($entry in @(@('cmake','cmake.exe'),@('node','node.exe'),@('npm','npm.cmd'))){
 $commands=@(Get-Command $entry[1] -CommandType Application -ErrorAction Stop)
 if($commands.Count-lt1){throw 'Missing actual command resolution'}
 $tools[$entry[0]]=[IO.Path]::GetFullPath([string]$commands[0].Source)
}
$record=[ordered]@{schema='soui.standard-llvm-discovery/v1';discovery=$profile;tools=$tools;observedCompiler=$null;qualificationPassed=$false}
[IO.File]::WriteAllText($Output,($record|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
`;
function nativeNatural(capture,verifyStream){
  need(capture.ready&&capture.result&&capture.contextBytes&&capture.rawReport,'Original owned evidence missing');
  const context=parseStrictJson(capture.contextBytes.toString('utf8'));
  const stream=verifyStream(capture.rawReport,context,capture.ready,capture.result),t=stream.terminal;
  need(capture.result.passed===true&&capture.result.exitCode===0&&capture.result.supervisorExitCode===0&&t.passed===true&&
    t.reason==='completed'&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob,'LLVM requires strict natural whole-Job closure');
  return stream;
}
function rawCapturePins(folder,name){return {raw:pin(path.join(folder,name+'.ndjson')),context:pin(path.join(folder,name+'.ndjson.context.json')),
  ready:pin(path.join(folder,name+'-ready.json')),finished:pin(path.join(folder,name+'-finished.json')),
  stdout:pin(path.join(folder,name+'-stdout.bin')),stderr:pin(path.join(folder,name+'-stderr.bin'))};}
function recordedCapture(rows){
  for(const row of Object.values(rows))match(row);
  return {ready:read(rows.ready.path),result:read(rows.finished.path),
    contextBytes:fs.readFileSync(rows.context.path),rawReport:fs.readFileSync(rows.raw.path)};
}
function requestMatches(root,v,request,discovery){
  need(request.schema==='soui.build-toolchain-request/v1'&&request.profile===profileName&&request.architecture===v.architecture&&
    isDeepStrictEqual(request.discovery,discovery),'Build request differs from prelaunch discovery');
  const expected=pin(path.join(root,'cmake/toolchains/SouiLLVM22.cmake'));
  need(path.resolve(request.toolchain.path)===expected.path&&request.toolchain.bytes===expected.bytes&&request.toolchain.sha256===expected.sha256,'Actual toolchain source changed');
}
function inputs(root,source,profile,validationPins=[]){
  const files=source.files.map(row=>pin(id.safePath(root,row.path)));
  // Private vendor source is outside the portable normalized source identity.
  // Its registered required source set is nevertheless a real prelaunch input.
  const vendor=require('./vendor-tests.cjs');for(const row of vendor.requiredSourcePaths(root))files.push(match(row));
  const directories=[...profile.IncludeDirectories,...profile.LibraryDirectories];
  const inventories=directories.map(directory=>({root:directory,files:tree(directory)}));
  const map=new Map();for(const row of [...files,...inventories.flatMap(value=>value.files),
    ...Object.values(profile.ToolPins),...profile.RequiredInputPins,...profile.BridgePins,...validationPins]){
    const full=path.resolve(row.path),key=full.toLowerCase();if(map.has(key))need(map.get(key).bytes===row.bytes&&map.get(key).sha256===row.sha256,'Conflicting dependency identity');
    else map.set(key,{path:full,bytes:row.bytes,sha256:row.sha256});
  }
  return {inventories,dependencyPins:[...map.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0)};
}
function validateNinjaScope(row, root, arch, config, sdkOnly) {
  const build = path.join(root, 'out', (sdkOnly ? 'sdk-' : '') + arch);
  require('./llvm-measurement-observation.cjs').verifyNinjaWorkdirRecords(row.ninjaScopePins, row.ninjaWorkdirBinding, build, config);
  for (const record of Object.values(row.ninjaScopePins)) {
    const matches = row.evidencePins.filter(item => path.resolve(item.path).toLowerCase() === path.resolve(record.path).toLowerCase());
    need(matches.length === 1 && isDeepStrictEqual(matches[0], record), 'Required Ninja scope pin differs from evidence snapshot');
  }
}
function validateObservation(row,root,v,profile,build,capture,observationRun,verifyStream){
  validateNinjaScope(row,root,v.architecture,v.configuration,!!v.sdkOnly);
  const natural=nativeNatural(capture,verifyStream);nativeNatural(observationRun,verifyStream);
  const debugMode=row.schema==='soui.standard-llvm-build-observation/v2';
  let debugProof=null;
  if(debugMode){
    need(row.debugCapture&&row.outerObserverCapture,'Explicit dual-root LLVM observation required');
    const input=read(row.debugCapture.input.path);
    debugProof=require('./host64-build-debug-observer.cjs').recordedEvidence(row.debugCapture,capture,root,row.debugCapture.rootArguments);
    need(input.sdkArchitecture===v.architecture&&input.configuration===v.configuration&&
      isDeepStrictEqual(row.fastToolEvidence,debugProof)&&isDeepStrictEqual(row.debugCapture.rootArguments,input.rootArguments),'Actual independent fast-tool evidence differs');
  }
  need((debugMode||row.schema==='soui.standard-llvm-build-observation/v1')&&row.profile===profileName&&path.resolve(row.root)===root&&
    row.architecture===v.architecture&&row.configuration===v.configuration&&row.sdkOnly===!!v.sdkOnly&&
    row.inputIdentity===profile.InputIdentity&&row.bridgeIdentity===profile.BridgeIdentity&&row.compilerVerified===true&&
    row.observedCompilerId==='Clang'&&row.observedFrontend==='MSVC'&&row.observedVersion==='22.1.3'&&
    row.observedPointerBytes===(v.architecture==='x64'?8:4)&&row.qualificationPassed===false&&row.releaseReady===false,
    'Actual LLVM observation identity differs');
  const outer=debugMode?row.outerObserverCapture:row.buildCapture;
  need(outer.pid===natural.start.pid&&outer.creationFileTime===natural.start.creationFileTime&&
    outer.raw.sha256===capture.result.reportSha256&&outer.wholeJobClosed===true&&!outer.forced,
    'Observer did not bind the current naturally closed Build');
  if(debugMode)need(row.buildCapture.role==='inner-build'&&row.buildCapture.pid===debugProof.innerBuild.pid&&row.buildCapture.creationFileTime===debugProof.innerBuild.creationFileTime&&
    samePin(row.buildCapture.raw,row.debugCapture.raw)&&samePin(row.buildCapture.input,row.debugCapture.input)&&row.buildCapture.wholeJobClosed===true&&row.buildCapture.forced===false,'Inner Build must bind actual CREATE/argv/EXIT, not a synthesized Owned start');
  match(outer.raw);match(outer.context);match(outer.ready);match(outer.finished);match(row.requestFile);match(row.buildReport);
  need(isDeepStrictEqual(row.requestFile,build.nativeToolchain.requestFile),'Observed request file differs from current build');
  need(isDeepStrictEqual(read(row.buildReport.path),build),'Observed source-build identity differs');
  need(Array.isArray(row.compileRecords)&&row.compileRecords.length>0&&Array.isArray(row.ninjaCompileEdges)&&row.ninjaCompileEdges.length===row.compileRecords.length&&
    Array.isArray(row.ninjaDependencyRecords)&&row.ninjaDependencyRecords.length===row.compileRecords.length&&Array.isArray(row.binaryRecords)&&row.binaryRecords.length>0,
    'Actual command/object/dependency/archive records missing');
  for(const item of row.compileRecords){match(item.source);match(item.object);for(const header of item.headers)match(header);}
  for(const item of row.resourceRecords){match(item.source);match(item.output);}
  for(const item of row.binaryRecords){match(item.output);for(const input of [...item.inputObjects,...item.inputLibraries,...item.inputResources])match(input);}
  // A command for an unrelated executable cannot authorize the deployed apps.
  // Each source-build payload must contain the very PE bound to its link edge.
  if(!v.sdkOnly)for(const [target,payload] of Object.entries({...build.payloads,...build.optionalPayloads})) {
    const file=payload.files.find(item=>item.path===target+'.exe');
    need(file,'Application payload lacks its executable');
    const deployed=pin(path.join(root,'out',v.architecture,'bin',v.configuration,target,target+'.exe'));
    need(deployed.bytes===file.bytes&&deployed.sha256===file.sha256,'Deployed application executable differs');
    const linked=row.binaryRecords.filter(item=>item.role==='LINKER'&&path.basename(item.output.path).toLowerCase()===(target+'.exe').toLowerCase()&&
      item.output.bytes===deployed.bytes&&item.output.sha256===deployed.sha256);
    need(linked.length===1,'Payload lacks its exact actual executable link proof: '+target);
  }
  for(const item of [...row.evidencePins,...row.dependencyPins,...row.frozenDependencyPins])match(item);
  return true;
}
async function run(root,v,folder,api){
  const toolsRoot=id.safePath(path.resolve(v.devkitRoot||root)),prepared=preparation(toolsRoot,v.architecture);
  need(prepared.requestPin,'LLVM requires current independently owned validation-tool inputs');
  const debug=v.host64BuildObserver?require('./host64-build-debug-observer.cjs'):null;
  const hostPrepared=debug?debug.loadPrepared(v.host64ObserverPrepared,v.host64ObserverSha256,root):null;
  const expectedRunner=hostPrepared?hostPrepared.value.outerRunner.path:prepared.setup.tools.OwnedProcessRunner;
  const runner=path.resolve(v.runner||expectedRunner);
  need(runner.toLowerCase()===expectedRunner.toLowerCase(),'Runner must be the actual prepared selected architecture or explicit host observer tool');
  const powershell=path.resolve(v.powerShellExe);need(!v.powershell||path.resolve(v.powershell).toLowerCase()===powershell.toLowerCase(),'LLVM PowerShell role must equal caller PowerShell');
  const ps=api.inspectTool(powershell),runnerPin=pin(runner),codePins=codeNames.map(name=>pin(path.join(root,name)));
  let env=environment(process.env,v.windowsCmdExe);write(folder,'selected-preparation.json',prepared.setup.report);
  const probe=write(folder,'llvm-discovery.ps1',Buffer.from(discoverySource)),probeOutput=path.join(folder,'llvm-discovery.json');
  const probeArgs=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',probe,'-Root',root,'-Architecture',v.architecture,
    ...Object.entries(api.llvmPaths).flatMap(([key,flag])=>[flag,v[key]]),'-Output',probeOutput];
  const discovered=await api.capture(folder,'llvm-discovery',powershell,probeArgs,{runner,cwd:root,timeoutMs:30000,drainMs:2000,env});
  nativeNatural(discovered,api.verifyStream);const facts=read(probeOutput),profile=facts.discovery;
  need(facts.schema==='soui.standard-llvm-discovery/v1'&&profile.Profile===profileName&&profile.Architecture===v.architecture&&
    ['soui.llvm-profile-inputs/v2','soui.llvm-profile-inputs/v3'].includes(profile.InputIdentitySchema)&&profile.Qualified===false&&profile.GeneratorValidated===false&&profile.ActualCompilerVersion===null,
    'Prelaunch discovery is invalid or claims compilation');
  // This private prelaunch envelope is requested-input evidence, never a Build receipt.
  const discoveredRequest=write(folder,'llvm-discovered-request.json',{schema:'soui.build-toolchain-request/v1',profile:profileName,
    architecture:v.architecture,discovery:profile,toolchain:pin(path.join(root,'cmake/toolchains/SouiLLVM22.cmake'))});
  const profiles=validatePreparedProfile(root,v,prepared,discoveredRequest),validationPins=stockDependencyPins(prepared,profiles.validation);
  const validationTools={root:toolsRoot,preparation:prepared.reportPin,requestFile:profiles.validation.pin,
    inputIdentity:profiles.validation.inputIdentity,bridgeIdentity:profiles.validation.bridgeIdentity,sourceFingerprint:prepared.setup.report.source.sha256};
  // CMake's fixed RC launcher is an independent prelaunch input. Its file pin
  // authenticates a later observed path; it never proves process execution.
  const resourceWrapperPin=freezeResourceWrapper(facts.tools.cmake);
  const toolPaths={...facts.tools,resourceWrapper:resourceWrapperPin.path,...Object.fromEntries(Object.entries(profile.ToolPins).map(([role,row])=>[role,row.path]))};
  const tools=[{role:'powershell',...ps},{role:'runner',...runnerPin},{role:'callerNode',...api.inspectTool(process.execPath)},
    ...Object.entries(toolPaths).map(([role,p])=>({role,...api.inspectTool(p)}))];
  need(path.resolve(facts.tools.node).toLowerCase()===path.resolve(process.execPath).toLowerCase(),'Build must use the current pinned Node runtime');
  // Keep the actual first-resolution tools at the front in every child, while
  // retaining the inherited PATH. No user/profile command provider is loaded.
  env.PATH=[...new Set([path.dirname(facts.tools.cmake),path.dirname(facts.tools.node),path.dirname(facts.tools.npm)]),env.PATH??env.Path??''].join(';');delete env.Path;
  const source=id.source(root),rawSource=id.snapshot(root,source.files.map(row=>row.path)),dependency=inputs(root,source,profile,[...validationPins,resourceWrapperPin]),scriptRows=api.scripts(root,v);
  const args=api.buildArguments(root,v),buildPath=path.join(root,'out',(v.sdkOnly?'sdk-':'')+v.architecture,`build-${v.configuration}.json`);
  let oldBuild={exists:false,buildId:null},previousObservation=null;
  if(fs.existsSync(buildPath)){const bytes=fs.readFileSync(buildPath);let buildId=null;try{buildId=read(buildPath).buildId;}catch{}
    write(folder,'previous-build.json.bin',bytes);oldBuild={exists:true,buildId,bytes:bytes.length,sha256:sha(bytes)};}
  const acceptedFile=path.join(path.dirname(buildPath),`standard-llvm-${v.configuration}.json`);
  if(fs.existsSync(acceptedFile)){
    // A prior success is usable only after its entire actual proof has been
    // revalidated. Invalid prior receipts cause a fresh build to fail closed.
    const previous=priorObservation(root,v.architecture,v.configuration,!!v.sdkOnly,profile,validationTools);
    previousObservation=pin(previous.observationPath);
  }
  const debugPlanned=debug?debug.makeInput(root,v,folder,profile,tools,[...codePins,...scriptRows.map(row=>({path:path.join(root,row.path),bytes:row.bytes,sha256:row.sha256})),
    pin(path.join(root,'source-manifest.json')),pin(probeOutput),...profile.RequiredInputPins,...profile.BridgePins],hostPrepared,args):null;
  const context={schema:'soui.owned-process-context/v1',runnerSha256:runnerPin.sha256,executableSha256:debug?hostPrepared.value.executable.sha256:ps.sha256,
    executable:powershell,args,cwd:root,timeoutMs:v.timeoutMs,drainMs:v.drainMs};
  if(debug){context.executable=hostPrepared.value.executable.path;context.args=['--input',debugPlanned.pin.path,'--input-sha256',debugPlanned.pin.sha256,'--report',debugPlanned.value.report,'--nonce',debugPlanned.value.nonce];}
  const frozen={schema:debug?'soui.standard-build-inputs/v2':'soui.standard-build-inputs/v1',version:api.buildEvidence.version(root),architecture:v.architecture,configuration:v.configuration,
    sdkOnly:!!v.sdkOnly,compilerTail:false,nativeToolchain:profileName,...Object.fromEntries(Object.keys(api.llvmPaths).map(key=>[key,v[key]])),
    source,rawSource,scripts:scriptRows,tools,scriptSha256:scriptRows.find(row=>row.path==='tools/Build.ps1').sha256,
    context,contextSha256:sha(encoded(context)),oldBuild,environment:env,llvm:{inputIdentity:profile.InputIdentity,bridgeIdentity:profile.BridgeIdentity,discovery:pin(probeOutput),
       sourceCodePins:codePins,dependencyInventories:dependency.inventories,dependencyPins:dependency.dependencyPins,resourceWrapperPin,validationTools,previousObservation}};
  if(debug){frozen.invocationOptions=v;frozen.llvm.debugBinding={input:debugPlanned.pin,raw:{path:debugPlanned.value.report,bytes:null,sha256:null},prepared:hostPrepared.pin,observer:hostPrepared.value.executable,rootArguments:args};}
  write(folder,'inputs.json',frozen);assertInventory(dependency.dependencyPins);tools.forEach(api.verifyToolCompatible);
  const debugRun=debug?await debug.captureBuild(root,v,folder,profile,tools,[],hostPrepared,args,env,api,debugPlanned):null;
  const launched=debugRun?debugRun.capture:await api.capture(folder,'build',powershell,args,{runner,cwd:root,timeoutMs:v.timeoutMs,drainMs:v.drainMs,env});
  if(debugRun){frozen.llvm.debugBinding.raw=debugRun.raw;write(folder,'closed-debug-inputs.json',frozen);}
  nativeNatural(launched,api.verifyStream);
  const buildBytes=fs.readFileSync(buildPath);write(folder,'current-build.json.bin',buildBytes);
  const requested=read(profileRequestPath(root,v));requestMatches(root,v,requested,profile);
  validateValidationBinding(validationTools,root,v,profileRequestPath(root,v),dependency.dependencyPins);
  const build=api.buildEvidence.verifyBuild(root,v.architecture,v.configuration,!!v.sdkOnly,selected(root,v));
  need(isDeepStrictEqual(parseStrictJson(buildBytes.toString('utf8')),build),'Build report changed during verification');
  const raw=rawCapturePins(folder,'build');const observerDirectory=path.join(folder,'llvm-observer');fs.mkdirSync(observerDirectory);
  const observerPath=path.join(root,'tools/internal/Observe-LlvmBuild.ps1'),observationPath=path.join(observerDirectory,'llvm-observation.json');
  const cfg={schema:debug?'soui.standard-llvm-observer-inputs/v2':'soui.standard-llvm-observer-inputs/v1',profile:profileName,root,architecture:v.architecture,configuration:v.configuration,sdkOnly:!!v.sdkOnly,
    withWebView2:!!v.withWebView2,buildDirectory:path.dirname(buildPath),evidenceDirectory:observerDirectory,inputIdentity:profile.InputIdentity,bridgeIdentity:profile.BridgeIdentity,
    requestFile:pin(profileRequestPath(root,v)),buildReport:pin(buildPath),buildCapture:{...raw,timeoutMs:v.timeoutMs,drainMs:v.drainMs},
    controlPins:codePins,dependencyPins:dependency.dependencyPins,resourceWrapperPin,nativeLog:pin(path.join(path.dirname(buildPath),`native-${v.configuration}.log`)),
    previousObservation,qualificationPassed:false,releaseReady:false};
  if(debugRun){cfg.debugCapture=frozen.llvm.debugBinding;cfg.fastToolEvidence=debugRun.proof;}
  const cfgFile=write(folder,'llvm-observer-inputs.json',cfg),cfgPin=pin(cfgFile);
  const observationRun=await api.capture(folder,'llvm-observer',powershell,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',observerPath,
    '-InputFile',cfgFile,'-InputSha',cfgPin.sha256,'-ObservationPath',observationPath],{runner,cwd:root,timeoutMs:120000,drainMs:2000,env});
  nativeNatural(observationRun,api.verifyStream);const observation=read(observationPath);validateResourceWrapperBinding(observation,frozen);validateObservation(observation,root,v,profile,build,launched,observationRun,api.verifyStream);
  assertInventory(dependency.dependencyPins);for(const inventory of dependency.inventories)need(isDeepStrictEqual(tree(inventory.root),inventory.files),'Tool input inventory changed');
  tools.forEach(api.verifyToolCompatible);codePins.forEach(match);
  const finalPreparation=preparation(toolsRoot,v.architecture);
  need(isDeepStrictEqual(finalPreparation.setup.report,prepared.setup.report)&&samePin(finalPreparation.reportPin,prepared.reportPin),
    'Validation preparation changed during Build');
  validateValidationBinding(validationTools,root,v,profileRequestPath(root,v),dependency.dependencyPins);
  const currentSource=id.source(root),observed={sourceSha256:currentSource.sha256,rawSourceSha256:id.snapshot(root,currentSource.files.map(row=>row.path)).sha256,
    scriptSha256:pin(path.join(root,'tools/Build.ps1')).sha256,scripts:api.scripts(root,v),tools:tools.map(api.currentTool),artifacts:api.artifacts(build),compilerVerified:true,
    llvm:{profile:profileName,architecture:v.architecture,configuration:v.configuration,inputIdentity:profile.InputIdentity,bridgeIdentity:profile.BridgeIdentity,
      observedPointerBytes:observation.observedPointerBytes,buildPid:debugRun?debugRun.proof.innerBuild.pid:launched.result.pid,buildCreationFileTime:debugRun?debugRun.proof.innerBuild.creationFileTime:launched.result.creationFileTime,
      requestFile:cfg.requestFile,validationTools,
      observation:pin(observationPath),observerCapture:rawCapturePins(folder,'llvm-observer'),observerNaturalClosure:true}};
  if(debugRun)observed.llvm.debugProof=debugRun.proof;
  write(folder,'compiler-observation.json',observation);write(folder,'observed.json',observed);
  const answer=api.evaluateBuild({...launched,frozen,buildReportBytes:buildBytes,observed,...(debugRun?{debugReportBytes:fs.readFileSync(debugRun.raw.path),debugInputBytes:fs.readFileSync(debugRun.input.path)}:{})});
  if(answer.accepted){const accepted={schema:debug?'soui.standard-llvm-accepted-build/v2':'soui.standard-llvm-accepted-build/v1',architecture:v.architecture,configuration:v.configuration,sdkOnly:!!v.sdkOnly,
    buildId:build.buildId,sourceSha256:build.source.sha256,attemptDirectory:folder,observation:pin(observationPath),inputs:pin(path.join(folder,debug?'closed-debug-inputs.json':'inputs.json')),
    observed:pin(path.join(folder,'observed.json')),buildCapture:raw,observerCapture:rawCapturePins(folder,'llvm-observer'),requestFile:pin(profileRequestPath(root,v)),
    buildReportSnapshot:pin(path.join(folder,'current-build.json.bin')),validationTools,qualificationPassed:false,releaseReady:false};
    const temporary=write(folder,'accepted-build.json',accepted);fs.copyFileSync(temporary,acceptedFile);}
  return answer;
}
function priorObservation(root,arch,config,sdkOnly,profile,validationTools){
  // Historical raw captures and the archived source-build report authenticate
  // old observations. Current source/header hashes are checked separately for
  // each reused object below; a source edit must not invalidate unrelated TUs.
  const directory=path.join(root,'out',(sdkOnly?'sdk-':'')+arch),accepted=read(path.join(directory,`standard-llvm-${config}.json`));
  need(['soui.standard-llvm-accepted-build/v1','soui.standard-llvm-accepted-build/v2'].includes(accepted.schema)&&accepted.architecture===arch&&accepted.configuration===config&&accepted.sdkOnly===sdkOnly&&
    accepted.qualificationPassed===false&&accepted.releaseReady===false,'Prior LLVM acceptance scope differs');
  const folder=id.safePath(accepted.attemptDirectory);need(path.dirname(folder)===path.join(root,'out/standard-build')&&path.basename(folder).startsWith('attempt-'),'Prior attempt ownership differs');
  for(const row of [accepted.observation,accepted.inputs,accepted.observed,accepted.buildReportSnapshot,accepted.requestFile])match(row);
  const result=read(path.join(folder,'result.json'));need(result.accepted===true&&result.genericSupervisionPassed===true&&result.compilerTailAcceptance===false,'Prior build lacks strict natural acceptance');
  const standard=require('./standard-build.cjs'),buildCapture=recordedCapture(accepted.buildCapture),observerCapture=recordedCapture(accepted.observerCapture);
  const natural=nativeNatural(buildCapture,standard.verifyStream);nativeNatural(observerCapture,standard.verifyStream);
  const old=read(accepted.observation.path),frozen=read(accepted.inputs.path),build=read(accepted.buildReportSnapshot.path);
  const priorDebug=accepted.schema==='soui.standard-llvm-accepted-build/v2';
  let historicalSubject=natural.start;
  if(priorDebug){
    need(frozen.schema==='soui.standard-build-inputs/v2'&&old.schema==='soui.standard-llvm-build-observation/v2'&&isDeepStrictEqual(old.debugCapture,frozen.llvm.debugBinding),'Historical dual-root schema/binding differs');
    const proof=require('./host64-build-debug-observer.cjs').recordedEvidence(old.debugCapture,buildCapture,root,standard.buildArguments(root,frozen.invocationOptions));
    need(isDeepStrictEqual(old.fastToolEvidence,proof)&&old.outerObserverCapture.pid===natural.start.pid&&old.outerObserverCapture.creationFileTime===natural.start.creationFileTime&&old.outerObserverCapture.raw.sha256===buildCapture.result.reportSha256,'Historical real outer observer differs');
    historicalSubject=proof.innerBuild;
  }
  verifyCurrentCodePins(root,frozen.llvm.sourceCodePins);
  validateResourceWrapperBinding(old,frozen);
  validateNinjaScope(old,root,arch,config,sdkOnly);
  need(isDeepStrictEqual(accepted.validationTools,validationTools)&&isDeepStrictEqual(frozen.llvm.validationTools,validationTools)&&
    isDeepStrictEqual(old.frozenDependencyPins,frozen.llvm.dependencyPins),'Historical validation-tool ownership differs');
  validateValidationBinding(validationTools,root,{architecture:arch},accepted.requestFile.path,frozen.llvm.dependencyPins);
  need((priorDebug||old.schema==='soui.standard-llvm-build-observation/v1')&&old.compilerVerified===true&&path.resolve(old.root)===root&&
    old.architecture===arch&&old.configuration===config&&old.sdkOnly===sdkOnly&&old.inputIdentity===profile.InputIdentity&&
    old.bridgeIdentity===profile.BridgeIdentity&&old.buildCapture.pid===historicalSubject.pid&&old.buildCapture.creationFileTime===historicalSubject.creationFileTime&&
    old.buildCapture.raw.sha256===(priorDebug?old.debugCapture.raw.sha256:buildCapture.result.reportSha256)&&old.buildReport.sha256===accepted.buildReportSnapshot.sha256&&
    build.buildId===accepted.buildId&&build.source.sha256===accepted.sourceSha256&&build.nativeBuild==='passed'&&
    frozen.llvm.inputIdentity===profile.InputIdentity&&frozen.llvm.bridgeIdentity===profile.BridgeIdentity,
    'Historical observation does not bind its actual source-build and native capture');
  return {observationPath:accepted.observation.path};
}
function verifyStandardBuild(root,arch,config,sdkOnly=false,request={}){
  root=id.safePath(path.resolve(root));need(request.nativeToolchain===profileName,'An explicit LLVM request is required');
  const build=require('./build-evidence.cjs').verifyBuild(root,arch,config,sdkOnly,request);
  const directory=path.join(root,'out',(sdkOnly?'sdk-':'')+arch),accepted=read(path.join(directory,`standard-llvm-${config}.json`));
  need(['soui.standard-llvm-accepted-build/v1','soui.standard-llvm-accepted-build/v2'].includes(accepted.schema)&&accepted.architecture===arch&&accepted.configuration===config&&accepted.sdkOnly===sdkOnly&&
    accepted.buildId===build.buildId&&accepted.sourceSha256===build.source.sha256&&!accepted.qualificationPassed&&!accepted.releaseReady,'Current accepted LLVM build identity missing');
  const folder=id.safePath(accepted.attemptDirectory);need(path.dirname(folder)===path.join(root,'out/standard-build')&&path.basename(folder).startsWith('attempt-'),'Actual standard attempt ownership differs');
  const result=read(path.join(folder,'result.json'));need(result.accepted===true&&result.genericSupervisionPassed===true&&!result.compilerTailAcceptance,'Original standard build not accepted naturally');
  for(const row of [accepted.observation,accepted.inputs,accepted.observed,accepted.requestFile,accepted.buildReportSnapshot])match(row);
  const standard=require('./standard-build.cjs');
  const current=read(accepted.observation.path),frozen=read(accepted.inputs.path),profile=read(accepted.requestFile.path).discovery;
  if(accepted.schema==='soui.standard-llvm-accepted-build/v2'){
    need(frozen.schema==='soui.standard-build-inputs/v2'&&current.schema==='soui.standard-llvm-build-observation/v2'&&isDeepStrictEqual(current.debugCapture,frozen.llvm.debugBinding)&&
      frozen.invocationOptions.architecture===arch&&frozen.invocationOptions.configuration===config&&!!frozen.invocationOptions.sdkOnly===sdkOnly,'Current dual-root schema/target differs');
    const proof=require('./host64-build-debug-observer.cjs').recordedEvidence(current.debugCapture,recordedCapture(accepted.buildCapture),root,standard.buildArguments(root,frozen.invocationOptions));
    need(isDeepStrictEqual(proof,current.fastToolEvidence),'Current recorded inner/outer proof differs');
  }else need(frozen.schema==='soui.standard-build-inputs/v1'&&current.schema==='soui.standard-llvm-build-observation/v1','Schema1 must retain its original single root');
  verifyCurrentCodePins(root,frozen.llvm.sourceCodePins);
  validateResourceWrapperBinding(current,frozen);
  need(isDeepStrictEqual(accepted.validationTools,frozen.llvm.validationTools)&&
    isDeepStrictEqual(current.frozenDependencyPins,frozen.llvm.dependencyPins),'Accepted validation-tool binding differs');
  validateValidationBinding(frozen.llvm.validationTools,root,{architecture:arch},accepted.requestFile.path,frozen.llvm.dependencyPins);
  validateObservation(current,root,{architecture:arch,configuration:config,sdkOnly},profile,build,recordedCapture(accepted.buildCapture),recordedCapture(accepted.observerCapture),standard.verifyStream);
  need(frozen.nativeToolchain===profileName&&frozen.llvm.inputIdentity===profile.InputIdentity&&frozen.llvm.bridgeIdentity===profile.BridgeIdentity,'Prior frozen profile differs');
  return {build,observation:current,observationPath:accepted.observation.path,accepted,compilerVerified:true,qualificationPassed:false,releaseReady:false};
}
module.exports={freezeResourceWrapper,validateResourceWrapperBinding,validateNinjaScope,codeNames,providerNames,preparation,profileRequestPath,selected,verifyCurrentCodePins,validatePreparedProfile,validateValidationBinding,run,verifyStandardBuild,cleanEnvironment:environment};
if(require.main===module){
  try{
    const args=process.argv.slice(2);
    need(args.length===6&&args[0]==='verify'&&['x64','x86'].includes(args[2])&&
      ['Debug','Release'].includes(args[3])&&['sdk-only','applications'].includes(args[4])&&
      /^[A-Za-z]:[\\/]/.test(args[1])&&/^[A-Za-z]:[\\/]/.test(args[5]),
      'Use verify ROOT x64|x86 Debug|Release sdk-only|applications ABS_REQUEST');
    const proof=verifyStandardBuild(args[1],args[2],args[3],args[4]==='sdk-only',
      {nativeToolchain:profileName,profileRequest:args[5]});
    process.stdout.write(JSON.stringify({schema:'soui.standard-llvm-verified-build/v1',profile:profileName,
      architecture:args[2],configuration:args[3],sdkOnly:args[4]==='sdk-only',buildId:proof.build.buildId,
      sourceSha256:proof.build.source.sha256,inputIdentity:proof.observation.inputIdentity,
      observation:proof.accepted.observation,compilerVerified:true,naturalBuildClosureVerified:true,
      qualificationPassed:false,releaseReady:false})+'\n');
  }catch(error){process.stderr.write(error.message+'\n');process.exitCode=1;}
}
