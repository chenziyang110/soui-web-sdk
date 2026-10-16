'use strict';
// DTO-only scalar validation. Supplied file pins are external evidence; this
// function neither inspects modules nor certifies a DLL or resource outcome.
const path=require('node:path');
const scope='named-process-cache-scalars';
const u64Names=['processId','processCreationFileTime','engineOwnerThreadId','recordingEpoch',
  'initializedQpc','sampleQpc','qpcFrequency','blinkLiveBytes','blinkDeadBytes','blinkCapacity',
  'blinkMinDeadCapacity','blinkMaxDeadCapacity','glyphAttachedBytes','glyphAttachedCount',
  'glyphByteLimit','glyphCountLimit','resourceRetainedBytes','resourceRetainedCount',
  'resourceByteLimit','resourceCountLimit','resourceRawSingleLimit','resourceEffectiveSingleLimit'];
const uncovered=['Blink entries/decoded allocation heap','detached glyph strikes','TLS caches',
  'other Skia instances','externally held evicted image data','all font/image heap','V8 compile cache','allocator overhead'];
function decimal(value) {
  if(typeof value!=='string'||!/^(0|[1-9][0-9]*)$/.test(value)||value.length>20||BigInt(value)>18446744073709551615n)
    throw new Error('Expected canonical uint64 decimal');
  return BigInt(value);
}
function analyzeNamedCache(value,{subject,modulePin,captureClock,previous}={}) {
  const result={scope,dataGate:'not-evaluated',components:{},uncovered:[...uncovered],
    resourceGate:'not-evaluated',qualified:false,passed:false};
  try {
    if(!value||value.schemaVersion!==1||value.scope!==scope)throw new Error('Invalid cache DTO');
    if(!['disabled','not_started','pending','busy','unavailable','sampled'].includes(value.state))throw new Error('Invalid state');
    for(const flag of ['hasCompletedSample','inflight','runtimeRetired'])
      if(typeof value[flag]!=='boolean')throw new Error('Invalid completion flag');
    if((value.state==='sampled'&&(!value.hasCompletedSample||value.inflight||value.runtimeRetired))||
        (value.hasCompletedSample&&['disabled','not_started'].includes(value.state)))throw new Error('Contradictory completion state');
    if(!value.hasCompletedSample)return result;
    if(value.structSize!==216||value.abiVersion!==1||value.status!==1||value.reserved!==0||value.blinkReserved!==0||value.glyphReserved!==0)
      throw new Error('Invalid ABI envelope');
    if(value.identityBound!==true||value.moduleMatches!==true||!modulePin||
        !Number.isSafeInteger(modulePin.bytes)||modulePin.bytes<=0||!/^[0-9a-f]{64}$/.test(modulePin.sha256)||
        typeof value.modulePath!=='string'||!path.win32.isAbsolute(modulePin.path)||
        path.win32.normalize(value.modulePath).toLowerCase()!==path.win32.normalize(modulePin.path).toLowerCase())
      throw new Error('Missing external module binding');
    const n=Object.fromEntries(u64Names.map(name=>[name,decimal(value[name])]));
    if(!subject||n.processId!==decimal(String(subject.pid))||!n.processId||
        n.processCreationFileTime!==decimal(subject.creationFileTime)||!n.processCreationFileTime||
        !n.engineOwnerThreadId||!n.qpcFrequency||!n.recordingEpoch||n.recordingEpoch!==n.initializedQpc||
        !captureClock||captureClock.valid!==true||decimal(captureClock.frequency)!==n.qpcFrequency||
        n.sampleQpc<n.initializedQpc||n.sampleQpc>decimal(captureClock.qpc))throw new Error('Identity/clock mismatch');
    if(previous){
      for(const key of ['processId','processCreationFileTime','engineOwnerThreadId','recordingEpoch','initializedQpc','qpcFrequency'])
        if(value[key]!==previous[key])throw new Error('Epoch identity changed');
      if(n.sampleQpc<decimal(previous.sampleQpc))throw new Error('Sample time regressed');
    }
    for(const name of ['blink','glyph','resource']){
      const status=value[`${name}Status`];
      if(!Number.isInteger(status)||status<0||status>3)throw new Error('Invalid component state');
      result.components[name]=['not_started','ready','busy','unavailable'][status];
    }
    if(value.resourceDiscardable!==0&&value.resourceDiscardable!==1)throw new Error('Invalid discardable mode');
    if(value.blinkStatus===1&&(n.blinkMinDeadCapacity>n.blinkMaxDeadCapacity||n.blinkMaxDeadCapacity>n.blinkCapacity))
      throw new Error('Invalid Blink prune policy');
    if(value.resourceStatus===1){
      const raw=n.resourceRawSingleLimit,total=n.resourceByteLimit;
      const effective=value.resourceDiscardable?raw:(raw===0n?total:(raw<total?raw:total));
      if(n.resourceEffectiveSingleLimit!==effective)throw new Error('Invalid single-allocation policy');
      result.resourcePolicy={byteBudget:value.resourceDiscardable&&total===0n?'absent':'retained-record-purge-threshold',
        singleAllocationCap:value.resourceDiscardable&&raw===0n?'not-explicit':'current-configured-limit'};
    }
    result.dataGate=Object.values(result.components).every(state=>state==='ready')?'matched':'partial';
    result.retainedSnapshot=value.runtimeRetired===true;
    result.freshRequestCompleted=value.state==='sampled'&&value.inflight===false&&!value.runtimeRetired;
    // Usage may exceed a soft threshold; sizes/limits may legitimately decrease.
    return result;
  }catch(error){result.dataGate='invalid';result.failure=error.message;return result;}
}
const CONFIGURATION_POLICY=Object.freeze({schema:'soui.named-cache-configuration-policy/v1',
  maxCompletedCopyAgeMs:180000,minNewSamplesPerHour:30,maxNewSampleGapMs:180000,
  calibratedByteBudgets:null,broadCacheCoverage:false});
function target(usage,limit,limitKind){
  return {usage:usage.toString(),limit:limit.toString(),limitKind,
    comparison:['absent','not-a-count-budget'].includes(limitKind)?'not-evaluated':usage<limit?'under':usage===limit?'equal':'over'};
}
// Independent finite checks; the original scalar/data API above stays unchanged.
function analyzeNamedCacheConfiguration(value,bindings={}){
  const result={scope,configurationGate:'not-evaluated',freshnessGate:'not-evaluated',
    targets:{},calibratedByteBudgetGate:'not-evaluated',cacheBudgetGate:'not-evaluated',
    calibratedByteBudgets:null,broadCacheCoverage:false,qualified:false,releaseReady:false};
  if(value===null||value===undefined)return result;
  if(value.schemaVersion!==1||value.scope!==scope||value.abiVersion!==undefined&&value.abiVersion!==1)
    return {...result,dataGate:'not-evaluated',failure:'unsupported-cache-envelope'};
  // Known current-envelope contradictions do not depend on an external clock
  // or module pin. Their failure must survive a separately missing binding.
  let n;
  try{
    if(!['disabled','not_started','pending','busy','unavailable','sampled'].includes(value.state))throw Error('Invalid state');
    for(const flag of ['hasCompletedSample','inflight','runtimeRetired'])
      if(typeof value[flag]!=='boolean')throw Error('Invalid completion flag');
    if((value.state==='sampled'&&(!value.hasCompletedSample||value.inflight||value.runtimeRetired))||
      (value.hasCompletedSample&&['disabled','not_started'].includes(value.state)))throw Error('Contradictory completion state');
    if(value.hasCompletedSample){
      if(value.structSize!==216||value.abiVersion!==1||value.status!==1||value.reserved!==0||value.blinkReserved!==0||value.glyphReserved!==0)
        throw Error('Invalid ABI envelope');
      n=Object.fromEntries(u64Names.map(name=>[name,decimal(value[name])]));
      if(!n.processId||!n.processCreationFileTime||!n.engineOwnerThreadId||!n.qpcFrequency||!n.recordingEpoch||
        n.recordingEpoch!==n.initializedQpc||n.sampleQpc<n.initializedQpc)throw Error('Invalid intrinsic identity/clock');
      for(const name of ['blink','glyph','resource'])
        if(!Number.isInteger(value[`${name}Status`])||value[`${name}Status`]<0||value[`${name}Status`]>3)
          throw Error('Invalid component state');
      if(value.resourceDiscardable!==0&&value.resourceDiscardable!==1)throw Error('Invalid discardable mode');
      if(value.blinkStatus===1&&(n.blinkMinDeadCapacity>n.blinkMaxDeadCapacity||n.blinkMaxDeadCapacity>n.blinkCapacity))
        throw Error('Invalid Blink prune policy');
      if(value.resourceStatus===1){
        const raw=n.resourceRawSingleLimit,total=n.resourceByteLimit;
        const effective=value.resourceDiscardable?raw:(raw===0n?total:(raw<total?raw:total));
        if(n.resourceEffectiveSingleLimit!==effective)throw Error('Invalid single-allocation policy');
      }
    }
  }catch(error){return {...result,dataGate:'invalid',configurationGate:'failed',failure:error.message};}
  if(!value.hasCompletedSample)return result;
  if(value.blinkStatus===1)result.targets.blink=target(n.blinkLiveBytes+n.blinkDeadBytes,n.blinkCapacity,'soft-prune-target');
  if(value.glyphStatus===1){
    result.targets.glyphBytes=target(n.glyphAttachedBytes,n.glyphByteLimit,'attached-purge-target');
    result.targets.glyphCount=target(n.glyphAttachedCount,n.glyphCountLimit,'attached-purge-target');
  }
  let failure=null;
  if(value.resourceStatus===1){
    const discardable=value.resourceDiscardable===1;
    result.targets.resourceBytes=target(n.resourceRetainedBytes,n.resourceByteLimit,discardable&&n.resourceByteLimit===0n?'absent':'retained-purge-threshold');
    result.targets.resourceCount=target(n.resourceRetainedCount,n.resourceCountLimit,discardable?'retained-purge-threshold':'not-a-count-budget');
    result.singleAllocationPolicy={raw:n.resourceRawSingleLimit.toString(),effective:n.resourceEffectiveSingleLimit.toString(),
      explicit:!(discardable&&n.resourceRawSingleLimit===0n),allocationUsageGate:'not-evaluated'};
    if(n.resourceRetainedCount===0n&&n.resourceRetainedBytes!==0n)failure='empty-resource-records-have-nonzero-bytes';
    const countExceeded=discardable&&(n.resourceCountLimit===0n
      ? n.resourceRetainedCount!==0n||n.resourceRetainedBytes!==0n : n.resourceRetainedCount>=n.resourceCountLimit);
    const byteExceeded=n.resourceByteLimit===0n
      ? !discardable&&(n.resourceRetainedCount!==0n||n.resourceRetainedBytes!==0n)
      : n.resourceRetainedBytes>=n.resourceByteLimit;
    if(countExceeded||byteExceeded)
      failure='retained-resource-purge-threshold-exceeded';
  }
  if(failure)return {...result,configurationGate:'failed',failure};
  if(!bindings.subject||!bindings.modulePin||!bindings.captureClock)
    return {...result,dataGate:'not-evaluated',failure:'missing-external-cache-binding'};
  const observed=analyzeNamedCache(value,bindings);result.dataGate=observed.dataGate;
  if(observed.dataGate==='invalid')return {...result,configurationGate:'failed',failure:observed.failure};
  const age=decimal(bindings.captureClock.qpc)-n.sampleQpc;
  result.completedCopyAge={numerator:(age*1000n).toString(),denominator:n.qpcFrequency.toString(),unit:'ms',
    scope:'completed-envelope-age-not-atomic-component-age'};
  const newer=!bindings.previous||n.sampleQpc>decimal(bindings.previous.sampleQpc);
  const fresh=observed.freshRequestCompleted&&newer&&age*1000n<=BigInt(CONFIGURATION_POLICY.maxCompletedCopyAgeMs)*n.qpcFrequency;
  if(observed.dataGate==='matched'&&fresh){result.freshnessGate='passed';result.configurationGate='passed';}
  else result.freshnessFailure=observed.dataGate!=='matched'?'partial-completed-copy':
    value.runtimeRetired?'retired-completed-copy':!newer?'repeated-completed-copy':'nonfresh-completed-copy';
  return result;
}
module.exports={scope,u64Names,decimal,analyzeNamedCache,CONFIGURATION_POLICY,analyzeNamedCacheConfiguration};
