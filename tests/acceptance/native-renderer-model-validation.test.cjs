'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {u64Names}=require('./native-renderer-validation.cjs');
const {analyzeNativeModel,countNames,scope}=require('./native-renderer-model-validation.cjs');
const {completeResourceAnalysis}=require('./resource-validation.cjs');
const {completeResourceCaptureResult}=require('./runner.cjs');
// DTO-only logical regressions; these are not Native/Owned/file measurements.
const subject={pid:9,creationFileTime:'123',entireJobProcesses:1,naturalClosure:true,forcedClosure:false,
  nativeExitCode:0,supervisorExitCode:0};
const modulePin={path:'C:\\fixture-only\\runtime\\node.dll',bytes:1234,sha256:'a'.repeat(64)};
function fixture() {
  const raw={schemaVersion:1,scope:'bound-dll-completed-compositor-calls',state:'ready',
    modulePath:modulePin.path,moduleMatches:true,identityBound:true,terminalSnapshot:false,
    structSize:168,abiVersion:1,status:1,flags:0x27,lastDrawKind:1,reserved:0,
    ...Object.fromEntries(u64Names.map(name=>[name,'0'])),processId:'9',processCreationFileTime:'123',
    engineOwnerThreadId:'10',qpcFrequency:'1000',recordingEpoch:'100',initializedQpc:'100',
    renderersCreatedTotal:'1',renderersLive:'1',rasterDrawsTotal:'1',drawEventsTotal:'1',
    lastDrawQpc:'110',lastDrawThreadId:'11',lastDrawProcessId:'9'};
  const clock=qpc=>({valid:true,qpc:String(qpc),frequency:'1000',captureThreadId:'17',phase:'synchronous-native-read-complete'});
  const model=value=>({schemaVersion:2,scope,state:value.state,modulePath:modulePin.path,moduleMatches:true,
    identityBound:true,terminalSnapshot:value.terminalSnapshot,structSize:264,abiVersion:2,status:value.status,
    flags:19,providerId:1,backendId:1,compiledGpuSupport:0,factoryCoverageMask:127,
    backendInitializationQpc:'90',...Object.fromEntries(countNames.map(name=>[name,'0'])),compositor:structuredClone(value)});
  const sample=(phase,cycle,value,qpc)=>({event:'sample',phase,cycle,nativeSdk:{pid:'9',creationFileTime:'123',
    captureThreadId:'17',captureClock:clock(qpc),renderer:structuredClone(value),rendererModel:model(value)}});
  const step=(action,value,qpc)=>({event:'step.complete',action,nativeRenderer:{renderer:structuredClone(value),
    model:model(value),captureClock:clock(qpc)}});
  const rows=[sample('mounted',0,raw,120)];
  for(let cycle=1;cycle<=110;cycle++)rows.push(sample('cycle',cycle,{...raw,rasterDrawsTotal:String(cycle+1),
    drawEventsTotal:String(cycle+1),lastDrawQpc:String(110+cycle)},120+cycle));
  const closed={...rows.at(-1).nativeSdk.renderer,state:'closed',status:4,renderersLive:'0',renderersClosedTotal:'1',
    lastRendererCloseQpc:'240',closedSnapshotQpc:'240'};
  rows.push(step('close',closed,250),sample('closed',111,closed,260));
  const terminal={...closed,terminalSnapshot:true,flags:closed.flags|8,engineFinalizedQpc:'270'};
  rows.push(step('runtime.shutdown',terminal,280),sample('runtime-closed',111,terminal,290),
    step('runtime.shutdown-idempotent',terminal,300));return rows;
}
const modelAt=(rows,index=1)=>rows[index].nativeSdk.rendererModel;
const analyze=rows=>analyzeNativeModel(rows,subject,modulePin);
const complete={inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',nativeScopeObserved:false,
  nativeToolchainProfile:'MSVC143',loadedPayloadModule:modulePin};
const analysis=(gate='passed')=>({resourceGate:gate,passed:gate==='passed',releaseReady:false,m0Complete:false,subject,
  sdkPrivateCacheQueueGate:'not-evaluated',thousandCycleOperationGate:'not-evaluated',
  eightHourGate:'not-evaluated',targetOsGate:'not-evaluated',failure:gate==='failed'?'private-byte-growth':undefined});
test('complete v2 frozen workload observations enable only original finite resource rules',()=>{
  const rows=fixture(),observed=analyze(rows);assert.equal(observed.softwareWorkloadGate,'matched');
  assert.equal(observed.qualified,false);assert.equal(observed.observations,116);
  const result=completeResourceAnalysis(analysis(),complete,rows);
  assert.equal(result.resourceGate,'passed');assert.equal(result.passed,true);
  assert.equal(result.softwareProcessModelGate,'matched-bound-workload');
  assert.equal(result.m0Complete,false);assert.equal(result.eightHourGate,'not-evaluated');assert.equal(result.releaseReady,false);
});
test('old v1, busy, missing, unknown coverage or unsupported model cannot evaluate the total gate',()=>{
  for(const edit of [v=>{v.state='busy';},v=>{v.structSize=168;},v=>{v.abiVersion=1;},
    v=>{v.factoryCoverageMask=63;},v=>{v.compiledGpuSupport=4294967295;},v=>{v.flags=3;}]) {
    const rows=fixture();edit(modelAt(rows));assert.equal(analyze(rows).softwareWorkloadGate,'not-evaluated');
    assert.equal(completeResourceAnalysis(analysis(),complete,rows).resourceGate,'not-evaluated');
  }
  const rows=fixture();delete rows[1].nativeSdk.rendererModel;
  assert.equal(completeResourceAnalysis(analysis(),complete,rows).passed,false);
});
test('every additional factory and compiled GPU support conservatively keep software workload unqualified',()=>{
  for(const name of countNames) {
    const rows=fixture();modelAt(rows)[name]='1';
    assert.equal(analyze(rows).softwareWorkloadGate,'not-evaluated',name);
  }
  const rows=fixture();modelAt(rows).compiledGpuSupport=1;assert.equal(analyze(rows).softwareWorkloadGate,'not-evaluated');
});
test('native module identity, backend epoch, owner and nested draw history changes never match',()=>{
  for(const edit of [v=>{v.modulePath='C:\\other\\node.dll';},v=>{v.backendInitializationQpc='91';},
    v=>{v.compositor.engineOwnerThreadId='12';},v=>{v.compositor.rasterDrawsTotal=v.compositor.drawEventsTotal='1';}]) {
    const rows=fixture();edit(modelAt(rows,2));assert.equal(analyze(rows).softwareWorkloadGate,'failed');
  }
  for(const value of ['09',9,'18446744073709551616']) {
    const rows=fixture();modelAt(rows).backendInitializationQpc=value;
    assert.notEqual(analyze(rows).softwareWorkloadGate,'matched');
  }
});
test('single-process natural original Owned scope and unchanged terminal model are mandatory',()=>{
  for(const changed of [{entireJobProcesses:2},{naturalClosure:false},{forcedClosure:true},{nativeExitCode:1},{supervisorExitCode:1}])
    assert.equal(analyzeNativeModel(fixture(),{...subject,...changed},modulePin).softwareWorkloadGate,'not-evaluated');
  const rows=fixture();rows.at(-1).nativeRenderer.model.backendInitializationQpc='91';
  assert.equal(analyze(rows).softwareWorkloadGate,'failed');
});
test('actual original trend failure remains failed even with a matched Native workload',()=>{
  const result=completeResourceAnalysis(analysis('failed'),complete,fixture());
  assert.equal(result.resourceGate,'failed');assert.equal(result.passed,false);assert.equal(result.failure,'private-byte-growth');
});
test('final phase failures cannot promote an earlier matched workload or resource DTO',()=>{
  const record=completeResourceAnalysis(analysis(),complete,fixture());
  const inputs={inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',closureObserved:false,
    resourceGate:'not-evaluated',passed:false,nativeScopeGate:'not-observed',releaseReady:false};
  const operation={schema:'soui.acceptance-run/v1',passed:true,runId:'fixture-only',config:{}};
  const evidence={inputs,finalVerified:true,failure:null,analysis:{record,file:null},bindingFile:null};
  const result=completeResourceCaptureResult(operation,null,null,evidence);
  assert.equal(result.resourceGate,'passed');assert.equal(result.passed,true);assert.equal(result.releaseReady,false);
  for(const phase of ['first-recheck','post-reader-pin','second-recheck','final-pin','analysis-receipt']) {
    const failed=completeResourceCaptureResult(operation,null,null,{...evidence,finalVerified:false,failure:phase});
    assert.equal(failed.resourceGate,'not-evaluated');assert.equal(failed.passed,false);assert.equal(failed.nativeModel,null);
    assert.ok(failed.requiredEvidence.includes('complete-current-source-and-consumer-input-bindings'));
  }
  for(const change of [{m0Complete:true},{releaseReady:true},{eightHourGate:'passed'},{failure:'retained analysis failure'},
    {nativeModel:{...record.nativeModel,scope:'all-process-GPU',qualified:true}}])
    assert.throws(()=>completeResourceCaptureResult(operation,null,null,{...evidence,analysis:{record:{...record,...change}}}));
  const operationFailure=completeResourceCaptureResult({...operation,failure:'retained operation failure'},null,null,evidence);
  assert.equal(operationFailure.resourceGate,'not-evaluated');assert.equal(operationFailure.passed,false);
});

test('public classification rejects contradictory or missing original resource rule observations',()=>{
  const record=completeResourceAnalysis(analysis(),complete,fixture());
  const inputs={inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',closureObserved:false,
    resourceGate:'not-evaluated',passed:false,nativeScopeGate:'not-observed',releaseReady:false};
  const operation={schema:'soui.acceptance-run/v1',passed:true,runId:'fixture-only',config:{}};
  const classify=value=>completeResourceCaptureResult(operation,null,null,{inputs,finalVerified:true,failure:null,
    analysis:{record:value,file:null},bindingFile:null});
  assert.equal(classify(record).resourceGate,'passed');
  for(const original of ['failed','not-evaluated'])
    assert.throws(()=>classify({...record,resourceRuleObservation:original}));
  const missing={...record};delete missing.resourceRuleObservation;assert.throws(()=>classify(missing));
  for(const changed of [{resourceGate:'failed',passed:false},{resourceGate:'not-evaluated',passed:false},
    {passed:false},{resourceGate:'not-evaluated',resourceRuleObservation:'not-evaluated',passed:true}])
    assert.throws(()=>classify({...record,...changed}));
  const failed=completeResourceAnalysis(analysis('failed'),complete,fixture());
  assert.equal(classify(failed).resourceGate,'failed');assert.equal(classify(failed).passed,false);
  assert.throws(()=>classify({...failed,resourceRuleObservation:'passed'}));
});
