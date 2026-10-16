'use strict';
// Scalar and sequence validation only. File/source/Owned-process verification
// belongs to the caller; this module cannot qualify a resource or release gate.
const scope='current-process-virtual-address-regions';
const maximumQpc=9223372036854775807n;
const fields=['processId','captureThreadId','beginQpc','endQpc','qpcFrequency',
  'minimumAddress','maximumAddress','nextAddress','coveredBytes','committedPrivateBytes',
  'committedMappedBytes','committedImageBytes','committedTotalBytes','reservedBytes','freeBytes'];
const states=['complete','unavailable','invalid-range','clock-error','time-budget',
  'region-budget','query-error','invalid-region','unknown-type','unknown-state'];
function decimal(value){
  if(typeof value!=='string'||!/^(0|[1-9][0-9]*)$/.test(value)||value.length>20||BigInt(value)>18446744073709551615n)
    throw Error('Expected canonical uint64 decimal');
  return BigInt(value);
}
function analyzeAddressSpace(value,{subject,nativeIdentity,captureClock,pointerBytes,previous}={}){
  const result={scope,dataGate:'not-evaluated',resourceGate:'not-evaluated',
    cacheBudgetGate:'not-evaluated',qualified:false,releaseReady:false};
  if(value===null||value===undefined)return result;
  if(value.schemaVersion!==1||value.scope!==scope)return {...result,failure:'unsupported-address-space-envelope'};
  try{
    if(!states.includes(value.state)||value.nonAtomic!==true||value.physicalMemory!==false||
        value.copyOnWriteReclassified!==false||![4,8].includes(value.pointerBytes)||
        value.queryLimit!==32768||value.timeLimitMs!==50)throw Error('Invalid address-space scope or limits');
    for(const name of ['queries','committedPrivateRegions','committedMappedRegions','committedImageRegions','win32Error'])
      if(!Number.isSafeInteger(value[name])||value[name]<0||value[name]>4294967295)throw Error('Invalid native counter');
    if(value.queries>value.queryLimit)throw Error('Query limit exceeded');
    const n=Object.fromEntries(fields.map(name=>[name,decimal(value[name])]));
    if(n.beginQpc>maximumQpc||n.endQpc>maximumQpc||n.qpcFrequency>maximumQpc)
      throw Error('Impossible signed native performance counter');
    if(!n.processId||n.processId>4294967295n||!n.captureThreadId||n.captureThreadId>4294967295n)
      throw Error('Invalid native process/thread identity');
    if(!subject||!nativeIdentity||!captureClock||![4,8].includes(pointerBytes))
      return {...result,failure:'missing-external-address-space-binding'};
    if(n.processId!==decimal(String(subject.pid))||n.processId!==decimal(nativeIdentity.pid)||
        !decimal(subject.creationFileTime)||nativeIdentity.creationFileTime!==subject.creationFileTime||
        n.captureThreadId!==decimal(nativeIdentity.captureThreadId)||
        n.captureThreadId!==decimal(captureClock.captureThreadId)||pointerBytes!==value.pointerBytes)
      throw Error('Address-space identity mismatch');
    if(captureClock.valid!==true||!decimal(captureClock.frequency)||!decimal(captureClock.qpc)||
        decimal(captureClock.frequency)>maximumQpc||decimal(captureClock.qpc)>maximumQpc)
      throw Error('Invalid enclosing clock');
    // Failed OS operations retain their partial output, never a known zero or
    // complete result. A complete observation must satisfy all invariants below.
    if(value.state!=='complete')return {...result,dataGate:'partial',state:value.state};
    if(value.win32Error!==0||!n.beginQpc||n.endQpc<n.beginQpc||!n.qpcFrequency||
        n.qpcFrequency!==decimal(captureClock.frequency)||n.endQpc>decimal(captureClock.qpc)||
        n.endQpc-n.beginQpc>n.qpcFrequency/20n)throw Error('Incomplete or out-of-budget capture clock');
    const maximum=(1n<<BigInt(value.pointerBytes*8))-1n;
    if(!n.minimumAddress||n.maximumAddress<n.minimumAddress||n.maximumAddress>=maximum||
        n.nextAddress!==n.maximumAddress+1n||!value.queries)throw Error('Incomplete address range');
    if(n.committedTotalBytes!==n.committedPrivateBytes+n.committedMappedBytes+n.committedImageBytes||
        n.coveredBytes!==n.committedTotalBytes+n.reservedBytes+n.freeBytes||
        n.coveredBytes!==n.nextAddress-n.minimumAddress)throw Error('Address range totals differ');
    let committedRegions=0;
    for(const type of ['Private','Mapped','Image']){
      const bytes=n[`committed${type}Bytes`],regions=value[`committed${type}Regions`];
      if((bytes===0n)!==(regions===0)||bytes<BigInt(regions))throw Error('Region count contradicts bytes');
      committedRegions+=regions;
    }
    if(committedRegions>value.queries)throw Error('Committed regions exceed queries');
    if(previous){
      for(const name of ['processId','captureThreadId','qpcFrequency','pointerBytes','minimumAddress','maximumAddress'])
        if(value[name]!==previous[name])throw Error('Address-space sequence identity changed');
      if(n.beginQpc<decimal(previous.endQpc))throw Error('Address-space capture time regressed');
    }
    return {...result,dataGate:'matched',state:value.state,coveredBytes:value.coveredBytes,
      elapsedQpc:(n.endQpc-n.beginQpc).toString(),qpcFrequency:value.qpcFrequency,
      committed:{private:value.committedPrivateBytes,mapped:value.committedMappedBytes,image:value.committedImageBytes},
      reservedBytes:value.reservedBytes,freeBytes:value.freeBytes};
  }catch(error){return {...result,dataGate:'invalid',failure:error.message};}
}
function addressSpaceObservations(records,subject,architecture){
  const counts={matched:0,partial:0,invalid:0,'not-evaluated':0},points=[];
  const peaks={private:0n,mapped:0n,image:0n};
  let previous;
  for(const [eventIndex,row] of records.entries()){
    if(row.event!=='sample')continue;
    const native=row.nativeSdk;
    const observed=analyzeAddressSpace(native?.addressSpace,{subject,nativeIdentity:native,
      captureClock:native?.captureClock,pointerBytes:architecture==='x86'?4:architecture==='x64'?8:undefined,previous});
    ++counts[observed.dataGate];
    points.push({eventIndex,phase:row.phase,cycle:row.cycle,...observed});
    if(observed.dataGate==='matched'){
      previous=native.addressSpace;
      for(const type of Object.keys(peaks))if(BigInt(observed.committed[type])>peaks[type])peaks[type]=BigInt(observed.committed[type]);
    }
  }
  return {schema:'soui.process-address-space-observations/v1',scope,counts,points,
    dataGate:counts.invalid?'invalid':points.length&&counts.matched===points.length?'matched':'not-evaluated',
    peaksFromMatchedSamples:counts.matched?Object.fromEntries(Object.entries(peaks).map(([name,value])=>[name,value.toString()])):null,
    atomic:false,uniquePhysicalMemory:false,allocationOwnership:'not-observed',
    resourceGate:'not-evaluated',cacheBudgetGate:'not-evaluated',qualified:false,releaseReady:false};
}
module.exports={scope,fields,states,decimal,analyzeAddressSpace,addressSpaceObservations};
