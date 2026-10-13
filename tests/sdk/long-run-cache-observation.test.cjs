'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const policy=require('../acceptance/long-run-policy.cjs');
const validation=require('../acceptance/long-run-validation.cjs');
const cache=require('../acceptance/native-cache-validation.cjs');
const modulePin={path:'C:\\fixture\\node.dll',bytes:123,sha256:'a'.repeat(64)};
const subject={pid:9,creationFileTime:'123'};
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
