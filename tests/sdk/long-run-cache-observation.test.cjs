'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const policy=require('../acceptance/long-run-policy.cjs');
const validation=require('../acceptance/long-run-validation.cjs');
const cache=require('../acceptance/native-cache-validation.cjs');
const modulePin={path:'C:\\fixture\\node.dll',bytes:123,sha256:'a'.repeat(64)};
const subject={pid:9,creationFileTime:'123'};
test('SDK private observations require the consumer EXE and complete source/Owned/raw bindings',()=>{
  const owners=require('../acceptance/long-run-owners.cjs');
  const binding=owners.privateBinding({sourceSha256:'a'.repeat(64),payloadSha256:'b'.repeat(64),consumerBuildId:'synthetic',logSha256:'c'.repeat(64)},
    subject,{path:'C:/fixture/SouiAcceptance.exe',bytes:100,sha256:'d'.repeat(64)},
    {ownedLogSha256:'e'.repeat(64),contextSha256:'f'.repeat(64)},true);
  assert.equal(owners.privateBindingValid(binding),true);
  for(const changed of [{...binding,executablePin:modulePin},{...binding,inputsMatched:false},
    {...binding,rawLogSha256:null},{...binding,subject:{pid:9,creationFileTime:'0123'}},
    {...binding,sourceSha256:123},{...binding,consumerBuildId:null}]){
    assert.equal(owners.privateBindingValid(changed),false);
    const out=owners.privateStorageObservations([],changed);
    assert.equal(out.bindingGate,'not-evaluated');assert.equal(out.fullCacheBudget,'not-evaluated');
    assert.equal(out.qualified,false);assert.equal(out.budgets,null);
  }
});
function sample(qpc='110',state='sampled'){
  const dto={...Object.fromEntries(cache.u64Names.map(key=>[key,'0'])),
    schemaVersion:1,scope:cache.scope,state,hasCompletedSample:true,identityBound:true,moduleMatches:true,
    modulePath:modulePin.path,inflight:state==='pending',runtimeRetired:false,structSize:216,abiVersion:1,
    status:1,reserved:0,blinkReserved:0,glyphReserved:0,blinkStatus:1,glyphStatus:1,resourceStatus:1,
    resourceDiscardable:0,processId:'9',processCreationFileTime:'123',engineOwnerThreadId:'10',
    recordingEpoch:'100',initializedQpc:'100',sampleQpc:qpc,qpcFrequency:'1000',
    resourceByteLimit:'1000',resourceEffectiveSingleLimit:'1000'};
  return {event:'sample',phase:'cycle',nativeSdk:{namedCaches:dto,
    captureClock:{valid:true,qpc:'120',frequency:'1000'}}};
}
test('frozen eight-hour argv requests named cache data without changing duration or step limits',()=>{
  for(const mode of ['mixed','session']){
    const config=policy.fixedConfig(mode),args=validation.nativeArgs(config,'C:\\out\\events.ndjson');
    assert.equal(args.filter(x=>x==='--cache-observation=1').length,1);
    assert.ok(args.includes('--duration-ms=28800000'));assert.ok(args.includes('--timeout-ms=20000'));
    assert.equal(policy.policySelection(config).requestFields.longRunScope.namedCacheObservation,cache.scope);
  }
  for(const mode of ['reload','recreate','multiwindow','bridge']){
    const config=policy.fixedConfig(mode);
    assert.equal(validation.nativeArgs(config,'C:\\out\\events.ndjson').length,6);
    assert.equal(policy.policySelection(config).requestFields.longRunScope.namedCacheObservation,'not-requested');
  }
});
test('observed named cache data cannot close the independent resource budget gate',()=>{
  const result=validation.namedCacheObservations([sample()],subject,modulePin);
  assert.equal(result.dataGate,'matched');assert.equal(result.newCompletedSamples,1);
  assert.equal(result.resourceGate,'not-evaluated');assert.equal(result.qualified,false);
});
test('same completed QPC is retained data and never counted as a new completion',()=>{
  const pending=sample('110','pending'),retired=sample('110','unavailable');
  retired.nativeSdk.namedCaches.runtimeRetired=true;
  const result=validation.namedCacheObservations([sample(),sample(),pending,retired],subject,modulePin);
  assert.equal(result.dataGate,'matched');assert.equal(result.newCompletedSamples,1);
  assert.equal(result.repeatedCompletedCopies,3);assert.equal(result.retiredCopies,1);
});
test('missing data, partial components and malformed identity stay explicit',()=>{
  const missing=validation.namedCacheObservations([{event:'sample',phase:'cycle',nativeSdk:{}}],subject,modulePin);
  assert.equal(missing.dataGate,'not-evaluated');assert.equal(missing.missingSamples,1);
  const partial=sample();partial.nativeSdk.namedCaches.glyphStatus=2;
  assert.equal(validation.namedCacheObservations([partial],subject,modulePin).dataGate,'partial');
  const invalid=sample();invalid.nativeSdk.namedCaches.processId='10';
  assert.equal(validation.namedCacheObservations([sample(),invalid],subject,modulePin).dataGate,'invalid');
});
test('sample time regression is invalid even when the captured UI clock advances',()=>{
  const result=validation.namedCacheObservations([sample('115'),sample('110')],subject,modulePin);
  assert.equal(result.dataGate,'invalid');assert.equal(result.invalidSamples,1);
});
test('frozen request rejects a scope that disables the declared eight-hour sampler',()=>{
  const config=policy.fixedConfig('mixed'),runId='00000000-0000-4000-8000-000000000001';
  const build={consumerBuildId:'fixture-build',sourceSha256:'b'.repeat(64),payload:{sha256:'c'.repeat(64)}};
  const request={schema:'soui.acceptance-request/v1',runId,...config,...policy.policySelection(config).requestFields,
    consumerBuildId:build.consumerBuildId,sourceSha256:build.sourceSha256,payloadSha256:build.payload.sha256,
    startedAt:'2026-10-13T00:00:00Z'};
  assert.doesNotThrow(()=>policy.validateFrozenRequest(request,build,'mixed-'+runId));
  request.longRunScope.namedCacheObservation='not-requested';
  assert.throws(()=>policy.validateFrozenRequest(request,build,'mixed-'+runId));
});

test('finite configurations expose a known violation while the full cache budget stays uncalibrated',()=>{
  const valid=validation.namedCacheObservations([sample()],subject,modulePin);
  assert.equal(valid.configurationGate,'passed');assert.equal(valid.cacheBudgetGate,'not-evaluated');
  const bad=sample();bad.nativeSdk.namedCaches.resourceRetainedBytes='1000';
  const out=validation.namedCacheObservations([bad],subject,modulePin);
  assert.equal(out.configurationGate,'failed');assert.equal(out.configurationFailures.length,1);
  assert.equal(out.cacheBudgetGate,'not-evaluated');assert.equal(out.calibratedByteBudgets,null);
});
function cacheHours(){
  const rows=[{event:'cycle.begin',cycle:1,elapsedMs:0}];
  for(let hour=0;hour<8;hour++)for(let i=0;i<30;i++){
    const time=hour*policy.HOUR+60000+i*120000,row=sample(String(time));
    row.elapsedMs=time;row.cycle=hour*30+i+1;row.nativeSdk.captureClock.qpc=String(time+10);rows.push(row);
  }
  return rows;
}
test('fresh completion coverage includes hourly edges and never counts a repeated copy twice',()=>{
  const config=policy.fixedConfig('mixed'),rows=cacheHours();
  const out=validation.namedCacheObservations(rows,subject,modulePin,config);
  assert.equal(out.freshCoverageGate,'passed');assert.equal(out.freshCoverage.cadence.length,8);
  assert.equal(out.freshCoverage.cadence[0].newCompleteCopies,30);assert.equal(out.cacheBudgetGate,'not-evaluated');
  rows[2].nativeSdk.namedCaches.sampleQpc=rows[1].nativeSdk.namedCaches.sampleQpc;
  assert.equal(validation.namedCacheObservations(rows,subject,modulePin,config).freshCoverageGate,'not-evaluated');
});
test('partial or stale completions cannot borrow an OS sample to fill cache coverage',()=>{
  for(const mutate of [row=>row.nativeSdk.namedCaches.glyphStatus=2,
    row=>row.nativeSdk.captureClock.qpc=String(BigInt(row.nativeSdk.namedCaches.sampleQpc)+180001n),
    row=>{row.nativeSdk.namedCaches.state='unavailable';row.nativeSdk.namedCaches.runtimeRetired=true;}]){
    const rows=cacheHours();mutate(rows[1]);
    const out=validation.namedCacheObservations(rows,subject,modulePin,policy.fixedConfig('mixed'));
    assert.equal(out.freshCoverageGate,'not-evaluated');assert.equal(out.cacheBudgetGate,'not-evaluated');
  }
});

test('noneligible points stay diagnostic when a complete independent cadence still exists',()=>{
  const rows=cacheHours(),partial=structuredClone(rows[1]);
  partial.elapsedMs--;partial.nativeSdk.namedCaches.sampleQpc=String(BigInt(partial.nativeSdk.namedCaches.sampleQpc)-1n);
  partial.nativeSdk.namedCaches.glyphStatus=2;rows.splice(1,0,partial);
  const out=validation.namedCacheObservations(rows,subject,modulePin,policy.fixedConfig('mixed'));
  assert.equal(out.configurationPoints[0].configurationGate,'not-evaluated');
  assert.equal(out.configurationGate,'passed');assert.equal(out.freshCoverageGate,'passed');
  assert.equal(out.dataGate,'partial');assert.equal(out.cacheBudgetGate,'not-evaluated');
});

test('sufficient fresh hourly coverage cannot swallow a known contradiction in a separate clock-missing point',()=>{
  const rows=cacheHours(),bad=sample('100000');bad.elapsedMs=100000;bad.cycle=999;
  bad.nativeSdk.namedCaches.structSize=215;delete bad.nativeSdk.captureClock;rows.splice(2,0,bad);
  const out=validation.namedCacheObservations(rows,subject,modulePin,policy.fixedConfig('mixed'));
  assert.equal(out.dataGate,'invalid');assert.equal(out.invalidSamples,1);assert.equal(out.freshCoverageGate,'passed');
  assert.equal(out.configurationGate,'failed');assert.equal(out.configurationFailures.length,1);
  assert.equal(out.cacheBudgetGate,'not-evaluated');
});
