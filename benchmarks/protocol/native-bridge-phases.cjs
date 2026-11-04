'use strict';
// Consumer-side ADR-0159 reader. No SDK-private header, tracing mutation or IO.
const MAX=(1n<<64n)-1n,LIMIT=8192,ENV='SOUI_DIAGNOSTIC_BRIDGE_PHASES';
const names=Object.freeze(['native_first_parse','resolver_create_js','reply_run_js']);
const counts=Object.freeze(['started','completed','returned','unwound','inFlight','nestedStarts','sumElapsedQpc']);
const fields=Object.freeze(['schema','requested','enabled','status','usable','coherent','observerIncarnation','qpcFrequency',
  'captureBeginQpc','captureEndQpc','publicationSequence','snapshotAttempts','faultMask','counterSaturated','clockInvalid',
  'ownerViolation','sequenceExhausted','closing','inclusiveWallTime','wholeHostAtomic','quiescent','phases']);
const statuses=new Set(['configuration_invalid','allocation_failed','observer_unavailable','not_started','busy','unavailable','ready']);
function need(value,reason){if(!value)throw new Error(reason);}
function object(value){return value!==null&&typeof value==='object'&&!Array.isArray(value);}
function exact(value){need(typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value)&&BigInt(value)<=MAX,'invalid-uint64');return BigInt(value);}
function whole(value,max=0xffffffff){return Number.isSafeInteger(value)&&value>=0&&value<=max;}
function canonical(value){if(Array.isArray(value))return '['+value.map(canonical).join(',')+']';
  if(object(value))return '{'+Object.keys(value).sort().map(key=>JSON.stringify(key)+':'+canonical(value[key])).join(',')+'}';return JSON.stringify(value);}
function same(a,b){return canonical(a)===canonical(b);}
function off(value){return value===null||value===''||value==='0';}
function envValue(environment,key){return object(environment)&&Object.hasOwn(environment,key)?environment[key]:null;}
function slice(text){const marker=',native_phases=',start=text.indexOf(marker),script=text.indexOf(',script=');
  if(start<0)return {start,end:0,raw:'',unique:false,wire:text};
  if(script>=0&&script<start)return {start,end:text.length,raw:'',unique:false,wire:text};
  const end=script<0?text.length:script,raw=text.slice(start+marker.length,end);
  return {start,end,raw,unique:text.indexOf(marker,start+marker.length)<0,wire:text.slice(0,start)+text.slice(end)};}
function shape(value){
  need(object(value)&&same(Object.keys(value).sort(),[...fields].sort()),'native-schema-fields');
  need(value.schema==='soui.miniblink-native-phases/v1'&&statuses.has(value.status),'native-schema');
  for(const key of ['requested','enabled','usable','coherent','counterSaturated','clockInvalid','ownerViolation','sequenceExhausted','closing','inclusiveWallTime','wholeHostAtomic','quiescent'])need(typeof value[key]==='boolean','native-boolean');
  need(value.inclusiveWallTime&&!value.wholeHostAtomic&&!value.quiescent&&whole(value.snapshotAttempts,3)&&whole(value.faultMask),'native-flags');
  for(const [bit,key] of ['counterSaturated','clockInvalid','ownerViolation','sequenceExhausted'].entries())need(value[key]===Boolean((value.faultMask>>>bit)&1),'fault-flag-mismatch');
  for(const key of ['observerIncarnation','qpcFrequency','captureBeginQpc','captureEndQpc','publicationSequence'])if(value[key]!==null)exact(value[key]);
  need(value.coherent?value.publicationSequence!==null&&(exact(value.publicationSequence)&1n)===0n:value.publicationSequence===null,'publication-sequence');
  if(!value.usable){need(value.phases===null,'unusable-has-buckets');return;}
  need(value.status==='ready'&&value.requested&&value.enabled&&value.coherent&&!value.faultMask&&value.snapshotAttempts>0,'usable-state-conflict');
  need(exact(value.observerIncarnation)>0n&&exact(value.qpcFrequency)>0n&&exact(value.captureBeginQpc)>0n&&exact(value.captureEndQpc)>=exact(value.captureBeginQpc),'capture-clock');
  need(Array.isArray(value.phases)&&value.phases.length===3,'phase-count');
  value.phases.forEach((phase,index)=>{
    need(object(phase)&&same(Object.keys(phase).sort(),['phase',...counts,'buckets'].sort())&&phase.phase===names[index],'phase-order-or-fields');
    const c=Object.fromEntries(counts.map(key=>[key,exact(phase[key])]));
    need(Array.isArray(phase.buckets)&&phase.buckets.length===64,'bucket-count');const buckets=phase.buckets.map(exact);
    need(c.started===c.completed+c.inFlight&&c.completed===c.returned+c.unwound&&c.nestedStarts<=c.started&&buckets.reduce((a,b)=>a+b,0n)===c.completed,'phase-accounting');
    const low=buckets.reduce((sum,count,bucket)=>sum+count*(bucket===0?0n:1n<<BigInt(bucket)),0n);
    const high=buckets.reduce((sum,count,bucket)=>sum+count*upper(bucket),0n);
    need(c.sumElapsedQpc>=low&&c.sumElapsedQpc<=high,'histogram-sum-range');
  });
}
function parseExtension(text,environment){
  need(typeof text==='string','diagnostics-not-string');const part=slice(text),requested=envValue(environment,ENV),script=envValue(environment,'SOUI_DIAGNOSTIC_SCRIPT');
  if(off(requested)&&part.start<0)return {summary:null,value:null,raw:null,wireDiagnostics:text};
  const summary={schema:'soui.sdk-performance-native-phase-cache/v1',source:'cached-ui-application-diagnostics',requestedEnvironment:requested,
    scriptEnvironment:script,extensionPresent:part.start>=0,parseStatus:'missing',usable:false,rawUtf8Bytes:Buffer.byteLength(part.raw,'utf8'),rawIncluded:false,reasons:[]};
  if(requested!=='1')summary.reasons.push('profile-not-enabled');
  if(!off(script))summary.reasons.push('script-profile-enabled-or-invalid');
  if(text.includes(',script='))summary.reasons.push('unexpected-script-observation');
  let value=null;
  if(part.start<0)summary.reasons.push('extension-missing');
  else try{
    need(part.unique&&summary.rawUtf8Bytes<=LIMIT,'extension-extent');value=JSON.parse(part.raw);
    need(canonical(value)===part.raw,'noncanonical-or-duplicate-fields');shape(value);summary.parseStatus='valid';
    for(const key of fields)if(!['schema','phases','usable'].includes(key))summary[key]=value[key];
    summary.nativeUsable=value.usable;
    if(!value.usable)summary.reasons.push('native-snapshot-unusable');
    if(value.closing)summary.reasons.push('native-closing');summary.usable=summary.reasons.length===0;
  }catch{value=null;summary.parseStatus='invalid';summary.reasons.push('extension-invalid');}
  return {summary,value,raw:summary.rawUtf8Bytes<=LIMIT?part.raw:null,wireDiagnostics:part.wire};
}
function wireSnapshot(snapshot){const value=structuredClone(snapshot);if(!Object.hasOwn(value,'nativePhaseCache'))return value;
  if(typeof value.applicationDiagnostics==='string')value.applicationDiagnostics=slice(value.applicationDiagnostics).wire;return value;}
function boundaryMetadata(source,begin,end,frequency,session){
  const host=source?.nativeHost;if(!object(host)||!Object.hasOwn(host,'nativePhaseCache'))return null;
  begin=exact(begin);end=exact(end);frequency=exact(frequency);session=exact(session);const cache=host.nativePhaseCache;
  const result={schema:'soui.sdk-performance-native-phase-boundary/v1',cache:structuredClone(cache),boundaryBeginQpc:String(begin),boundaryEndQpc:String(end),
    qpcFrequency:String(frequency),expectedSessionGeneration:String(session),cacheAgeAtBoundaryBeginQpc:null,qualified:false,
    rawSource:'sourceIdentity.nativeHost.applicationDiagnostics',reasons:[]};
  if(!object(cache)||cache.usable!==true)result.reasons.push('cache-unusable');
  else if(exact(cache.qpcFrequency)!==frequency||frequency===0n)result.reasons.push('frequency-mismatch');
  else if(exact(cache.captureBeginQpc)===0n||exact(cache.captureEndQpc)<exact(cache.captureBeginQpc)||begin===0n||end<begin||exact(cache.captureEndQpc)>begin)result.reasons.push('capture-order-invalid');
  else if(!whole(host.sessionGeneration,Number.MAX_SAFE_INTEGER)||BigInt(host.sessionGeneration)!==session)result.reasons.push('cached-session-mismatch');
  else{result.cacheAgeAtBoundaryBeginQpc=String(begin-exact(cache.captureEndQpc));result.qualified=true;}
  return result;
}
function upper(bucket){return bucket===63?MAX:(1n<<BigInt(bucket+1))-1n;}
function percentile(buckets,n,percent){if(n===0n)return null;const rank=(n*BigInt(percent)+99n)/100n;let seen=0n;
  for(let bucket=0;bucket<64;bucket++){seen+=buckets[bucket];if(seen>=rank)return {bucket,upperQpc:String(upper(bucket)),inclusive:true};}throw new Error('percentile-accounting');}
function identity(source){
  need(object(source)&&source.schema==='soui.sdk-performance-identity/v1'&&source.backend==='miniblink'&&['x64','x86'].includes(source.architecture)&&source.inputTrace==='scalar','source-identity');
  for(const key of ['buildIdentity','sdkVersion','sdkPackageVersion'])need(typeof source[key]==='string'&&source[key].length>0&&source[key].length<=80,'source-version');
  for(const key of ['pageHash','executableSha256','nodeSha256'])need(typeof source[key]==='string'&&/^[a-f0-9]{64}$/.test(source[key]),'source-hash');
  need(typeof source.optionalFeedback==='boolean'&&[0,1000,2000].includes(source.engineBudgetUs)&&['off','scalar'].includes(source.observation)&&whole(source.requestedFrameRate,1000)&&source.requestedFrameRate>=30&&object(source.environment)&&Object.hasOwn(source.environment,ENV)&&Object.hasOwn(source.environment,'SOUI_DIAGNOSTIC_SCRIPT'),'source-profile');
  const descriptor=source.inputTraceDescriptor,process=source.initialProcess,host=source.nativeHost;
  need(object(descriptor)&&descriptor.configured===true&&descriptor.mode==='scalar'&&whole(descriptor.processId)&&descriptor.processId>0,'trace-identity');
  for(const key of ['runIdHigh','runIdLow','processIncarnation','qpcFrequency'])exact(descriptor[key]);
  need(exact(descriptor.qpcFrequency)>0n&&exact(descriptor.processIncarnation)>0n&&(exact(descriptor.runIdHigh)>0n||exact(descriptor.runIdLow)>0n),'trace-identity-values');
  need(object(process)&&process.pid===descriptor.processId&&process.creationFileTime===descriptor.processIncarnation&&object(host)&&host.pid===process.pid&&host.creationFileTime===process.creationFileTime,'process-identity');
  const keys=['schema','backend','architecture','buildIdentity','sdkVersion','sdkPackageVersion','optionalFeedback','engineBudgetUs','requestedFrameRate','pageHash','executableSha256','nodeSha256','inputTrace','observation','environment'];
  return {...Object.fromEntries(keys.map(key=>[key,source[key]])),trace:Object.fromEntries(['processId','runIdHigh','runIdLow','processIncarnation','qpcFrequency'].map(key=>[key,descriptor[key]]))};
}
function analyzeBoundaries(begin,end,{maximumCacheAgeMs=2000}={}){
  need(whole(maximumCacheAgeMs,60000),'invalid-cache-age-policy');
  const result={schema:'soui.native-bridge-phase-analysis/v1',qualified:false,reasons:[],maximumCacheAgeMs,boundaries:[],phases:null,
    qualificationScope:'Native boundary content only; caller must verify frozen artifacts, saved receipts, isolation and natural lifecycle.',
    performanceVerified:false,physicalInputLatencyMeasured:false,physicalPresentationMeasured:false,
    limits:['Cached UI capture points are not the DOM or SDK scalar window.','Inclusive wall-time may overlap and cross boundaries; never sum phases as CPU time.','No new native input or physical presentation causality.']};
  try{
    const parsed=[],ids=[];
    for(const [index,boundary] of [begin,end].entries()){
      need(object(boundary)&&boundary.schema==='soui.sdk-performance-scalar-boundary/v1'&&boundary.phase===(index?'end':'begin')&&boundary.scope==='process-cumulative-live'&&whole(boundary.hostIndex,1)&&whole(boundary.measurementId)&&boundary.measurementId>0,'boundary-schema');
      const start=exact(boundary.captureBeginQpc),finish=exact(boundary.captureEndQpc),freq=exact(boundary.qpcFrequency),session=exact(boundary.sessionGeneration);
      need(start>0n&&finish>=start&&freq>0n&&session>0n,'boundary-clock');
      ids.push(identity(boundary.sourceIdentity));need(ids[index].trace.qpcFrequency===boundary.qpcFrequency,'boundary-trace-frequency');
      const host=boundary.sourceIdentity.nativeHost,extension=parseExtension(host.applicationDiagnostics??'',boundary.sourceIdentity.environment);
      const recomputed=extension.summary===null?null:boundaryMetadata({...boundary.sourceIdentity,nativeHost:{...host,nativePhaseCache:extension.summary}},boundary.captureBeginQpc,boundary.captureEndQpc,boundary.qpcFrequency,boundary.sessionGeneration);
      result.boundaries.push({phase:boundary.phase,metadata:recomputed});
      need(extension.summary!==null&&same(host.nativePhaseCache,extension.summary),'cache-summary-missing-or-differs');
      need(same(boundary.nativePhaseCache,recomputed),'boundary-cache-metadata-differs');
      need(recomputed.qualified&&extension.value!==null,'native-phase-unavailable');
      need(exact(recomputed.cacheAgeAtBoundaryBeginQpc)*1000n<=freq*BigInt(maximumCacheAgeMs),'native-phase-cache-stale');parsed.push(extension.value);
    }
    need(same(ids[0],ids[1])&&begin.measurementId===end.measurementId&&begin.hostIndex===end.hostIndex&&begin.sessionGeneration===end.sessionGeneration,'boundary-identity-mismatch');
    need(exact(end.captureBeginQpc)>=exact(begin.captureEndQpc),'boundary-clock-regressed');
    const a=parsed[0],b=parsed[1];
    need(a.observerIncarnation===b.observerIncarnation&&a.qpcFrequency===b.qpcFrequency,'observer-identity-mismatch');
    need(a.captureBeginQpc!==b.captureBeginQpc||a.captureEndQpc!==b.captureEndQpc||a.publicationSequence!==b.publicationSequence,'no-new-capture');
    need(exact(b.captureBeginQpc)>=exact(a.captureEndQpc)&&exact(b.publicationSequence)>=exact(a.publicationSequence),'phase-capture-or-sequence-regressed');
    if(a.publicationSequence===b.publicationSequence)need(same(a.phases,b.phases),'same-sequence-different-counters');
    result.phaseWindow={beginCaptureBeginQpc:a.captureBeginQpc,beginCaptureEndQpc:a.captureEndQpc,endCaptureBeginQpc:b.captureBeginQpc,endCaptureEndQpc:b.captureEndQpc,qpcFrequency:a.qpcFrequency,
      observerIncarnation:a.observerIncarnation,beginSequence:a.publicationSequence,endSequence:b.publicationSequence};
    result.phases=names.map((phase,index)=>{
      const left=a.phases[index],right=b.phases[index],deltas={};
      for(const key of counts.filter(key=>key!=='inFlight')){const change=exact(right[key])-exact(left[key]);need(change>=0n,'phase-counter-regressed');deltas[key]=String(change);}
      const buckets=right.buckets.map((count,bucket)=>{const delta=exact(count)-exact(left.buckets[bucket]);need(delta>=0n,'phase-bucket-regressed');return delta;});
      const n=exact(deltas.completed);need(buckets.reduce((sum,count)=>sum+count,0n)===n,'delta-bucket-accounting');
      const low=buckets.reduce((sum,count,bucket)=>sum+count*(bucket===0?0n:1n<<BigInt(bucket)),0n);
      const high=buckets.reduce((sum,count,bucket)=>sum+count*upper(bucket),0n);
      need(exact(deltas.sumElapsedQpc)>=low&&exact(deltas.sumElapsedQpc)<=high,'delta-histogram-sum-range');
      need(exact(deltas.nestedStarts)<=exact(deltas.started),'delta-nested-start-accounting');
      return {phase,...deltas,beginInFlight:left.inFlight,endInFlight:right.inFlight,buckets:buckets.map(String),p95:percentile(buckets,n,95),p99:percentile(buckets,n,99),
        meanInclusiveQpc:n===0n?null:{numerator:deltas.sumElapsedQpc,denominator:String(n)},samplesCompleteWithinPhaseCaptureWindow:false};
    });result.qualified=true;
  }catch(error){result.reasons.push(error.message);result.phases=null;}
  return result;
}
module.exports={parseExtension,wireSnapshot,boundaryMetadata,analyzeBoundaries,exact,maximum:MAX,phaseNames:names,extensionLimit:LIMIT};
