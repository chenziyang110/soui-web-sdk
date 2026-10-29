'use strict';
// Enumerates/pins transport inputs only. Does not copy, stage, launch or alter either old suite.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname.replaceAll('\\','/'),sourceRoot='F:/work/soui-issue96';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>fs.readFileSync(p),json=p=>JSON.parse(read(p).toString('utf8'));
const pin=p=>{const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink());const b=read(p);return{path:p.replaceAll('\\','/'),bytes:b.length,sha256:hash(b)}};
const save=(n,v)=>{const p=here+'/'+n;fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});return pin(p)};
const prepared=json(here+'/CandidateStageInputs.approved.json');
const stages=fs.readdirSync(here).filter(name=>/^stage-(x64|x86)-[a-f0-9-]+$/.test(name)).map(name=>json(here+'/'+name+'/Summary.actual.json'));
assert.equal(stages.length,2);assert(stages.every(row=>row.naturalStageClosure.rootExitCode===0&&!row.naturalStageClosure.terminatedOwnedJob));
const sourceSdk=json(sourceRoot+'/out/sdk-x64/build-Release.json');
const protocol=require(sourceRoot+'/tests/acceptance/runner.cjs').settings('reload',3,0,500,20000);
const batch=crypto.randomUUID(),arms=[];
for(const architecture of ['x64','x86']){
 const original=prepared.architectures.find(row=>row.architecture===architecture),stage=stages.find(row=>row.architecture===architecture);assert(original&&stage);
 for(const [arm,suite] of [['baseline',original.baselineSuite],['candidate',stage.answer.actualCandidateSuite]]){
  const build=json(suite+'/build.json');
  assert.equal(build.architecture,architecture);assert.equal(build.version,'0.5.1-preview.1');assert.equal(build.sourceSha256,sourceSdk.source.sha256);
  arms.push({key:architecture+'-'+arm,arm,architecture,suite:suite.replaceAll('\\','/'),sdkBuildId:build.buildId,
   sourceSha256:build.sourceSha256,consumerSource:build.consumerSource,installedSdk:build.installedSdk,
   webSdk:build.webSdk,web:build.web,payload:build.payload,consumerBuildId:build.consumerBuildId,
   executable:pin(suite+'/runtime/SouiAcceptance.exe'),observationOutput:here+'/observations/'+batch+'/'+architecture+'-'+arm,
   buildRecord:pin(suite+'/build.json'),kernelInput:pin(suite+'/kernel-input/node.dll'),
   sourceVersion:build.version,originalCompilationReused:true,newCompilationExecuted:false});
 }
 const pair=arms.filter(row=>row.architecture===architecture);
 for(const field of ['sdkBuildId','sourceSha256'])assert.equal(pair[0][field],pair[1][field]);
 for(const field of ['consumerSource','installedSdk','webSdk','web'])assert.deepEqual(pair[0][field],pair[1][field]);
 assert.deepEqual(pair[0].payload.files.map(row=>row.path),pair[1].payload.files.map(row=>row.path));
 assert.deepEqual(pair[0].payload.files.filter((row,i)=>row.bytes!==pair[1].payload.files[i].bytes||row.sha256!==pair[1].payload.files[i].sha256).map(row=>row.path),['node.dll']);
}
const originalRunner=pin(sourceRoot+'/tests/acceptance/runner.cjs'),supplementalReader=pin(sourceRoot+'/tests/acceptance/message-port-report-reader.cjs');
const controller=pin(sourceRoot+'/tests/acceptance/message-port-ab-controller.cjs');
const subjectRunner=pin('F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe');
assert.equal(subjectRunner.sha256,'1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f');
const executionInput={schema:'soui.current-message-port-ab-inputs/v1',executionEnabled:false,rootIndependentReviewPending:true,
 sourceRoot,sourceSha256:sourceSdk.source.sha256,sourceVersion:'0.5.1-preview.1',protocol,originalRunner,supplementalReader,
 subjectRunner,controller,arms,pins:[originalRunner,supplementalReader,controller,subjectRunner,pin(sourceRoot+'/VERSION'),
  ...arms.flatMap(row=>[row.buildRecord,row.kernelInput,row.executable])],
 requiredExecutionNode:{...prepared.tools.node,path:here+'/tools/node.exe'},
 originalProcessTimeoutMs:214500,originalOwnedTimeoutMs:394500,originalDrainMs:2000,
 scope:'Four finite public-fixture browser arms on the original 0.5.1-preview.1 installed SDK; old0.5 and current0.6 exact DLLs only.',
 currentSdk060Qualified:false,newCompilationExecuted:false,browserBehaviorExecuted:false,
 resourcePassed:false,qualificationPassed:false,releaseReady:false};
const executionDraft=save('BrowserABInputs.draft.json',executionInput);
const files=new Map();
function add(source,destination=source,reason='runtime-reader-input'){
 source=source.replaceAll('\\','/');destination=destination.replaceAll('\\','/');
 assert(destination.startsWith('F:/')&&!destination.includes('/../'));
 const current=pin(source),key=destination.toLowerCase();
 if(files.has(key)){assert.equal(files.get(key).sha256,current.sha256);return;}
 files.set(key,{source,guestPath:destination,bytes:current.bytes,sha256:current.sha256,reason});
}
function tree(directory,reason){
 for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
  assert(!entry.isSymbolicLink(),'No linked input trees');const full=directory+'/'+entry.name;
  if(entry.isDirectory())tree(full,reason);else{assert(entry.isFile());add(full,full,reason);}
 }
}
for(const file of sourceSdk.source.files)add(sourceRoot+'/'+file.path,undefined,'Exact historical SDK source-reader inventory; raw bytes retained independently from normalized source SHA');
for(const architecture of ['x64','x86']){
 const sdk=json(sourceRoot+'/out/sdk-'+architecture+'/build-Release.json');assert.deepEqual(sdk.source,sourceSdk.source);
 add(sourceRoot+'/out/sdk-'+architecture+'/build-Release.json');add(sourceRoot+'/out/sdk-'+architecture+'/CMakeCache.txt');
 for(const file of sdk.library.files)add(sourceRoot+'/out/sdk-'+architecture+'/'+file.path);
 assert.deepEqual(sdk.optionalLibraries,{},'Unexpected optional library read closure');
}
for(const arm of arms){
 for(const name of ['build.json','pending.json','web-build.json','web-typecheck.tsconfig.json','web-typecheck.log','web-build.log','configure.log','compile.log'])add(arm.suite+'/'+name);
 for(const name of ['web-sdk','web-output','consumer-source','kernel-input','install','runtime'])tree(arm.suite+'/'+name,'Exact original runner.verify '+name+' closed tree');
 add(arm.suite+'/consumer/bin/Release/SouiAcceptance.exe');
 const producer=arm.suite+'/shared-stage-producer.json';if(fs.existsSync(producer))add(producer);
}
for(const item of executionInput.pins)add(item.path);
add(executionDraft.path);add(prepared.tools.node.path,executionInput.requiredExecutionNode.path,'Exact pinned Node image relocated as an execution tool; old producer argv/logs unchanged');
const list=[...files.values()].sort((a,b)=>a.guestPath.localeCompare(b.guestPath));
const closure=save('GuestReadClosure.plan.json',{schema:'soui.message-port-guest-read-closure-plan/v1',createdUtc:new Date().toISOString(),
 sourceRoot,sourceVersion:'0.5.1-preview.1',sourceSha256:sourceSdk.source.sha256,sourceFiles:sourceSdk.source.files.length,
 candidateSuites:stages.map(row=>({architecture:row.architecture,suite:row.answer.actualCandidateSuite,summary:pin(row.output+'/Summary.actual.json')})),
 executionInput:executionDraft,files:list,fileCount:list.length,totalRawBytes:list.reduce((sum,row)=>sum+row.bytes,0),
 guestDriveBinding:{requiredDrive:'F:',proposedIsolatedBackingRoot:'C:/SOUI_Issue77_Drive',
  action:'Root GUI operator verifies F: is absent in this task VM, copies drive-root/work/... from read-only ISO, then explicitly launches subst F: C:\\SOUI_Issue77_Drive in the guest UI.',
  preserveOriginalProducerPathsAndTimestamps:true,rewriteOriginalBuildOrLogs:false,neverRunSubstOnHost:true},
 transport:{kind:'read-only ISO then exact byte copy into writable guest task directory',created:false,
  sourceRootsClosedFileSetMustMatch:true,guestMustVerifyEveryRawPin:true,guestMustRunUnmodifiedOriginalRunnerVerifyOnAllFourSuites:true,
  rootMustMintReviewedEnabledExecutionInputThenAddItsExactPinToCarrier:true,
  noConsumerObjectsNodeModulesCompilerOrCmakeExecutionRequiredInGuest:true},
 rootIndependentReviewPending:true,guestMachineIdentityPending:true,browserBehaviorExecuted:false,
 currentSdk060Qualified:false,resourcePassed:false,qualificationPassed:false,releaseReady:false});
console.log(JSON.stringify({executionDraft,closure,fileCount:list.length,totalRawBytes:list.reduce((sum,row)=>sum+row.bytes,0),
 baselineAndCandidateBytesDifferOnlyInKernel:true,candidateStagingCompleted:true,browserBehaviorExecuted:false,releaseReady:false}));
