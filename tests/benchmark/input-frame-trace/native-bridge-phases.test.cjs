'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const phases=require('../../../benchmarks/protocol/native-bridge-phases.cjs');
const clone=value=>structuredClone(value);
const env={SOUI_DIAGNOSTIC_BRIDGE_PHASES:'1',SOUI_DIAGNOSTIC_SCRIPT:'0',SOUI_ENGINE_SHARED_TURN_BUDGET_US:'0'};
function canonical(value){if(Array.isArray(value))return '['+value.map(canonical).join(',')+']';if(value&&typeof value==='object')return '{'+Object.keys(value).sort().map(key=>JSON.stringify(key)+':'+canonical(value[key])).join(',')+'}';return JSON.stringify(value);}
function snapshot(tick=100n,n=0n,bucket=0,sum=n){
  return {schema:'soui.miniblink-native-phases/v1',requested:true,enabled:true,status:'ready',usable:true,coherent:true,
    observerIncarnation:'1',qpcFrequency:'1000',captureBeginQpc:String(tick),captureEndQpc:String(tick+1n),publicationSequence:String(n*4n),snapshotAttempts:1,
    faultMask:0,counterSaturated:false,clockInvalid:false,ownerViolation:false,sequenceExhausted:false,closing:false,inclusiveWallTime:true,wholeHostAtomic:false,quiescent:false,
    phases:phases.phaseNames.map(phase=>({phase,started:String(n),completed:String(n),returned:String(n),unwound:'0',inFlight:'0',nestedStarts:'0',sumElapsedQpc:String(sum),
      buckets:Array.from({length:64},(_,i)=>String(i===bucket?n:0n))}))};
}
function diagnostics(value,script=''){return 'document=ready,bridge=ready,native_phases='+canonical(value)+script;}
function source(value){
  const text=diagnostics(value),nativePhaseCache=phases.parseExtension(text,env).summary;
  return {schema:'soui.sdk-performance-identity/v1',backend:'miniblink',architecture:'x64',buildIdentity:'candidate-test',sdkVersion:'0.7.0-preview.1',sdkPackageVersion:'0.7.0',
    optionalFeedback:true,engineBudgetUs:0,requestedFrameRate:60,pageHash:'a'.repeat(64),executableSha256:'b'.repeat(64),nodeSha256:'c'.repeat(64),inputTrace:'scalar',observation:'off',environment:clone(env),
    inputTraceDescriptor:{configured:true,mode:'scalar',processId:42,runIdHigh:'3',runIdLow:'4',processIncarnation:'134000000000000001',qpcFrequency:'1000'},
    initialProcess:{pid:42,creationFileTime:'134000000000000001'},
    nativeHost:{pid:42,creationFileTime:'134000000000000001',sessionGeneration:2,applicationDiagnostics:text,nativePhaseCache}};
}
function boundary(value,phase){
  const begin=BigInt(value.captureEndQpc)+19n;
  const result={schema:'soui.sdk-performance-scalar-boundary/v1',phase,scope:'process-cumulative-live',hostIndex:0,measurementId:1,sessionGeneration:'2',
    captureBeginQpc:String(begin),captureEndQpc:String(begin+1n),qpcFrequency:'1000',sourceIdentity:source(value),snapshotUsable:true,sourceStatus:{oldSourceQualification:'unchanged'}};
  result.nativePhaseCache=phases.boundaryMetadata(result.sourceIdentity,result.captureBeginQpc,result.captureEndQpc,result.qpcFrequency,result.sessionGeneration);return result;
}
function rebind(value){const source=value.sourceIdentity;source.nativeHost.nativePhaseCache=phases.parseExtension(source.nativeHost.applicationDiagnostics,source.environment).summary;
  value.nativePhaseCache=phases.boundaryMetadata(source,value.captureBeginQpc,value.captureEndQpc,value.qpcFrequency,value.sessionGeneration);}

test('fixed extension parsing and wire projection preserve native evidence, off output and script suffix',()=>{
  const original=diagnostics(snapshot(),',script={"old":true}'),parsed=phases.parseExtension(original,env);
  assert.equal(phases.parseExtension(diagnostics(snapshot()),env).summary.usable,true);
  assert.equal(parsed.summary.usable,false);assert.deepEqual(parsed.summary.reasons,['unexpected-script-observation']);
  assert.equal(parsed.raw,canonical(snapshot()));assert.equal(parsed.summary.rawIncluded,false);assert.equal(parsed.summary.phases,undefined);
  const native={applicationDiagnostics:original,nativePhaseCache:parsed.summary};
  const wire=phases.wireSnapshot(native);assert.equal(wire.applicationDiagnostics,'document=ready,bridge=ready,script={"old":true}');assert.equal(native.applicationDiagnostics,original);
  const off={applicationDiagnostics:'document=waiting,script={"old":true}'};
  assert.equal(phases.parseExtension(off.applicationDiagnostics,{SOUI_DIAGNOSTIC_BRIDGE_PHASES:null}).summary,null);assert.deepEqual(phases.wireSnapshot(off),off);
  const missing=phases.parseExtension('document=ready',env);assert.equal(missing.summary.usable,false);assert.deepEqual(missing.summary.reasons,['extension-missing']);
  for(const change of [v=>{v.phases[0].started=0;},v=>{v.phases[0].started='01';},v=>{v.phases[0].started='18446744073709551616';},
    v=>{v.phases[0].buckets.pop();},v=>{v.phases.reverse();},v=>{v.publicationSequence='1';},v=>{v.snapshotAttempts=4;},v=>{v.clockInvalid=true;},v=>{v.extra=true;}]){
    const invalid=snapshot();change(invalid);assert.equal(phases.parseExtension(diagnostics(invalid),env).summary.parseStatus,'invalid');
  }
  for(const raw of ['{broken',canonical(snapshot()).replace('{','{"schema":"duplicate",'),canonical(snapshot())+',native_phases={}', 'x'.repeat(8193)])
    assert.equal(phases.parseExtension('document=ready,native_phases='+raw,env).summary.parseStatus,'invalid');
  const busy=snapshot();Object.assign(busy,{status:'busy',usable:false,coherent:false,publicationSequence:null,phases:null,snapshotAttempts:3});
  const busyResult=phases.parseExtension(diagnostics(busy),env);assert.equal(busyResult.summary.parseStatus,'valid');assert.equal(busyResult.summary.usable,false);assert.equal(busyResult.value.phases,null);
  const fault=snapshot();Object.assign(fault,{status:'unavailable',usable:false,phases:null,faultMask:4,ownerViolation:true});assert.equal(phases.parseExtension(diagnostics(fault),env).summary.usable,false);
  assert.equal(phases.parseExtension(diagnostics(snapshot()),{...env,SOUI_DIAGNOSTIC_SCRIPT:'1'}).summary.usable,false);
  const scriptEnabled=phases.parseExtension(original,{...env,SOUI_DIAGNOSTIC_SCRIPT:'1'});
  assert.equal(scriptEnabled.summary.usable,false);assert(scriptEnabled.summary.reasons.includes('unexpected-script-observation'));
  const scriptBoundary=boundary(snapshot(200n,1n),'end');scriptBoundary.sourceIdentity.nativeHost.applicationDiagnostics+=',script={"old":true}';rebind(scriptBoundary);
  assert.equal(scriptBoundary.nativePhaseCache.qualified,false);
  assert.equal(phases.analyzeBoundaries(boundary(snapshot(),'begin'),scriptBoundary).qualified,false);
  const scriptOnly='document=ready,script={"text":",native_phases=unexpected"}';
  assert.equal(phases.parseExtension(scriptOnly,env).wireDiagnostics,scriptOnly);
});

test('BigInt delta uses real cached capture windows, strict identity/age and bounded percentile accounting',()=>{
  const a=boundary(snapshot(),'begin'),b=boundary(snapshot(200n,100n,3,800n),'end');const original=clone(a);
  const result=phases.analyzeBoundaries(a,b);assert.equal(result.qualified,true);assert.deepEqual(result.phases[0].p95,{bucket:3,upperQpc:'15',inclusive:true});
  assert.equal(result.boundaries[0].metadata.cacheAgeAtBoundaryBeginQpc,'19');assert.equal(result.performanceVerified,false);assert.deepEqual(a,original);
  const large=9007199254741009n,right=boundary(snapshot(200n,large,0,0n),'end');assert.equal(phases.analyzeBoundaries(a,right).phases[0].completed,String(large));
  const highest=boundary(snapshot(200n,1n,63,phases.maximum),'end');assert.equal(phases.analyzeBoundaries(a,highest).phases[0].p99.upperQpc,String(phases.maximum));
  const outstanding=snapshot();outstanding.phases[0].started='1';outstanding.phases[0].inFlight='1';outstanding.publicationSequence='2';
  const completed=snapshot(200n);completed.phases[0]={...completed.phases[0],started:'1',completed:'1',returned:'1',sumElapsedQpc:'1',buckets:['1',...Array(63).fill('0')]};completed.publicationSequence='4';
  const crossed=phases.analyzeBoundaries(boundary(outstanding,'begin'),boundary(completed,'end'));assert.equal(crossed.qualified,true);assert.equal(crossed.phases[0].started,'0');assert.equal(crossed.phases[0].completed,'1');
  for(const mutate of [r=>{r.sourceIdentity.executableSha256='d'.repeat(64);},r=>{r.sourceIdentity.environment.SOUI_ENGINE_SHARED_TURN_BUDGET_US='1000';},
    r=>{r.sourceIdentity.nativeHost.sessionGeneration=3;rebind(r);},r=>{r.nativePhaseCache.cacheAgeAtBoundaryBeginQpc='0';},
    r=>{r.sourceIdentity.nativeHost.nativePhaseCache.publicationSequence='999';},r=>{r.qpcFrequency='2000';rebind(r);},
    r=>{r.captureBeginQpc='50';r.captureEndQpc='51';rebind(r);},r=>{r.captureBeginQpc='9999';r.captureEndQpc='10000';rebind(r);},
    r=>{r.sourceIdentity.inputTraceDescriptor.processIncarnation='134000000000000002';},r=>{delete r.nativePhaseCache;}]){
    const invalid=clone(b);mutate(invalid);assert.equal(phases.analyzeBoundaries(a,invalid).qualified,false);
  }
  const other=snapshot(200n,100n,3,800n);other.observerIncarnation='2';assert.equal(phases.analyzeBoundaries(a,boundary(other,'end')).qualified,false);
  const repeated=boundary(snapshot(),'end');repeated.captureBeginQpc='220';repeated.captureEndQpc='221';rebind(repeated);assert.equal(phases.analyzeBoundaries(a,repeated).qualified,false);
  const unchanged=phases.analyzeBoundaries(a,boundary(snapshot(200n),'end'));assert.equal(unchanged.qualified,true);assert.equal(unchanged.phases[0].p95,null);
  const decreasing=boundary(snapshot(300n,1n,0,1n),'end');assert.equal(phases.analyzeBoundaries({...clone(b),phase:'begin'},decreasing).qualified,false);
  const inconsistentSum=phases.analyzeBoundaries(boundary(snapshot(100n,1n,1,2n),'begin'),boundary(snapshot(200n,2n,1,6n),'end'));
  assert.equal(inconsistentSum.qualified,false);assert.deepEqual(inconsistentSum.reasons,['delta-histogram-sum-range']);
  const nested=snapshot(200n,3n);nested.phases[0].nestedStarts='3';
  const inconsistentNested=phases.analyzeBoundaries(boundary(snapshot(100n,2n),'begin'),boundary(nested,'end'));
  assert.equal(inconsistentNested.qualified,false);assert.deepEqual(inconsistentNested.reasons,['delta-nested-start-accounting']);
});

test('bounded native sidecar retains raw once while repeated page identity carries only summaries',()=>{
  const value=snapshot(100n,64n,0,phases.maximum);
  for(const phase of value.phases)phase.buckets=Array(64).fill('1');
  const raw=canonical(value);assert(Buffer.byteLength(raw)<=phases.extensionLimit);
  const native=boundary(value,'begin');assert.equal(native.sourceIdentity.nativeHost.nativePhaseCache.usable,true);
  const wire=phases.wireSnapshot(native.sourceIdentity.nativeHost);assert(!wire.applicationDiagnostics.includes(',native_phases='));assert(!JSON.stringify(wire).includes('buckets'));
  const page={identity:{native:wire},endIdentity:{native:wire},fixedWork:{records:Array.from({length:1024},(_,i)=>[i,1,2])}};
  assert(Buffer.byteLength(JSON.stringify(page))<60000);
  assert(Buffer.byteLength(JSON.stringify({requestId:'r',sessionId:'s',origin:'app://x',token:'t',method:'benchmark.report',params:{report:page}}))<65536);
  const scalar={stages:Array(31).fill(String(phases.maximum)),counters:Array(16).fill(String(phases.maximum)),durations:Array.from({length:15},()=>Array(64).fill(String(phases.maximum)))};
  const sidecar={...native,sdk:scalar,kernel:scalar};assert(Buffer.byteLength(JSON.stringify(sidecar,null,2))<128*1024);
  assert.equal(sidecar.sourceIdentity.nativeHost.applicationDiagnostics,diagnostics(value));assert.equal(sidecar.snapshotUsable,true);
  assert.deepEqual(sidecar.sourceStatus,{oldSourceQualification:'unchanged'});
  const absent=clone(native);delete absent.sourceIdentity.nativeHost.nativePhaseCache;absent.sourceIdentity.nativeHost.applicationDiagnostics='document=ready';delete absent.nativePhaseCache;
  assert.equal(phases.analyzeBoundaries(absent,{...clone(absent),phase:'end'}).qualified,false);assert.equal(absent.snapshotUsable,true);
});
