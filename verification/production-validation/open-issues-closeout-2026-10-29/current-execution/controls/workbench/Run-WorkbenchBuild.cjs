'use strict';
// Output-only real tooling smoke. Authoring this file does not execute it.
// No product/GUI, generated EXE execution, helper termination or old-result rewrite.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util');
const repository='C:/sc07',legacy='C:/sr06';
const runner='C:/sr06/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
const powershell='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe';
const preparation='C:/sr06/out/benchmark-protocol/measurement-tools.json';
const expectedRunner='f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83';
const expectedPreparation='0de724e35238813efe68f35c0f286f6432934d695d12e2575a8e95caa76ba629';
const expectedLegacyReader='15bb990bf0dc2e62e460c4f157c24570b953600b888d666bc5ea27d9a314896f';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const need=(value,message)=>{if(!value)throw Error(message);};
const encode=value=>JSON.stringify(value,null,2)+'\n';
const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();
function pin(filename){
  const absolute=path.resolve(filename),before=fs.lstatSync(absolute,{bigint:true});
  need(before.isFile()&&!before.isSymbolicLink(),'Expected a plain file: '+absolute);
  const bytes=fs.readFileSync(absolute),after=fs.lstatSync(absolute,{bigint:true});
  need(['dev','ino','size','mtimeNs','ctimeNs','nlink'].every(name=>before[name]===after[name])&&BigInt(bytes.length)===before.size,'File changed while pinned: '+absolute);
  return {path:absolute,bytes:bytes.length,sha256:hash(bytes),fileId:before.ino.toString(),mtimeNs:before.mtimeNs.toString(),ctimeNs:before.ctimeNs.toString()};
}
function save(directory,name,value){fs.writeFileSync(path.join(directory,name),encode(value),{flag:'wx'});}
function unchanged(rows){for(const expected of rows)need(isDeepStrictEqual(pin(expected.path),expected),'Frozen input changed: '+expected.path);}
function walk(directory){
  const files=[];for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
    const filename=path.join(directory,entry.name);need(!entry.isSymbolicLink(),'Unexpected linked output');
    if(entry.isDirectory())files.push(...walk(filename));else{need(entry.isFile(),'Unexpected output kind');files.push(filename);}
  }return files.sort();
}
function natural(result){
  const t=result.terminal;
  need(result.passed===true&&result.exitCode===0&&result.supervisorExitCode===0&&result.signal===null&&
    result.spawnError===null&&result.reportError===null&&result.stopRequested===false&&result.fallbackUsed===false&&result.closureConfirmed===true&&
    t?.reason==='completed'&&t.rootExited===true&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&
    t.limitTerminatedProcesses===0&&t.terminatedOwnedJob===false&&t.closureConfirmed===true&&t.passed===true,
    'The real current stage did not complete naturally; original failure is retained.');
}
function verifyCase(directory,architecture,capture,standard){
  need(capture.readyError===null,'Actual ready observation failed');
  const context=JSON.parse(capture.contextBytes),rows=capture.rawReport.toString('utf8').trimEnd().split('\n').map(JSON.parse);
  standard.verifyStream(capture.rawReport,context,capture.ready,capture.result);natural(capture.result);
  need(context.timeoutMs===120000&&context.drainMs===2000&&!context.tailImagePin,'Original fixed natural-only bounds differ');
  const receipt=JSON.parse(fs.readFileSync(path.join(directory,'root-return.json')));
  need(receipt.completed===true&&receipt.architecture===architecture&&receipt.rootPid===capture.ready.pid&&
    receipt.productExecuted===false&&receipt.executableExecuted===false&&receipt.cleanupFailures.length===0&&receipt.oldFailureReclassified===false,'Root return is incomplete');
  const before=JSON.parse(fs.readFileSync(path.join(directory,'tool-pins-before.json'))),after=JSON.parse(fs.readFileSync(path.join(directory,'tool-pins-after.json')));
  need(isDeepStrictEqual(before,after),'Selected actual compiler/tool bytes changed');
  for(const expected of before){const actual=pin(expected.path);need(actual.bytes===expected.bytes&&actual.sha256===expected.sha256,'Actual tool pin differs');}
  const invocations=JSON.parse(fs.readFileSync(path.join(directory,'invocations.json')));
  need(invocations.toolchain.MsvcVersion==='14.44.35207'&&invocations.toolchain.Toolset==='v143'&&invocations.toolchain.HostArchitecture==='x64'&&
    invocations.environment.MSBUILDDISABLENODEREUSE==='1'&&invocations.compilerHelperSuppressionConfigured===false,'Actual MSVC profile differs');
  need(samePath(receipt.installedSdkPrefix,path.join(legacy,'out/release-install-'+architecture+'-release')),'Installed SDK prefix differs');
  const compiler=path.win32.join(invocations.toolchain.MsvcRoot,'bin','Hostx64',architecture,'cl.exe');
  const copiedSource=JSON.parse(fs.readFileSync(path.join(directory,'fixture-source-pins.json')));
  const frozen=JSON.parse(fs.readFileSync(path.join(__dirname,'FrozenSource.actual.json')));
  need(copiedSource.length===frozen.sourceFiles.length,'Workbench source fixture is incomplete');
  for(const copy of copiedSource){const relative=path.relative(path.join(receipt.workspace,'source'),copy.path);const original=pin(path.join(__dirname,'source',relative));need(original.bytes===copy.bytes&&original.sha256===copy.sha256,'Copied Workbench source differs');}
  const policySource=JSON.parse(fs.readFileSync(path.join(__dirname,'PolicySource.actual.json'))),policyCopies=JSON.parse(fs.readFileSync(path.join(directory,'policy-source-pins.json')));
  need(policyCopies.length===policySource.sourceFiles.length,'Policy source copy closed set differs');
  for(const copied of policyCopies){const relative=path.relative(path.join(receipt.workspace,'policy-source'),copied.path),expected=policySource.sourceFiles.find(f=>samePath(path.join(__dirname,'policy-source',f.relative),path.join(__dirname,'policy-source',relative)));need(expected&&copied.bytes===expected.copy.bytes&&copied.sha256===expected.copy.sha256,'Policy source copy differs');}
  const hook=JSON.parse(fs.readFileSync(path.join(directory,'policy-hook-pin.json')));need(hook.bytes===policySource.hook.bytes&&hook.sha256===policySource.hook.sha256,'Copied policy hook differs');
  const expectedMembers=[];const phasePins=[];
  for(const phase of ['configure','build']){
    const folder=path.join(directory,phase),children=fs.readdirSync(folder);
    need(children.length===1&&children[0].startsWith('compiler-lifecycle-'+phase+'-'),'Unexpected lifecycle evidence directory');
    const phaseDir=path.join(folder,children[0]),filename=path.join(phaseDir,'returned.json');
    const record=JSON.parse(fs.readFileSync(filename));
    need(record.schema==='soui.msvc-compiler-lifecycle/v1'&&record.phase===phase&&record.architecture===architecture&&
      record.serverNaturallyExited===true&&record.exitCode===0&&record.nativeExitCode===0&&record.failure===null&&
      record.readinessSignaled===true&&record.readinessIsOwnershipProof===false&&record.preexistingServiceRejected===true&&
      record.compilerHelperSuppressionConfigured===false&&record.originalOwnedJobClosureRequired===true&&
      record.forcedTerminationUsed===false&&record.naturalWaitLimitMs===30000&&
      record.readyEvent==='VCTIP_14.44.35226.0_READY_EVENT'&&isDeepStrictEqual(record.arguments,['-timeout:20'])&&samePath(record.compiler.path,compiler)&&
      samePath(record.image.path,path.win32.join(path.win32.dirname(compiler),'vctip.exe')),'Current helper phase metadata differs');
    const compilerPin=before.find(file=>samePath(file.path,compiler)),serverPin=before.find(file=>samePath(file.path,record.image.path)),configPin=before.find(file=>samePath(file.path,record.image.path+'.config'));
    need(compilerPin?.sha256===record.compiler.sha256&&serverPin?.sha256===record.image.sha256&&serverPin?.bytes===record.image.bytes&&
      configPin?.sha256===record.image.configSha256,'Held lifecycle metadata differs from independent actual tool pins');
    need(rows.some(row=>row.event==='owned-member'&&row.pid===record.pid&&row.creationFileTime===record.creationFileTime&&samePath(row.image,record.image.path)),
      'The held phase server identity is missing from the actual owned-member stream');
    expectedMembers.push({pid:record.pid,creationFileTime:record.creationFileTime,image:record.image.path});
    phasePins.push(...walk(phaseDir).map(pin));
  }
  const tips=rows.filter(row=>row.event==='owned-member'&&typeof row.image==='string'&&path.win32.basename(row.image).toLowerCase()==='vctip.exe');
  need(tips.length>0&&tips.every(row=>expectedMembers.some(expected=>row.pid===expected.pid&&row.creationFileTime===expected.creationFileTime&&samePath(row.image,expected.image))),
    'An additional VCTIP identity was observed in the owned Job');
  const cache=fs.readFileSync(receipt.cache,'utf8'),values=new Map();
  for(const line of cache.split(/\r?\n/)){const match=/^([^#/:][^:]*):[^=]+=(.*)$/.exec(line);if(match){need(!values.has(match[1]),'Duplicate actual cache entry');values.set(match[1],match[2]);}}
  need(values.get('CMAKE_GENERATOR')==='Visual Studio 17 2022'&&values.get('CMAKE_GENERATOR_TOOLSET')==='v143,host=x64,version=14.44.35207'&&
    values.get('CMAKE_SYSTEM_VERSION')==='10.0.19041.0'&&samePath(values.get('CMAKE_GENERATOR_INSTANCE'),invocations.toolchain.Root)&&
    values.get('CMAKE_GENERATOR_PLATFORM')===(architecture==='x86'?'Win32':'x64')+',version=10.0.19041.0','Actual CMake compiler/SDK selection differs');
  need(samePath(values.get('SouiSdk_DIR'),path.join(receipt.installedSdkPrefix,'lib/cmake/SouiSdk')),'CMake did not select exact installed SDK');
  const declaration=fs.readFileSync(receipt.compilerDeclaration,'utf8'),actualCompiler=/^set\(CMAKE_CXX_COMPILER "([^"]+)"\)/m.exec(declaration)?.[1];
  need(samePath(actualCompiler,compiler),'Actual compiler declaration differs from held target compiler');
  const outputs=walk(receipt.workspace).map(pin),exe=fs.readFileSync(receipt.executable);
  need(exe.length>64&&exe.toString('ascii',0,2)==='MZ','Missing compiled executable');
  const pe=exe.readUInt32LE(0x3c);need(exe.toString('ascii',pe,pe+4)==='PE\0\0'&&exe.readUInt16LE(pe+4)===(architecture==='x64'?0x8664:0x14c),'Compiled output PE architecture differs');
  need(outputs.some(file=>file.path.endsWith('.obj'))&&outputs.some(file=>/CL\.command\.1\.tlog$/i.test(file.path)),'Actual compiler object/tracking evidence is missing');
  const project=path.join(receipt.buildDirectory,'SouiWorkbench.vcxproj'),projectText=fs.readFileSync(project,'utf8');
  const cpp=[...projectText.matchAll(/<ClCompile Include="([^"]+)"\s*\/>/g)].map(m=>m[1]);
  const rc=[...projectText.matchAll(/<ResourceCompile Include="([^"]+)"\s*\/>/g)].map(m=>m[1]);
  const expectedCpp=frozen.sourceFiles.filter(f=>f.relative.endsWith('.cpp')).map(f=>path.join(receipt.workspace,'source',f.relative));
  need(cpp.length===4&&expectedCpp.every(f=>cpp.some(p=>samePath(f,p)))&&rc.length===1&&samePath(rc[0],path.join(receipt.workspace,'source/demos/workbench/resources/demo.rc')),'Actual target must contain only four reviewed TU and one resource');
  need(projectText.toLowerCase().includes(('c:\\sr06\\out\\'+architecture+'\\sdk\\release\\soui_zlib.lib').toLowerCase())&&projectText.includes('soui_sdk.lib'),'Actual target must reuse sealed auxiliary zlib and installed SDK');
  save(directory,'application-compile-shape.json',{project:pin(project),cpp,rc,cppTranslationUnits:4,resourceTranslationUnits:1,sdkRebuilt:false,webRebuilt:false,runtimeTargetExecuted:false,oldBuildRelabeled:false});
  let policyExecutable=null;
  if(architecture==='x64'){
    const policyProject=path.join(receipt.buildDirectory,'policy-current/soui_resident_shutdown_policy_tests.vcxproj'),text=fs.readFileSync(policyProject,'utf8');
    const units=[...text.matchAll(/<ClCompile Include="([^"]+)"\s*\/>/g)].map(m=>m[1]);
    need(units.length===1&&samePath(units[0],path.join(receipt.workspace,'policy-source/tests/native/resident_shutdown/policy_tests.cpp')),'Only the reviewed console policy TU may be added');
    policyExecutable=pin(receipt.policyExecutable);const bytes=fs.readFileSync(receipt.policyExecutable),offset=bytes.readUInt32LE(0x3c);
    need(bytes.toString('ascii',offset,offset+4)==='PE\0\0'&&bytes.readUInt16LE(offset+4)===0x8664&&bytes.readUInt16LE(offset+24+68)===3,'Policy executable must be x64 console');
    save(directory,'policy-compile-shape.json',{project:pin(policyProject),units,cppTranslationUnits:1,consoleOnly:true,executable:policyExecutable,executed:false});
  }
  const observationDir=path.join(directory,'copied-observations');fs.mkdirSync(observationDir);
  const selected=outputs.filter(file=>/\.(?:obj|tlog|vcxproj)$/i.test(file.path)||samePath(file.path,receipt.cache)||samePath(file.path,receipt.compilerDeclaration)||samePath(file.path,receipt.executable));
  const copies=selected.map((source,index)=>{const destination=path.join(observationDir,String(index).padStart(3,'0')+'-'+path.win32.basename(source.path));fs.copyFileSync(source.path,destination,fs.constants.COPYFILE_EXCL);const copied=pin(destination);need(copied.sha256===source.sha256&&copied.bytes===source.bytes,'Copied observation differs');return {source,copied};});
  save(directory,'output-pins.json',outputs);save(directory,'observation-copies.json',copies);
  const verdict={schema:'soui.workbench-consumer-build-only-case/v1',architecture,root:capture.ready,terminal:capture.result.terminal,
    genericSupervisionPassed:true,wholeJobNaturallyClosed:true,helperPhases:expectedMembers,phasePins,toolPins:before,
    compiler,selectedWindowsSdk:'10.0.19041.0',compiledExecutable:pin(receipt.executable),policyExecutable,executableExecuted:false,
    scope:'independent current public Workbench consumer configure/build only',installedSdkPrefix:receipt.installedSdkPrefix,oldFailuresReclassified:false,productionQualified:false,releaseReady:false};
  save(directory,'case-verdict.json',verdict);return verdict;
}
async function main(){
  need(process.platform==='win32'&&process.argv.length===3&&process.argv[2]==='ROOT_REVIEWED_CURRENT_WORKBENCH_BUILD_V1','Root reviewed Windows invocation required');
  const frozenFile=path.join(__dirname,'FrozenSource.actual.json'),frozen=JSON.parse(fs.readFileSync(frozenFile));
  const policyFile=path.join(__dirname,'PolicySource.actual.json'),policy=JSON.parse(fs.readFileSync(policyFile));
  need(policy.schema==='soui.current-workbench-two-policy-case-source/v1'&&isDeepStrictEqual(policy.cases,['reload-pending','runtime-pending'])&&!policy.executed,'Exact unexecuted two-case policy source required');
  for(const f of policy.sourceFiles)need(pin(f.source.path).sha256===f.source.sha256&&pin(f.copy.path).sha256===f.copy.sha256,'Policy source changed since freeze');
  need(frozen.schema==='soui.current-workbench-consumer-source-freeze/v1'&&frozen.cppTranslationUnits===4&&frozen.resourceTranslationUnits===1&&!frozen.productExecuted&&!frozen.sdkRebuilt,'Reviewed source freeze required');
  for(const f of frozen.sourceFiles){need(pin(f.source.path).sha256===f.source.sha256&&pin(f.copy.path).sha256===f.copy.sha256,'Source changed since reviewed freeze');}
  need(pin(runner).sha256===expectedRunner&&pin(preparation).sha256===expectedPreparation,'Original sealed runner/preparation changed');
  const readerPath=path.join(legacy,'tools/internal/measurement-validation.cjs');need(pin(readerPath).sha256===expectedLegacyReader,'Original preparation reader changed');
  const prepared=require(readerPath).preparedAt(legacy,'x64');
  need(samePath(prepared.tools.OwnedProcessRunner,runner)&&prepared.report.genericSupervisionPassed===null&&
    prepared.report.supervision==='external-caller-evidence-required','Original preparation scope was relabeled');
  const stamp=crypto.randomUUID(),directory=path.join(__dirname,'attempt-'+stamp);fs.mkdirSync(directory);
  const codeNames=['tools/Build.ps1','tools/internal/Build-ValidationTools.ps1','tools/internal/Msvc-CompilerLifecycle.ps1',
    'tools/internal/Find-Toolchain.ps1','tools/internal/CMake-Configure.ps1','cmake/SouiWindowsSdk.cmake',
    'tools/internal/standard-build.cjs','tools/internal/owned-process.cjs','tools/internal/owned-tail-evidence.cjs',
    'tools/internal/compiler-tail-policy.cjs','tools/internal/sdk-version.cjs','tools/internal/build-evidence.cjs','tools/internal/delivery-identity.cjs'];
  const sealedRoots=[path.join(legacy,'third_party/include'),...['x64','x86'].map(a=>path.join(legacy,'third_party/prebuilt',a,'soui/lib'))];
  const installedRoots=['x64','x86'].map(a=>path.join(legacy,'out/release-install-'+a+'-release'));
  const installedFileSets=[...installedRoots,...sealedRoots].map(root=>({root,files:walk(root)}));
  const files=[__filename,path.join(__dirname,'Invoke-WorkbenchBuild.ps1'),path.join(__dirname,'Freeze-WorkbenchInputs.cjs'),frozenFile,
    ...frozen.sourceFiles.flatMap(f=>[f.source.path,f.copy.path]),policyFile,path.join(__dirname,'Prepare-Policy.cjs'),path.join(__dirname,'PolicyTarget.cmake'),...policy.sourceFiles.flatMap(f=>[f.source.path,f.copy.path]),process.execPath,powershell,runner,preparation,readerPath,
    path.join(legacy,'tools/internal/delivery-identity.cjs'),path.join(legacy,'tools/internal/owned-process.cjs'),
    ...codeNames.map(name=>path.join(repository,name)),...prepared.report.source.files.map(file=>path.join(legacy,file.path)),
    ...['cmake/SouiApplication.cmake','cmake/SouiDevTools.cmake','third_party/sources/zlib/zlib.h','third_party/sources/zlib/zconf.h'].map(p=>path.join(legacy,p)),
    ...['x64','x86'].map(a=>path.join(legacy,'out',a,'sdk/Release/soui_zlib.lib')),...installedFileSets.flatMap(r=>r.files)];
  const sourcePins=[...new Set(files.map(file=>path.resolve(file)))].map(pin),freeze=path.join(directory,'inputs-before.json');
  save(directory,'inputs-before.json',{schema:'soui.workbench-consumer-build-only-inputs/v1',repository,runnerOrigin:legacy,
    originalPreparedAt:prepared.report.preparedAt,originalPreparationGenericSupervisionPassed:null,sourcePins,
    timeoutMs:120000,drainMs:2000,architectures:['x64','x86'],oldFailuresReclassified:false});
  fs.copyFileSync(preparation,path.join(directory,'original-preparation.json'),fs.constants.COPYFILE_EXCL);
  let failure=null;const verdicts=[];
  try{
    const standard=require(path.join(repository,'tools/internal/standard-build.cjs'));
    for(const architecture of ['x64','x86']){
      unchanged(sourcePins);const folder=path.join(directory,architecture);fs.mkdirSync(folder);
      const shortWorkspace='C:\\scw8-'+stamp.replace(/-/g,'').slice(0,8)+'-'+architecture;
      need(!fs.existsSync(shortWorkspace),'Short native workspace already exists');
      const args=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(__dirname,'Invoke-WorkbenchBuild.ps1'),
        '-Architecture',architecture,'-CaseDirectory',folder,'-ShortWorkspace',shortWorkspace,'-FrozenInputs',freeze];
      save(folder,'request.json',{executable:powershell,args,cwd:repository,runner,timeoutMs:120000,drainMs:2000,shortWorkspace});
      const env={...process.env};for(const name of Object.keys(env))if(name.toLowerCase()==='psmodulepath')delete env[name];
      const capture=await standard.capture(folder,'native',powershell,args,{runner,cwd:repository,env,timeoutMs:120000,drainMs:2000});
      verdicts.push(verifyCase(folder,architecture,capture,standard));unchanged(sourcePins);
    }
  }catch(error){failure={message:error.message,stack:error.stack};}
  finally{
    need(isDeepStrictEqual(installedFileSets,[...installedRoots,...sealedRoots].map(root=>({root,files:walk(root)}))),'Read-only installed SDK/dependency membership changed');
    const after=sourcePins.map(expected=>{try{return pin(expected.path);}catch(error){return {path:expected.path,error:error.message};}});
    save(directory,'inputs-after.json',{sourcePins:after});
    if(!isDeepStrictEqual(sourcePins,after)){failure??={message:'Frozen input bytes or metadata changed during the short smoke.'};}
    save(directory,'summary.json',{schema:'soui.workbench-consumer-build-only/v1',passed:failure===null&&verdicts.length===2,
      scope:'independent current Workbench consumer four-TU compile x64 then x86 only',originalRunnerPreparationGenericSupervisionPassed:null,
      verdicts,failure,productExecuted:false,executableExecuted:false,oldFailuresReclassified:false,
      currentFullBuildEntriesExecuted:false,productionQualified:false,releaseReady:false});
  }
  console.log(directory);if(failure)throw Error(failure.message);
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
