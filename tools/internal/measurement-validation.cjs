'use strict';
// Local input/output consistency. Prepared tools and SHA values do not
// authenticate a user who can rewrite the complete source/evidence bundle.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util');
const {launchOwned}=require('./owned-process.cjs');
const root=path.resolve(__dirname,'../..');
const protocolTests=['page-pair','run-traces','owned-trace-subject','pair-preflight','series-summary','measurement-preparation-source','display-calibration','desktop-visual'];
const protocolRuntimeInputs=['benchmarks/protocol/display-calibration.cjs','benchmarks/protocol/desktop-visual.cjs'];
const nativeTests=['process-tree-sampler','run-traces'];
const tailProtocolTests=['owned-tail-evidence-reader','owned-tail-client-boundaries','compiler-tail-policy'];
const tailNativeTests=['owned-process-tail-snapshot','owned-tail-client-native'];
const tailRuntimeInputs=['tools/internal/owned-tail-evidence.cjs','tools/internal/compiler-tail-policy.cjs',
 'tools/internal/standard-build.cjs','tools/internal/build-evidence.cjs','tools/internal/delivery-identity.cjs',
 'tests/benchmark/tail-evidence-fixtures.cjs'];
const names=['OwnedProcessRunner','OwnedProcessAssignmentFixture','ProcessTreeSampler','InputTrace','NativeTraceIdentity',
 'OwnedTailProcessFixture','OwnedTailMutationFixture','OwnedTailApiFixture','DisplayCalibration'];
const sourceNames=['benchmarks/protocol/CMakeLists.txt','benchmarks/protocol/owned-process-runner.cpp',
 'benchmarks/protocol/owned-tail-evidence.cpp','benchmarks/protocol/owned-tail-evidence.h',
 'tests/benchmark/owned-process-assignment-fixture.cpp','benchmarks/protocol/process-tree-sampler.cpp','benchmarks/protocol/input-trace.cpp',
 'benchmarks/protocol/desktop-observer.h','benchmarks/protocol/desktop-observer.cpp',
 'benchmarks/protocol/trace-identity.h','tests/benchmark/native-trace-identity.cpp','tools/internal/CMake-Configure.ps1',
 'cmake/SouiWindowsSdk.cmake','tools/internal/Build-ValidationTools.ps1','tools/internal/measurement-validation.cjs',
 'tests/benchmark/owned-tail-process-fixture.cpp','tests/benchmark/owned-tail-mutation-fixture.cpp','tests/benchmark/owned-tail-api-fixture.cpp',
 'benchmarks/protocol/display-calibration.cpp'];
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const encoded=value=>JSON.stringify(value,null,2)+'\n';
function need(condition,message){if(!condition)throw new Error(message);}
function architecture(value){need(['x86','x64'].includes(value),'Invalid measurement tool architecture');return value;}
function buildDirectory(arch,ownerRoot=root){architecture(arch);return path.join(ownerRoot,'out',arch==='x64'?'benchmark-protocol':'benchmark-protocol-x86');}
function file(name,ownerRoot=root){
 const full=path.resolve(ownerRoot,name);need(path.relative(ownerRoot,full)&&!path.relative(ownerRoot,full).startsWith('..')&&!path.isAbsolute(path.relative(ownerRoot,full)),'Tool path escapes workspace');
 let cursor=full;while(cursor!==ownerRoot){need(!fs.lstatSync(cursor).isSymbolicLink(),'Linked tool/source paths are not allowed');cursor=path.dirname(cursor);}
 need(fs.statSync(full).isFile(),'Required measurement tool/source is not a file');return full;
}
function selectedRequest(arch,options={},ownerRoot=root){
 need(options&&typeof options==='object'&&!Array.isArray(options)&&Object.keys(options).every(key=>key==='toolchainRequest'),'Invalid measurement profile options');
 return options.toolchainRequest===undefined?null:require('./llvm-profile-inputs.cjs').readRequest(options.toolchainRequest,arch,ownerRoot);
}
function fingerprintNames(options={}){
 const selected=options.toolchainRequest!==undefined;
 const extra=selected?['cmake/toolchains/SouiLLVM22.cmake','tools/internal/Find-LlvmToolchain.ps1',
  'tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1',
  'tools/internal/llvm-profile-inputs.cjs','tools/internal/llvm-measurement-observation.cjs',
  'tools/internal/llvm-reference-sdk.cjs','tests/benchmark/llvm-measurement-observation.test.cjs','tests/sdk/llvm-profile-inputs.test.cjs']:[];
 return [...sourceNames,...extra];
}
function fingerprint(options={},ownerRoot=root){
 const files=fingerprintNames(options).map(name=>{const bytes=fs.readFileSync(file(name,ownerRoot));return {path:name,bytes:bytes.length,sha256:hash(bytes)};});
 return {sha256:hash(encoded(files)),files};
}
function binary(name,arch,ownerRoot=root){
 const filename=file(path.join(buildDirectory(arch,ownerRoot),'Release',name+'.exe'),ownerRoot);
 need(fs.statSync(filename).size<=67108864,'Measurement tool exceeds 64 MiB');const bytes=fs.readFileSync(filename);
 need(bytes.length>=64&&bytes.length<=67108864&&bytes.readUInt16LE(0)===0x5a4d,'Invalid bounded measurement tool PE');
 const offset=bytes.readUInt32LE(60);need(offset>=64&&offset+24<=bytes.length&&bytes.readUInt32LE(offset)===0x4550,'Invalid measurement tool PE header');
 const coff=bytes.readUInt16LE(offset+4);need(coff===(arch==='x86'?332:34404),'Measurement tool architecture differs: '+name);
 return {path:path.relative(ownerRoot,filename).replace(/\\/g,'/'),bytes:bytes.length,sha256:hash(bytes),coff};
}
function preparationPath(arch,ownerRoot=root){return path.join(buildDirectory(arch,ownerRoot),'measurement-tools.json');}
function invalidate(arch,options={}){
 const request=selectedRequest(arch,options);
 fs.mkdirSync(buildDirectory(arch),{recursive:true});
 const value={schema:request?'soui.measurement-tool-preparation/v2':'soui.measurement-tool-preparation/v1',architecture:arch,prepared:false,genericSupervisionPassed:null};
 if(request)value.nativeToolchain={profile:request.value.profile,requestFile:request.pin,inputIdentity:request.inputIdentity,bridgeIdentity:request.bridgeIdentity,
  compilerVerified:false,nativeClosureVerified:false};
 fs.writeFileSync(preparationPath(arch),encoded(value));
}
function seal(arch,expectedSourceSha256,options={}){
 const request=selectedRequest(arch,options),source=fingerprint(options);need(/^[a-f0-9]{64}$/.test(expectedSourceSha256||'')&&source.sha256===expectedSourceSha256,'Measurement inputs changed during tool build');
 const directory=buildDirectory(arch),cache=fs.readFileSync(file(path.join(directory,'CMakeCache.txt')),'utf8');
 const home=/^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$/m.exec(cache)?.[1]?.trim();
 need(home&&path.resolve(home).toLowerCase()===path.join(root,'benchmarks/protocol').toLowerCase(),'Validation tools use another source directory');
 const logs=['configure.log','compile.log'].map(name=>{const filename=file(path.join(directory,name)),bytes=fs.readFileSync(filename);return {path:path.relative(root,filename).replace(/\\/g,'/'),bytes:bytes.length,sha256:hash(bytes)};});
 const report={schema:request?'soui.measurement-tool-preparation/v2':'soui.measurement-tool-preparation/v1',architecture:arch,configuration:'Release',prepared:true,
  preparedAt:new Date().toISOString(),source,executables:names.map(name=>({name,...binary(name,arch)})),logs,
  buildExitCode:0,genericSupervisionPassed:null,supervision:'external-caller-evidence-required'};
 if(request){
  const inputs=require('./llvm-profile-inputs.cjs');
  report.nativeToolchain={profile:request.value.profile,requestFile:request.pin,inputIdentity:request.inputIdentity,bridgeIdentity:request.bridgeIdentity,
   observation:require('./llvm-measurement-observation.cjs').observe(request,directory,root),compilerVerified:false,nativeClosureVerified:false,
   abiQualified:false,osQualified:false};
  inputs.verifyPin(request.pin);selectedRequest(arch,options);need(fingerprint(options).sha256===source.sha256,'LLVM measurement inputs changed during observation');
 }
 fs.writeFileSync(preparationPath(arch),encoded(report));return report;
}
function prepared(arch,options={}){
 return preparedAt(root,arch,options);
}
// The owner is data. All readers and helper imports stay in this current module.
function preparedAt(ownerRoot,arch,options={}){
 need(typeof ownerRoot==='string'&&path.isAbsolute(ownerRoot),'Explicit absolute measurement owner required');
 ownerRoot=require('./delivery-identity.cjs').safePath(path.resolve(ownerRoot));
 need(fs.statSync(ownerRoot).isDirectory(),'Measurement owner must be a plain directory');
 const request=selectedRequest(arch,options,ownerRoot);
 const filename=file(preparationPath(arch,ownerRoot),ownerRoot);const report=JSON.parse(fs.readFileSync(filename,'utf8'));
 need(report.schema===(request?'soui.measurement-tool-preparation/v2':'soui.measurement-tool-preparation/v1')&&report.prepared===true&&report.architecture===arch&&report.configuration==='Release'&&
  report.buildExitCode===0&&isDeepStrictEqual(report.source,fingerprint(options,ownerRoot)),'Measurement tools are missing or stale; prepare the selected architecture and explicit profile');
 if(request){
  const inputs=require('./llvm-profile-inputs.cjs'),native=report.nativeToolchain;
  need(native?.profile==='LLVM22MSVC142'&&native.inputIdentity===request.inputIdentity&&native.bridgeIdentity===request.bridgeIdentity&&
   native.compilerVerified===false&&native.nativeClosureVerified===false&&native.abiQualified===false&&native.osQualified===false,'Prepared measurement profile scope differs');
  need(path.resolve(native.requestFile.path)===path.join(buildDirectory(arch,ownerRoot),'llvm-profile-request-Release.json'),'Prepared request is not measurement-owned');
  inputs.verifyPin(native.requestFile);
  const actual=inputs.readRequest(native.requestFile.path,arch,ownerRoot);inputs.sameProfile(request,actual);
  require('./llvm-measurement-observation.cjs').verify(native.observation);
  const configured=inputs.inspectCache(actual,buildDirectory(arch,ownerRoot),path.join(ownerRoot,'benchmarks/protocol'),'Release');
  need(configured.inputIdentity===native.observation.inputIdentity&&configured.target===native.observation.target&&
   configured.cache.sha256===native.observation.cache.sha256,'Prepared measurement configured identity changed');
 }
 need(Array.isArray(report.executables)&&report.executables.length===names.length,'Prepared measurement tools are incomplete');
 const tools={};for(const name of names){const matches=report.executables.filter(item=>item.name===name);need(matches.length===1,'Duplicate/missing prepared measurement tool');
  const actual=binary(name,arch,ownerRoot);need(JSON.stringify(actual)===JSON.stringify(Object.fromEntries(Object.entries(matches[0]).filter(([key])=>key!=='name'))),'Prepared measurement executable changed');
  tools[name]=path.resolve(ownerRoot,actual.path);
 }return {report,tools};
}
function parseTap(text){
 const read=name=>{const values=[...text.matchAll(new RegExp('^# '+name+' (\\d+)\\r?$','gm'))];need(values.length===1,'Missing or ambiguous TAP '+name+' count');return Number(values[0][1]);};
 return Object.fromEntries(['tests','pass','fail','cancelled','skipped','todo'].map(name=>[name,read(name)]));
}
async function runGroup(kind,arch='x64',options={}){
 architecture(arch);need(['protocol','native','tail-protocol','tail-native'].includes(kind),'Unknown validation group');
 const setup=prepared(arch,options),env={...process.env};
 for(const key of ['PROCESS_TREE_SAMPLER_EXE','BENCHMARK_IDENTITY_FIXTURE_EXE','BENCHMARK_INPUT_TRACE_EXE','BENCHMARK_OWNED_RUNNER_EXE','BENCHMARK_OWNED_CLIENT'])delete env[key];
 env.BENCHMARK_OWNED_RUNNER_EXE=setup.tools.OwnedProcessRunner;
 env.BENCHMARK_OWNED_CLIENT=path.join(root,'tools/internal/owned-process.cjs');
 if(kind==='native'){
  env.PROCESS_TREE_SAMPLER_EXE=setup.tools.ProcessTreeSampler;env.BENCHMARK_INPUT_TRACE_EXE=setup.tools.InputTrace;
  env.BENCHMARK_IDENTITY_FIXTURE_EXE=setup.tools.NativeTraceIdentity;
 }
 const folder=path.join(root,'out','measurement-validation',kind+'-'+arch+'-'+crypto.randomUUID());fs.mkdirSync(folder,{recursive:true});
 for(const key of Object.keys(env))if(key.startsWith('SOUI_TAIL_')||key==='SOUI_TEST_TAIL_API_MODE'||key==='SOUI_TEST_TAIL_MUTATION_EXE')delete env[key];
 if(kind==='tail-native'){
  env.SOUI_TAIL_RUNNER=setup.tools.OwnedProcessRunner;env.SOUI_TAIL_FIXTURE=setup.tools.OwnedTailProcessFixture;
  env.SOUI_TAIL_MUTATION_RUNNER=setup.tools.OwnedTailMutationFixture;env.SOUI_TAIL_API_RUNNER=setup.tools.OwnedTailApiFixture;
  env.SOUI_TAIL_BOOTSTRAP_RUNNER=setup.tools.OwnedProcessRunner;env.SOUI_TAIL_BOOTSTRAP_CLIENT=path.join(root,'tools/internal/owned-process.cjs');
  env.SOUI_TAIL_EVIDENCE_DIRECTORY=path.join(folder,'native-fixtures');env.SOUI_TAIL_JS_EVIDENCE_DIRECTORY=path.join(folder,'client-fixtures');
 }
 const groups={protocol:protocolTests,native:nativeTests,'tail-protocol':tailProtocolTests,'tail-native':tailNativeTests};
 const tests=groups[kind].map(name=>'tests/benchmark/'+name+'.test.cjs');
 if(kind==='tail-protocol')tests.push('tests/sdk/standard-build.test.cjs');
 const inputs={schema:'soui.measurement-standard-group-inputs/v1',kind,architecture:arch,
  source:fingerprint(options),preparation:setup.report,tests:tests.map(name=>({path:name,sha256:hash(fs.readFileSync(file(name)))})),
  node:{path:process.execPath,version:process.version,sha256:hash(fs.readFileSync(process.execPath))},
  clientSha256:hash(fs.readFileSync(file('tools/internal/owned-process.cjs'))),
  runtimeInputs:(kind.startsWith('tail-')?tailRuntimeInputs:kind==='protocol'?protocolRuntimeInputs:[])
   .map(name=>({path:name,sha256:hash(fs.readFileSync(file(name)))})),
  env:Object.fromEntries(Object.entries(env).filter(([key])=>key.startsWith('BENCHMARK_')||key.startsWith('SOUI_TAIL_')||key==='PROCESS_TREE_SAMPLER_EXE')),
  noGui:true,correctInputTraceExecuted:false};
 fs.writeFileSync(path.join(folder,'inputs.json'),encoded(inputs));
 const owned=launchOwned(process.execPath,['--test','--test-reporter=tap',...tests],{runner:setup.tools.OwnedProcessRunner,cwd:root,
  report:path.join(folder,'owned.ndjson'),timeoutMs:120000,drainMs:2500,stdout:'pipe',stderr:'pipe',env});
 const stdout=[],stderr=[];let size=0,outputError=null;
 const collect=(target,bytes)=>{size+=bytes.length;if(size>8388608){outputError='Measurement test output exceeds 8 MiB';owned.stop();return;}target.push(bytes);};
 owned.child.stdout.on('data',bytes=>collect(stdout,bytes));owned.child.stderr.on('data',bytes=>collect(stderr,bytes));
 const result=await owned.finished,stdoutBytes=Buffer.concat(stdout),stderrBytes=Buffer.concat(stderr);
 fs.writeFileSync(path.join(folder,'stdout.tap'),stdoutBytes);fs.writeFileSync(path.join(folder,'stderr.bin'),stderrBytes);
 fs.writeFileSync(path.join(folder,'finished.json'),encoded(result));
 need(!outputError,outputError);need(result.passed&&result.closureConfirmed,'Measurement test Job failed: '+folder);
 const counts=parseTap(stdoutBytes.toString('utf8'));const expectedSkip=kind==='protocol'?1:0;
 need(counts.tests>0&&counts.fail===0&&counts.cancelled===0&&counts.todo===0&&counts.skipped===expectedSkip&&counts.pass+counts.skipped===counts.tests,
  'Measurement group omitted or failed expected cases: '+folder);
 need(hash(fs.readFileSync(result.report))===result.reportSha256&&setup.report.source.sha256===fingerprint(options).sha256,'Measurement source or raw evidence changed');
 need(inputs.tests.every(item=>hash(fs.readFileSync(file(item.path)))===item.sha256)&&
  inputs.runtimeInputs.every(item=>hash(fs.readFileSync(file(item.path)))===item.sha256)&&
  hash(fs.readFileSync(file('tools/internal/owned-process.cjs')))===inputs.clientSha256&&
  hash(fs.readFileSync(process.execPath))===inputs.node.sha256,'Measurement tests, client, or Node changed during execution');
 prepared(arch,options);
 const summary={schema:'soui.measurement-standard-group/v1',kind,architecture:arch,passed:true,counts,
  nativeCasesExecuted:kind==='native'||kind==='tail-native',nativeSerializerSkipExpected:kind==='protocol',noGui:true,correctInputTraceExecuted:false,
  report:path.relative(root,result.report).replace(/\\/g,'/'),reportSha256:result.reportSha256,
  genericSupervisionPassed:result.passed,closureConfirmed:result.closureConfirmed,sourceSha256:setup.report.source.sha256};
 fs.writeFileSync(path.join(folder,'summary.json'),encoded(summary));return summary;
}
// Optional current display configuration, deliberately separate from native
// identity tests and from every presentation/performance acceptance gate.
async function runDisplay(arch='x64',options={}){
 architecture(arch);
 const setup=prepared(arch,options),runtimeNames=['benchmarks/protocol/display-calibration.cjs','benchmarks/protocol/owned-trace-subject.cjs'];
 const runtimeInputs=runtimeNames.map(name=>({path:name,sha256:hash(fs.readFileSync(file(name)))}));
 const clientSha256=hash(fs.readFileSync(file('tools/internal/owned-process.cjs')));
 const collector=setup.report.executables.find(item=>item.name==='DisplayCalibration');
 const provider=setup.report.executables.find(item=>item.name==='OwnedProcessRunner');
 const folder=path.join(root,'out','measurement-validation','display-'+arch+'-'+crypto.randomUUID());
 for(let cursor=folder;cursor!==root;cursor=path.dirname(cursor))if(fs.existsSync(cursor))
  need(!fs.lstatSync(cursor).isSymbolicLink()&&fs.statSync(cursor).isDirectory(),'Linked/invalid display evidence directory');
 fs.mkdirSync(folder,{recursive:true});
 const inputs={schema:'soui.display-configuration-standard-inputs/v1',architecture:arch,
  source:fingerprint(options),preparation:setup.report,runtimeInputs,clientSha256,
  executable:setup.tools.DisplayCalibration,args:[],cwd:root,timeoutMs:15000,drainMs:2500,
  collector,provider,node:{path:process.execPath,version:process.version,sha256:hash(fs.readFileSync(process.execPath))},
  explicitConfigurationRequest:true,noGui:true,noEtw:true,noSendInput:true,noTailPolicy:true};
 fs.writeFileSync(path.join(folder,'inputs.json'),encoded(inputs),{flag:'wx'});
 const stdout=[],stderr=[];let size=0,outputError=null,owned,result=null,ready=null,contextSha256=null;
 let persisted=false;
 const report=path.join(folder,'sampler.owned.ndjson');
 const summary={schema:'soui.display-configuration-standard-run/v1',kind:'display-configuration',architecture:arch,
  passed:false,displayConfigurationAccepted:false,physicalPresentation:false,opticalCalibration:false,
  performanceGate:false,releaseReady:false,noGui:true,noEtw:true,noSendInput:true,noTailPolicy:true,
  sourceSha256:inputs.source.sha256,collectorSha256:collector.sha256,
  report:path.relative(root,report).replace(/\\/g,'/'),genericSupervisionPassed:false};
 const preserve=()=>{
  if(!result||persisted)return;persisted=true;
  Object.assign(summary,{rootExitCode:result.exitCode,supervisorExitCode:result.supervisorExitCode,
   genericSupervisionPassed:result.passed,closureConfirmed:result.closureConfirmed,reportSha256:result.reportSha256});
  fs.writeFileSync(path.join(folder,'sampler.stdout.log'),Buffer.concat(stdout),{flag:'wx'});
  fs.writeFileSync(path.join(folder,'sampler.stderr.log'),Buffer.concat(stderr),{flag:'wx'});
  fs.writeFileSync(path.join(folder,'finished.json'),encoded(result),{flag:'wx'});
 };
 try{
  owned=launchOwned(setup.tools.DisplayCalibration,[],{runner:setup.tools.OwnedProcessRunner,cwd:root,
   report,timeoutMs:inputs.timeoutMs,drainMs:inputs.drainMs,stdout:'pipe',stderr:'pipe'});
  const collect=(target,bytes)=>{size+=bytes.length;if(size>2097152){outputError='Display output exceeds 2 MiB';owned.stop();return;}target.push(bytes);};
  owned.child.stdout.on('data',bytes=>collect(stdout,bytes));owned.child.stderr.on('data',bytes=>collect(stderr,bytes));
  contextSha256=hash(fs.readFileSync(report+'.context.json'));
  try{ready=await owned.ready;}catch(error){outputError??=error.message;}
  result=await owned.finished;
  preserve();
  const stdoutBytes=Buffer.concat(stdout),stderrBytes=Buffer.concat(stderr);
  need(!outputError,outputError);
  need(setup.report.source.sha256===fingerprint(options).sha256&&
   runtimeInputs.every(item=>hash(fs.readFileSync(file(item.path)))===item.sha256)&&
   hash(fs.readFileSync(file('tools/internal/owned-process.cjs')))===clientSha256&&
   hash(fs.readFileSync(process.execPath))===inputs.node.sha256,'Display source/reader/client/Node changed during execution');
  prepared(arch,options);
  const {readDisplayCalibration}=require(file('benchmarks/protocol/display-calibration.cjs'));
  // Nonzero native status never receives a bound subject or an acceptance.
  // Retain a strictly parsed but unbound diagnostic before returning failure.
  if(!result.passed){
   try{const diagnostic=readDisplayCalibration(stdoutBytes);
    summary.diagnostic={status:diagnostic.status,reason:diagnostic.reason,exactCollectorBound:false};
   }catch(error){summary.diagnosticError=error.message;}
   throw new Error('Display collector unavailable or failed; original Job status retained');
  }
  const provenance=require(file('benchmarks/protocol/owned-trace-subject.cjs'));
  // "sampler" is the existing generic collector evidence role, not a claim
  // that the ProcessTreeSampler or a three-process benchmark ran here.
  const item={name:'sampler',pid:result.pid,code:result.exitCode,signal:result.signal,error:null,
   contextSha256,ownedProcess:result};
  const evidence=provenance.captureOwnedEvidence(folder,[item])[0];
  need(evidence.stdout.sha256===hash(stdoutBytes)&&evidence.stderr.sha256===hash(stderrBytes),
   'Display raw files differ from the actual captured output bytes');
  const verified=provenance.verifyOwnedProcess(folder,'sampler',item,evidence,
   {runnerSha256:provider.sha256,executableSha256:collector.sha256,executable:setup.tools.DisplayCalibration,args:[],cwd:root});
  need(verified.naturalCompletion&&verified.genericPassed&&ready&&
   ready.pid===verified.subject.pid&&ready.creationFileTime===verified.subject.creationFileTime,
   'Display collector did not naturally complete with its held native identity');
  const verdict=readDisplayCalibration(stdoutBytes,{ownedSubject:verified.subject});
  need(verdict.boundConfigurationAccepted===true,'Display configuration is not available and bound');
  need(inputs.source.sha256===fingerprint(options).sha256&&
   runtimeInputs.every(item=>hash(fs.readFileSync(file(item.path)))===item.sha256)&&
   hash(fs.readFileSync(file('tools/internal/owned-process.cjs')))===clientSha256&&
   hash(fs.readFileSync(process.execPath))===inputs.node.sha256,'Display validation inputs changed before acceptance');
  prepared(arch,options);
  Object.assign(summary,{passed:true,displayConfigurationAccepted:true,verdict,
   ownedSubject:verified.subject,ownedEvidence:evidence,process:item});
 }catch(error){summary.error=error.message;
  // Even a context/read/setup failure after launch owns the held process until
  // its native terminal. STOP is failure cleanup and never configuration success.
  if(owned&&!result){owned.stop();result=await owned.finished;}
  preserve();
 }
 fs.writeFileSync(path.join(folder,'summary.json'),encoded(summary),{flag:'wx'});
 need(summary.passed,'Display configuration was not accepted: '+folder+(summary.error?' ('+summary.error+')':''));
 return summary;
}
module.exports={buildDirectory,fingerprint,fingerprintNames,prepared,preparedAt,runGroup,runDisplay};
if(require.main===module)(async()=>{
 const args=process.argv.slice(2);let options={};const selected=args.indexOf('--toolchain-request');
 if(selected!==-1){need(selected===args.length-2&&path.isAbsolute(args[selected+1]),'Explicit absolute measurement request required');options={toolchainRequest:args[selected+1]};args.splice(selected,2);}
 const [command,arch='x64',expected,...rest]=args;architecture(arch);need(rest.length===0,'Unexpected measurement arguments');let report;
 if(command==='source'&&expected===undefined){selectedRequest(arch,options);report=fingerprint(options);}
 else if(command==='prepared'&&expected===undefined){const value=prepared(arch,options);report={prepared:true,runner:value.tools.OwnedProcessRunner,
  sourceSha256:value.report.source.sha256,requestFile:value.report.nativeToolchain?.requestFile||null,scope:'existing current preparation; no new compiler execution'};}
 else if(command==='invalidate'&&expected===undefined){invalidate(arch,options);report={invalidated:true,architecture:arch};}
 else if(command==='seal'&&expected!==undefined)report=seal(arch,expected,options);
 else if(['protocol','native','tail-protocol','tail-native'].includes(command)&&expected===undefined)report=await runGroup(command,arch,options);
 else if(command==='display'&&expected===undefined)report=await runDisplay(arch,options);
 else throw Error('Usage: measurement-validation.cjs source|invalidate|seal|protocol|native|tail-protocol|tail-native|display [x86|x64] [seal source SHA256]');
 console.log(encoded(report));
})().catch(error=>{console.error(error.message);process.exitCode=1});
