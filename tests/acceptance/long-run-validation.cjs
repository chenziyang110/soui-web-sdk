'use strict';
const fs=require('node:fs'),path=require('node:path');
const {isUtf8}=require('node:buffer');
const policy=require('./long-run-policy.cjs');
const owners=require('./long-run-owners.cjs');
const identity=require('../../tools/internal/delivery-identity.cjs');
const metrics=['privateBytes','handles','threads','gdiObjects','userObjects'];
const NE='not-evaluated';
class EvidenceError extends Error{constructor(code,failed=false){super(code);this.failed=failed;}}
const need=(ok,code,failed=false)=>{if(!ok)throw new EvidenceError(code,failed);};
const integer=(n,min=0)=>Number.isSafeInteger(n)&&n>=min;
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
function parseRaw(raw,ndjson=false){
  need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=256*1024*1024&&isUtf8(raw),'missing-or-invalid-long-raw');
  const text=raw.toString('utf8');need(text.endsWith('\n'),'truncated-long-raw');
  try{return ndjson?text.slice(0,-1).split('\n').map(line=>JSON.parse(line)):JSON.parse(text);}
  catch{throw new EvidenceError('invalid-long-raw-json');}
}
function nearestRank(values,percent){
  need(values.length>0&&values.every(n=>integer(n)),'missing-percentile-observation');
  return [...values].sort((a,b)=>a-b)[Math.ceil(values.length*percent/100)-1];
}
function percentCeil(value,percent){return (BigInt(value)*BigInt(percent)+99n)/100n;}
function amplitude(base,minimum,percent){const proportional=percentCeil(base,percent);return proportional>BigInt(minimum)?proportional:BigInt(minimum);}
function trend(values){
  const ratio=require('./resource-validation.cjs').theilSen(values);
  return {numerator:ratio.n.toString(),denominator:ratio.d.toString(),pairCount:values.length*(values.length-1)/2,nonIncreasing:ratio.n<=0n};
}
function quantiles(hours,key){return hours.map(hour=>({p50:nearestRank(hour.map(row=>row[key]),50),p95:nearestRank(hour.map(row=>row[key]),95)}));}
function checkSeries(points,medianMinimum,p95Minimum,medianPercent,p95Percent){
  const p50=points.map(row=>row.p50),p95=points.map(row=>row.p95);
  const medianBudget=amplitude(p50[0],medianMinimum,medianPercent),p95Budget=amplitude(p95[0],p95Minimum,p95Percent);
  const medianTrend=trend(p50),p95Trend=trend(p95);
  const passed=p50.every(value=>BigInt(value)-BigInt(p50[0])<=medianBudget)&&
    p95.every(value=>BigInt(value)-BigInt(p95[0])<=p95Budget)&&medianTrend.nonIncreasing&&p95Trend.nonIncreasing;
  return {gate:passed?'passed':'failed',points,medianBudget:medianBudget.toString(),p95Budget:p95Budget.toString(),medianTrend,p95Trend};
}
function gate(fn){try{return fn();}catch(error){return {gate:error.failed?'failed':NE,failure:error.message||'invalid-long-observation'};}}
function combine(values){return values.includes('failed')?'failed':values.every(value=>value==='passed')?'passed':NE;}
const durationGates=['inputs','operations','owned','tree','duration','sampling','resources','actions','native','ownerCaps','terminalDrain','namedCacheConfiguration','namedCacheCoverage','cacheBudget'];
const ordinaryGates=['inputs','operations','owned','tree'];
function requiredGates(mode){return ['mixed','session'].includes(mode)?durationGates:['inputs','operations','owned'];}
function aggregate(mode,gates){
  const duration=['mixed','session'].includes(mode),all=duration?durationGates:ordinaryGates;
  need(policy.MODES.includes(mode)&&gates&&typeof gates==='object'&&!Array.isArray(gates)&&
    Object.keys(gates).sort().join('|')===[...all].sort().join('|')&&all.every(name=>['passed','failed',NE].includes(gates[name])),
    'invalid-long-gate-contract');
  const required=requiredGates(mode),overall=combine(required.map(name=>gates[name]));
  const resource=duration?combine(durationGates.filter(name=>name!=='actions').map(name=>gates[name])):NE;
  return {requiredGates:[...required],longRunGate:overall,resourceGate:resource,eightHourGate:duration?overall:NE,
    thousandCycleOperationGate:duration?NE:overall,passed:overall==='passed',
    failure:overall==='passed'?null:required.filter(name=>gates[name]!=='passed').map(name=>name+':'+gates[name]).join(',')};
}
function checkedAggregate(analysis,mode){
  if(['mixed','session'].includes(mode)){
    need(analysis.gates?.cacheBudget===NE,'uncalibrated-full-cache-budget-cannot-pass');
    if(analysis.namedCaches){
      const named=analysis.namedCaches,points=named.configurationPoints||[];
      const configuration=points.some(row=>row.configurationGate==='failed')?'failed':
        points.some(row=>row.configurationGate==='passed')?'passed':NE;
      need(named.configurationGate===configuration&&analysis.gates.namedCacheConfiguration===configuration&&
        analysis.gates.namedCacheCoverage===named.freshCoverageGate&&named.cacheBudgetGate===NE&&
        named.calibratedByteBudgetGate===NE&&named.calibratedByteBudgets===null,
        'inconsistent-finite-cache-analysis-gates');
    }
  }
  const current=aggregate(mode,analysis.gates);
  need(same(analysis.requiredGates,current.requiredGates)&&['longRunGate','resourceGate','eightHourGate','thousandCycleOperationGate','passed','failure']
    .every(name=>analysis[name]===current[name]),'inconsistent-long-analysis-aggregate');
  return current;
}
function nativeArgs(config,eventLogPath){
  return config.durationMs!==undefined?
    ['--mode='+config.mode,'--duration-ms='+config.durationMs,'--idle-ms='+config.idleMs,'--settle-ms='+config.settleMs,'--timeout-ms='+config.timeoutMs,'--report='+eventLogPath,'--cache-observation=1']:
    ['--mode='+config.mode,'--cycles='+config.cycles,'--warmup='+config.warmup,'--settle-ms='+config.settleMs,'--timeout-ms='+config.timeoutMs,'--report='+eventLogPath];
}
function namedCacheObservations(records,subject,modulePin,config){
  const cache=require('./native-cache-validation.cjs');
  const result={scope:cache.scope,dataGate:NE,sampleRows:0,completedCopies:0,missingSamples:0,
    invalidSamples:0,partialSamples:0,newCompletedSamples:0,repeatedCompletedCopies:0,retiredCopies:0,
    maximumAgeMicroseconds:'0',lastSampleQpc:null,resourceGate:NE,qualified:false,failure:null,
    configurationGate:NE,freshCoverageGate:NE,calibratedByteBudgetGate:NE,cacheBudgetGate:NE,
    calibratedByteBudgets:null,configurationFailures:[],configurationPoints:[],freshCoverage:null};
  let previous=null,maxAge=0n;const fresh=[];
  for(const row of records){
    if(row.event!=='sample'||row.phase==='process')continue;
    ++result.sampleRows;
    const dto=row.nativeSdk?.namedCaches,clock=row.nativeSdk?.captureClock;
    const configuration=cache.analyzeNamedCacheConfiguration(dto,{subject,modulePin,captureClock:clock,previous});
    result.configurationPoints.push({elapsedMs:row.elapsedMs??null,phase:row.phase,cycle:row.cycle??null,
      sampleQpc:dto?.sampleQpc??null,...configuration});
    if(configuration.configurationGate==='failed')result.configurationFailures.push(configuration.failure);
    if(configuration.configurationGate==='passed')fresh.push(row);
    if(!dto){++result.missingSamples;continue;}
    const observed=cache.analyzeNamedCache(dto,{subject,modulePin,captureClock:clock,previous});
    if(observed.dataGate==='invalid'){
      ++result.invalidSamples;result.failure??=observed.failure;continue;
    }
    if(observed.dataGate===NE){++result.missingSamples;continue;}
    ++result.completedCopies;
    if(observed.dataGate==='partial')++result.partialSamples;
    const qpc=cache.decimal(dto.sampleQpc),prior=previous?cache.decimal(previous.sampleQpc):null;
    if(prior!==null&&qpc===prior)++result.repeatedCompletedCopies;
    if(observed.freshRequestCompleted&&(prior===null||qpc>prior))++result.newCompletedSamples;
    if(observed.retainedSnapshot)++result.retiredCopies;
    const age=(cache.decimal(clock.qpc)-qpc)*1000000n/cache.decimal(dto.qpcFrequency);
    if(age>maxAge)maxAge=age;
    previous=dto;result.lastSampleQpc=dto.sampleQpc;
  }
  result.maximumAgeMicroseconds=maxAge.toString();
  result.dataGate=result.invalidSamples?'invalid':!result.completedCopies?NE:
    result.partialSamples||result.missingSamples?'partial':'matched';
  result.configurationGate=result.configurationFailures.length?'failed':fresh.length?'passed':NE;
  result.configurationScope='eligible-fresh-complete-points; all other points retained as diagnostics and coverage exclusions';
  if(config?.durationMs!==undefined){
    result.freshCoverage=gate(()=>{
      const origin=originFor(records,config),phase=config.mode==='session'?'anchor':'cycle';
      const hours=Array.from({length:8},()=>[]);
      for(const row of fresh){
        if(row.phase!==phase)continue;
        need(integer(row.elapsedMs)&&row.elapsedMs>=origin,'invalid-named-cache-observation-time');
        const hour=Math.floor((row.elapsedMs-origin)/policy.HOUR);if(hour<8)hours[hour].push(row.elapsedMs);
      }
      const cadence=hours.map((times,hour)=>{
        need(times.every((time,i)=>i===0||time>=times[i-1]),'named-cache-observation-time-reversed',true);
        const start=origin+hour*policy.HOUR,end=start+policy.HOUR;
        const gaps=times.length?[times[0]-start,...times.slice(1).map((time,i)=>time-times[i]),end-times.at(-1)]:[policy.HOUR];
        return {hour,newCompleteCopies:times.length,maxGapMs:Math.max(...gaps)};
      });
      const rule=policy.RULE.namedCacheConfigurationPolicy;
      return {gate:cadence.every(row=>row.newCompleteCopies>=rule.minNewSamplesPerHour&&row.maxGapMs<=rule.maxNewSampleGapMs)?'passed':NE,
        phase,originMs:origin,cadence,scope:'distinct-fresh-completed-copies-observed-at-native-phase-times'};
    });
    result.freshCoverageGate=result.freshCoverage.gate;
  }
  return result;
}
function processBudget(config){
  return config.durationMs!==undefined?config.durationMs+Math.max(180000,2*config.timeoutMs+config.idleMs):
    Math.min(12*60*60*1000,Math.max(60000,require('./runner.cjs').expectedSteps(config).length*(config.timeoutMs+config.settleMs)+30000));
}
function verifyOwned(result,evidence,config){
  const out=result.outcome,finished=out?.ownedProcess;
  need(out&&finished,'missing-owned-outcome');
  need(out.timedOut===false&&out.exitCode===0&&!out.signal&&!out.spawnError&&finished.exitCode===0&&finished.supervisorExitCode===0&&
    finished.passed===true&&finished.stopRequested===false&&finished.fallbackUsed===false&&finished.closureConfirmed===true&&
    !finished.spawnError&&!finished.reportError&&!finished.signal,'owned-not-natural-success',true);
  const rows=parseRaw(evidence.ownedRaw,true),context=parseRaw(evidence.contextRaw),start=rows[0],terminal=rows.at(-1);
  const members=rows.filter(row=>row.event==='owned-member');
  need(start?.schema==='soui.owned-process-run/v1'&&start.event==='start'&&rows.filter(row=>row.event==='start').length===1&&
    rows.filter(row=>row.event==='terminal').length===1&&terminal?.event==='terminal','missing-owned-boundaries');
  need(integer(start.pid,1)&&start.pid<=0xffffffff&&typeof start.creationFileTime==='string'&&
    /^[1-9]\d{0,19}$/.test(start.creationFileTime)&&owners.count(start.creationFileTime)>0n&&
    start.pid===out.pid&&start.pid===finished.pid&&start.creationFileTime===finished.creationFileTime&&
    start.jobAssignedBeforeResume===true&&start.breakawayAllowed===false,'owned-process-identity-differs',true);
  need(terminal.reason==='completed'&&terminal.rootExited===true&&terminal.rootExitCode===0&&terminal.activeBeforeCleanup===0&&
    terminal.activeProcesses===0&&terminal.terminatedOwnedJob===false&&terminal.limitTerminatedProcesses===0&&
    terminal.closureConfirmed===true&&terminal.passed===true&&integer(terminal.totalProcesses,1)&&same(terminal,finished.terminal),
    'owned-whole-job-not-natural',true);
  const timeout=processBudget(config)+180000;
  need(out.processTimeoutMs===processBudget(config)&&context.schema==='soui.owned-process-context/v1'&&
    samePath(context.executable,evidence.executablePath)&&samePath(context.cwd,evidence.payloadRoot)&&
    same(context.args,nativeArgs(config,evidence.eventLogPath))&&context.timeoutMs===timeout&&start.timeoutMs===timeout&&
    context.drainMs===2000&&start.drainMs===2000&&context.executableSha256===evidence.executableSha256&&
    context.runnerSha256===evidence.runnerSha256&&finished.runnerSha256===evidence.runnerSha256&&
    finished.executableSha256===evidence.executableSha256&&policy.hash(evidence.ownedRaw)===finished.reportSha256,
    'owned-original-argv-context-or-pin-differs',true);
  need(integer(terminal.elapsedMs),'missing-or-invalid-owned-elapsed');
  need(terminal.elapsedMs<=context.timeoutMs+context.drainMs+15000,'owned-elapsed-exceeds-original-watchdog',true);
  need(config.durationMs===undefined||terminal.elapsedMs>=config.durationMs,'owned-real-duration-shorter-than-request',true);
  const single=terminal.totalProcesses===1&&members.length>0&&members.every(row=>row.pid===start.pid&&
    row.creationFileTime===start.creationFileTime&&samePath(row.image,evidence.executablePath));
  return {gate:'passed',treeGate:single?'passed':NE,
    treeFailure:single?null:'Actual child members lack per-member resource sampling',
    subject:{pid:start.pid,creationFileTime:start.creationFileTime,entireJobProcesses:single?1:terminal.totalProcesses,
      nativeExitCode:0,supervisorExitCode:0,naturalClosure:true,forcedClosure:false},members:members.length,elapsedMs:terminal.elapsedMs};
}
function verifyRunClock(records,config,owned){
  const end=Array.isArray(records)?records.at(-1):null;
  need(end?.event==='run.complete'&&integer(end.elapsedMs)&&integer(owned?.elapsedMs),'missing-original-native-or-owned-clock');
  need(end.elapsedMs<=owned.elapsedMs,'native-clock-exceeds-actual-owned-time',true);
  need(config.durationMs===undefined||owned.elapsedMs>=config.durationMs,'owned-real-duration-shorter-than-request',true);
  return {gate:'passed',nativeRunCompleteElapsedMs:end.elapsedMs,ownedTerminalElapsedMs:owned.elapsedMs,requestedDurationMs:config.durationMs??null};
}
function originFor(records,config){
  if(config.mode==='mixed'){
    const row=records.find(row=>row.event==='cycle.begin'&&row.cycle===1);
    need(row&&integer(row.elapsedMs),'missing-mixed-cold-origin');return row.elapsedMs;
  }
  const started=records.find(row=>row.event==='session.report'&&row.window===1&&row.data?.phase==='started');
  const anchor=records.find(row=>row.event==='sample'&&row.phase==='anchor'&&row.cycle===0);
  need(started&&anchor&&integer(started.elapsedMs)&&integer(anchor.elapsedMs)&&started.elapsedMs<=anchor.elapsedMs,
    'missing-authenticated-session-anchor-origin');return anchor.elapsedMs;
}
function hourlySamples(records,config){
  const origin=originFor(records,config),phase=config.mode==='session'?'anchor':'cycle';
  const selected=records.filter(row=>row.event==='sample'&&row.phase===phase),hours=Array.from({length:8},()=>[]),tail=[];
  for(const row of selected){
    need(integer(row.elapsedMs)&&row.elapsedMs>=origin,'invalid-resource-sample-clock');
    const offset=row.elapsedMs-origin,index=Math.floor(offset/policy.HOUR);
    if(index<8)hours[index].push(row);else tail.push(row);
  }
  const cadence=hours.map((rows,index)=>{
    const start=origin+index*policy.HOUR,end=start+policy.HOUR;
    const timestamps=rows.map(row=>row.elapsedMs);
    const gaps=timestamps.length?[timestamps[0]-start,...timestamps.slice(1).map((time,i)=>time-timestamps[i]),end-timestamps.at(-1)]:[policy.HOUR];
    need(timestamps.every((time,i)=>i===0||time>=timestamps[i-1]),'resource-clock-reversed',true);
    return {hour:index,samples:rows.length,maxGapMs:Math.max(...gaps),
      firstOffsetMs:timestamps.length?timestamps[0]-start:null,lastOffsetMs:timestamps.length?end-timestamps.at(-1):null};
  });
  return {gate:cadence.every(row=>row.samples>=policy.RULE.minSamplesPerHour&&row.maxGapMs<=policy.RULE.maxSampleGapMs)?'passed':NE,
    origin,phase,hours,cadence,tail,tailSamples:tail.length,
    retainedBoundarySamples:records.filter(row=>row.event==='sample'&&row.phase!==phase),
    failure:cadence.some(row=>row.samples<30||row.maxGapMs>180000)?'hourly-resource-count-or-edge-gap-exceeded':null};
}
function resourceTrends(group){
  need(group.gate==='passed'&&group.hours?.length===8&&group.hours.every(rows=>rows.length>=policy.RULE.minSamplesPerHour),'missing-full-hour-resource-points');
  for(const rows of group.hours)for(const row of rows)for(const name of [...metrics,'workingSetBytes'])
    need(integer(row[name]),'missing-actual-process-resource-'+name);
  const values={};
  values.privateBytes=checkSeries(quantiles(group.hours,'privateBytes'),policy.RULE.privateMedianMinimumBytes,
    policy.RULE.privateP95MinimumBytes,20,25);
  for(const name of metrics.slice(1))values[name]=checkSeries(quantiles(group.hours,name),0,0,0,0);
  const classKey=value=>{
    need(value&&typeof value==='object'&&!Array.isArray(value),'missing-window-classes');
    const entries=Object.entries(value).sort(([a],[b])=>a<b?-1:a>b?1:0);
    need(entries.length>0&&entries.every(([name,n])=>name.length>0&&integer(n,1)),'invalid-window-classes');
    return JSON.stringify(entries);
  };
  const baseline=classKey(group.hours[0][0].windowClasses);
  const classes=group.hours.flat().every(row=>classKey(row.windowClasses)===baseline);
  return {gate:combine([...Object.values(values).map(value=>value.gate),classes?'passed':'failed']),metrics:values,
    windowClassesGate:classes?'passed':'failed',phase:group.phase,
    workingSet:group.hours.map(rows=>({p50:nearestRank(rows.map(row=>row.workingSetBytes),50),p95:nearestRank(rows.map(row=>row.workingSetBytes),95)})),
    workingSetScope:'descriptive-only'};
}
function actionTrends(records,origin){
  const groups=new Map(),slots=new Map(),tail=[],boundary=[];let pending=null;
  for(const row of records){
    if(row.event==='step.begin'){
      need(!pending,'overlapping-action-observation',true);pending=row;
    }else if(row.event==='step.complete'){
      need(pending&&row.action===pending.action&&row.window===pending.window&&row.cycle===pending.cycle&&integer(row.elapsedMs)&&
        row.elapsedMs>=pending.elapsedMs,'unpaired-action-observation',true);
      const begin=pending;pending=null;
      const item={action:row.action,window:row.window,cycle:row.cycle,beginMs:begin.elapsedMs,endMs:row.elapsedMs,durationMs:row.elapsedMs-begin.elapsedMs};
      if(row.cycle===0||row.action==='idle'||row.action.startsWith('runtime.')){boundary.push(item);continue;}
      const positionKey=row.cycle+'|'+row.action+'|'+row.window,position=(slots.get(positionKey)||0)+1;slots.set(positionKey,position);
      const key=row.action+'|window='+row.window+'|position='+position;
      const hour=Math.floor((begin.elapsedMs-origin)/policy.HOUR);
      if(hour>=0&&hour<8&&row.elapsedMs<origin+policy.DURATION){
        if(!groups.has(key))groups.set(key,Array.from({length:8},()=>[]));groups.get(key)[hour].push(item);
      }else tail.push({...item,key});
    }
  }
  need(!pending,'unterminated-action-observation',true);need(groups.size>0,'missing-repeated-business-actions');
  const output=[];
  for(const [key,hours] of groups){
    const complete=hours.every(rows=>rows.length>=policy.RULE.minActionsPerGroupPerHour);
    output.push(complete?{key,counts:hours.map(rows=>rows.length),...checkSeries(quantiles(hours,'durationMs'),25,100,25,25)}:
      {key,counts:hours.map(rows=>rows.length),gate:NE,failure:'action-window-position-missing-in-complete-hour'});
  }
  return {gate:combine(output.map(value=>value.gate)),groups:output,boundaryActions:boundary,tailActions:tail,
    scope:'Whole native business action duration by action/window/within-cycle position; not RTT or physical presentation'};
}
function observedRaster(reader,observation,clock,subject,modulePin,ended){
  try{return reader.snapshot(observation,clock,subject,modulePin,ended);}
  catch(error){
    // The existing snapshot already checked identity, clocks, accounting and no GPU.
    // Only a valid READY prefix before the first completed CPU draw may wait.
    if(!ended&&error.message==='completed-raster-draw-not-observed'&&observation?.state==='ready'&&
      observation.lastDrawKind===0&&(observation.flags&0x68)===0&&
      ['rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc','lastDrawThreadId','lastDrawProcessId']
        .every(name=>owners.count(observation[name])===0n)){
      return {values:Object.fromEntries(reader.u64Names.map(name=>[name,owners.count(observation[name])])),
        captured:owners.count(clock.qpc),observation,prefixBeforeFirstDraw:true};
    }
    throw error;
  }
}
function nativeScope(records,subject,modulePin){
  const raster=require('./native-renderer-validation.cjs'),modelReader=require('./native-renderer-model-validation.cjs');
  let prior=null,terminal=null,observations=0;
  const selected=records.filter(row=>row.event==='sample'&&row.phase!=='process'||
    row.event==='step.complete'&&['runtime.shutdown','runtime.shutdown-idempotent'].includes(row.action));
  for(const row of selected){
    const sample=row.event==='sample',native=sample?row.nativeSdk:row.nativeRenderer;
    const model=sample?native?.rendererModel:native?.model,clock=native?.captureClock;
    const ended=sample?row.phase==='runtime-closed':true;
    if(sample)need(native?.pid===String(subject.pid)&&native.creationFileTime===subject.creationFileTime&&native.captureThreadId===clock?.captureThreadId,
      'native-sample-owned-identity-differs',true);
    need(model?.schemaVersion===2&&model.scope===modelReader.scope&&['ready','closed'].includes(model.state),'complete-native-model-unobserved');
    need(model.structSize===264&&model.abiVersion===2&&model.status===(model.state==='ready'?1:4)&&model.flags===19&&
      model.providerId===1&&model.backendId===1&&model.factoryCoverageMask===127&&model.compiledGpuSupport===0,'native-model-coverage-unobserved');
    need(model.moduleMatches===true&&model.identityBound===true&&model.modulePath===model.compositor?.modulePath&&
      model.state===model.compositor.state&&model.status===model.compositor.status&&model.terminalSnapshot===model.compositor.terminalSnapshot,
      'native-model-compositor-identity-differs',true);
    const separate=observedRaster(raster,native?.renderer,clock,subject,modulePin,ended);
    if(prior){
      need(['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch','initializedQpc']
        .every(name=>separate.values[name]===prior.values[name]),'separate-native-getter-owner-or-epoch-changed',true);
      need(['renderersCreatedTotal','renderersClosedTotal','rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc']
        .every(name=>separate.values[name]>=prior.values[name])&&separate.captured>=prior.captured,'separate-native-history-regressed',true);
    }
    if(terminal)need(same(separate.observation,terminal.compositor),'first-v1-terminal-overwritten',true);
    const current=observedRaster(raster,model.compositor,clock,subject,modulePin,ended),v=current.values;
    need(['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch','initializedQpc']
      .every(name=>v[name]===separate.values[name])&&['renderersCreatedTotal','renderersClosedTotal','rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc']
      .every(name=>v[name]>=separate.values[name]),'sequential-live-getter-identity-or-history-differs',true);
    const initialized=owners.count(model.backendInitializationQpc);
    need(initialized>0n&&initialized<=v.initializedQpc,'native-backend-initialization-unobserved');
    for(const name of modelReader.countNames)need(owners.count(model[name])===0n,'extra-or-unknown-rendering-factory-observed',true);
    // Sequential live getters are not compared for equality. Each remains a valid
    // observation with monotone history; the nested v1 is the same-lock model snapshot.
    if(prior){
      need(initialized===prior.initialized&&['processId','processCreationFileTime','engineOwnerThreadId','qpcFrequency','recordingEpoch','initializedQpc']
        .every(name=>v[name]===prior.values[name]),'native-owner-or-epoch-changed',true);
      need(['renderersCreatedTotal','renderersClosedTotal','rasterDrawsTotal','gpuSkiaDrawsTotal','drawEventsTotal','lastDrawQpc']
        .every(name=>v[name]>=prior.values[name])&&current.captured>=prior.captured,'native-history-regressed',true);
    }
    if(terminal)need(same(model,terminal),'first-native-terminal-overwritten',true);
    if(ended&&!terminal)terminal=model;
    prior={initialized,values:v,captured:current.captured};++observations;
  }
  need(terminal&&observations>0,'missing-native-terminal-evidence');
  return {gate:'passed',scope:'bound-compatibility-backend-workload',observations,modulePin,terminalSnapshot:terminal,
    exclusions:['OS DWM/other-process GPU activity','other workloads and target OS certification']};
}
function analyzeLong(result,eventRaw,evidence){
  const output={schema:'soui.long-run-acceptance-analysis/v1',mode:result?.config?.mode,runId:result?.runId,
    gates:Object.fromEntries((['mixed','session'].includes(result?.config?.mode)?durationGates:ordinaryGates).map(name=>[name,name==='operations'&&result?.passed===false?'failed':NE])),passed:false,releaseReady:false};
  try{
    const request=parseRaw(evidence.requestRaw),config=policy.fixedConfig(request.mode);
    policy.validateFrozenRequest(request,{consumerBuildId:result.consumerBuildId,sourceSha256:result.sourceSha256,payload:{sha256:result.payloadSha256}},evidence.runRoot);
    need(policy.sameConfig(result.config,config)&&result.schema==='soui.acceptance-run/v1'&&result.runId===request.runId&&
      evidence.policyRaw.equals(policy.policyBytes(config.mode))&&evidence.currentInputsMatched===true&&
      result.sourceSha256===evidence.sourceSha256&&result.payloadSha256===evidence.payloadSha256,'current-long-inputs-not-bound');
    Object.assign(output,{policySha256:policy.hash(evidence.policyRaw),sourceSha256:result.sourceSha256,
      payloadSha256:result.payloadSha256,consumerBuildId:result.consumerBuildId,inputBindingScope:'complete-acceptance-inputs'});
    output.gates.inputs='passed';
    need(result.passed===true&&!result.failure,'original-operation-failed',true);
    const records=parseRaw(eventRaw,true);
    need(policy.hash(eventRaw)===result.logSha256,'original-native-log-changed',true);
    output.logSha256=policy.hash(eventRaw);
    const owned=verifyOwned(result,evidence,config);
    const runner=require('./runner.cjs');let operations,clock;
    try{
      runner.verifyNativeIdentity(records,result.outcome);
      clock=verifyRunClock(records,config,owned);
      operations=config.mode==='session'?runner.validateSessionRecords(records,config,result.outcome):
        config.mode==='mixed'?runner.validateMixedRecords(records,config,result.outcome):runner.validateRecords(records,config,result.outcome);
    }catch(error){throw new EvidenceError('original-operation-observation:'+error.message,true);}
    Object.assign(output,{policySha256:policy.hash(evidence.policyRaw),logSha256:policy.hash(eventRaw),
      sourceSha256:result.sourceSha256,payloadSha256:result.payloadSha256,consumerBuildId:result.consumerBuildId,
      inputBindingScope:'complete-acceptance-inputs',operations,subject:owned.subject,actualClock:clock});
    Object.assign(output.gates,{inputs:'passed',operations:'passed',owned:'passed',tree:owned.treeGate});
    if(config.durationMs===undefined){
      need(operations.measuredCycles===1000,'thousand-cycle-count-differs',true);
      output.thousandCycleOperationGate='passed';
      output.requiredGates=['inputs','operations','owned'];
      output.longRunGate=combine(output.requiredGates.map(name=>output.gates[name]));
      output.resourceGate=NE;output.eightHourGate=NE;
      output.coverage='Only this public mode: 1000 same-process operations; other modes and process restart remain separate';
    }else{
      const sampled=gate(()=>hourlySamples(records,config));output.sampling=sampled;output.gates.sampling=sampled.gate;
      output.duration=gate(()=>{
        const origin=originFor(records,config);
        const end=config.mode==='session'?records.find(row=>row.event==='session.end'):records.at(-1);
        need(end&&end.elapsedMs-origin>=policy.DURATION&&owned.elapsedMs>=config.durationMs,'eight-real-hours-not-complete',true);
        return {gate:'passed',originMs:origin,endMs:end.elapsedMs,elapsedMs:end.elapsedMs-origin,ownedElapsedMs:owned.elapsedMs};
      });output.gates.duration=output.duration.gate;
      output.resources=sampled.gate==='passed'?gate(()=>resourceTrends(sampled)):{gate:NE,failure:'hourly-resource-observation-insufficient'};output.gates.resources=output.resources.gate;
      output.actions=gate(()=>actionTrends(records,originFor(records,config)));output.gates.actions=output.actions.gate;
      output.native=owned.treeGate==='passed'?gate(()=>nativeScope(records,owned.subject,evidence.modulePin)):
        {gate:NE,failure:'Compatibility single-owned-tree not proved'};
      output.gates.native=output.native.gate;
      output.owners=owners.analyzeOwners(records);
      output.namedCaches=owned.treeGate==='passed'?namedCacheObservations(records,owned.subject,evidence.modulePin,config):
        {scope:policy.RULE.namedCacheScope,dataGate:NE,resourceGate:NE,qualified:false,failure:'single-owned-tree-not-proved'};
      output.gates.ownerCaps=output.owners.capacityGate;output.gates.terminalDrain=output.owners.terminalDrainGate;
      output.gates.cacheBudget=output.owners.cacheBudgetGate;
      output.gates.namedCacheConfiguration=output.namedCaches.configurationGate||NE;
      output.gates.namedCacheCoverage=output.namedCaches.freshCoverageGate||NE;
      output.requiredGates=[...durationGates];
      output.eightHourGate=combine(output.requiredGates.map(name=>output.gates[name]));
      output.resourceGate=combine(['inputs','tree','sampling','resources','native','ownerCaps','terminalDrain','namedCacheConfiguration','namedCacheCoverage','cacheBudget'].map(name=>output.gates[name]));
      output.longRunGate=output.eightHourGate;output.thousandCycleOperationGate=NE;
      output.coverage=config.mode==='session'?'same-document authenticated task/subscription renewals':'mixed lifecycle business';
      output.retention={originalEvents:records.length,rawLogSha256:policy.hash(eventRaw),allTailAndBoundaryEventsRetained:true};
    }
    Object.assign(output,aggregate(config.mode,output.gates));
  }catch(error){
    output.failure=error.message||'invalid-long-observation';
    if(error.failed)output.gates.operations='failed';
    try{Object.assign(output,aggregate(output.mode,output.gates));}catch{output.longRunGate=NE;output.resourceGate=NE;output.eightHourGate=NE;output.thousandCycleOperationGate=NE;}
    output.observationFailure=error.message||'invalid-long-observation';
  }
  return output;
}
function readBounded(file,limit=256*1024*1024){
  const current=identity.safePath(file),stat=fs.statSync(current);
  need(stat.isFile()&&stat.size>0&&stat.size<=limit,'missing-or-unbounded-long-file');return fs.readFileSync(current);
}
function readPinned(pin){
  need(pin&&integer(pin.bytes,1)&&typeof pin.sha256==='string','missing-long-file-pin');
  const raw=readBounded(pin.path);need(raw.length===pin.bytes&&policy.hash(raw)===pin.sha256,'long-pinned-file-changed');return raw;
}
function analyzeFile(resultPath,bindingPath){
  try{
    const resultRaw=readBounded(resultPath,16*1024*1024),bindingRaw=readBounded(bindingPath,16*1024*1024);
    const result=parseRaw(resultRaw),binding=parseRaw(bindingRaw),runRoot=path.dirname(resultPath),files=require('./resource-file-bindings.cjs');
    need(samePath(resultPath,path.join(runRoot,'operation-result.json'))&&samePath(bindingPath,path.join(runRoot,'file-bindings.json')),
      'long-analysis-needs-fixed-original-files');
    const complete=files.verifyCompleteBinding(binding,runRoot,result);
    const report=path.join(runRoot,'owned-process.ndjson'),eventLogPath=path.join(runRoot,'events.ndjson');
    need(samePath(result.outcome?.ownedProcess?.report,report),'owned-report-outside-fixed-run');
    const eventRaw=readBounded(eventLogPath),requestRaw=readBounded(path.join(runRoot,'request.json'),16*1024*1024);
    const ownedRaw=readBounded(report),contextRaw=readBounded(report+'.context.json',16*1024*1024);
    const policyRaw=readPinned(binding.policy),runnerRaw=readPinned(binding.runner),executable=binding.executablePath;
    const exe=binding.payload.files.find(file=>file.path===path.basename(executable));
    need(exe&&exe.normalization!=='lf','original-long-executable-pin-missing');
    const analysis=analyzeLong(result,eventRaw,{requestRaw,policyRaw,ownedRaw,contextRaw,runRoot,currentInputsMatched:true,
      sourceSha256:complete.sourceSha256,payloadSha256:complete.payloadSha256,runnerSha256:policy.hash(runnerRaw),
      executableSha256:exe.sha256,executablePath:executable,payloadRoot:binding.payload.root,eventLogPath,modulePin:complete.loadedPayloadModule});
    const final=files.verifyCompleteBinding(binding,runRoot,result);
    need(same(final,complete)&&readBounded(resultPath).equals(resultRaw)&&readBounded(bindingPath).equals(bindingRaw)&&
      readBounded(eventLogPath).equals(eventRaw)&&readBounded(path.join(runRoot,'request.json')).equals(requestRaw)&&
      readBounded(report).equals(ownedRaw)&&readBounded(report+'.context.json').equals(contextRaw)&&
      readPinned(binding.policy).equals(policyRaw)&&readPinned(binding.runner).equals(runnerRaw),'complete-long-record-or-input-changed');
    return analysis;
  }catch(error){return {schema:'soui.long-run-acceptance-analysis/v1',longRunGate:NE,resourceGate:NE,eightHourGate:NE,
    thousandCycleOperationGate:NE,gates:{inputs:NE},passed:false,releaseReady:false,failure:error.message||'invalid-long-file-observation'};}
}
function checkAnalysisClock(analysis,operation){
  if(analysis.gates.operations!=='passed'&&analysis.gates.owned!=='passed')return;
  const clock=analysis.actualClock,elapsed=operation.outcome?.ownedProcess?.terminal?.elapsedMs,config=policy.fixedConfig(operation.config.mode);
  need(clock?.gate==='passed'&&integer(elapsed)&&integer(clock.nativeRunCompleteElapsedMs)&&
    clock.ownedTerminalElapsedMs===elapsed&&clock.nativeRunCompleteElapsedMs<=elapsed&&
    clock.requestedDurationMs===(config.durationMs??null)&&elapsed<=processBudget(config)+180000+2000+15000,
    'inconsistent-analysis-original-clock');
  if(config.durationMs!==undefined){
    need(elapsed>=config.durationMs,'analysis-owned-duration-shorter-than-request');
    if(analysis.gates.duration==='passed'){
      const d=analysis.duration;
      need(d&&integer(d.originMs)&&integer(d.endMs)&&d.endMs>=d.originMs&&d.elapsedMs===d.endMs-d.originMs&&
        d.elapsedMs>=config.durationMs&&d.ownedElapsedMs===elapsed&&d.endMs<=clock.nativeRunCompleteElapsedMs,
        'inconsistent-eight-hour-native-owned-window');
    }
  }
}
function captureResult(operation,operationPin,policyPin,evidence){
  need(evidence&&typeof evidence==='object'&&!Array.isArray(evidence)&&Object.keys(evidence).every(key=>['bindingFile','inputs','analysis','failure','finalVerified'].includes(key)),
    'fixed-long-capture-evidence-required');
  const analysis=evidence.analysis?.record,inputs=evidence.inputs;
  let coherent=false,contractFailure=null;
  if(analysis){try{checkedAggregate(analysis,operation.config.mode);checkAnalysisClock(analysis,operation);coherent=true;}catch(error){contractFailure=error.message;}}
  const matched=coherent&&operation.passed===true&&evidence.finalVerified===true&&evidence.failure===null&&
    inputs?.inputsMatched===true&&inputs.inputBindingScope==='complete-acceptance-inputs'&&inputs.closureObserved===false&&
    inputs.nativeScopeGate==='not-observed'&&inputs.resourceGate===NE&&inputs.passed===false&&inputs.releaseReady===false&&
    analysis?.schema==='soui.long-run-acceptance-analysis/v1'&&analysis.runId===operation.runId&&analysis.mode===operation.config.mode&&
    analysis.consumerBuildId===operation.consumerBuildId&&analysis.sourceSha256===operation.sourceSha256&&
    analysis.payloadSha256===operation.payloadSha256&&analysis.policySha256===policyPin.sha256&&analysis.logSha256===operation.logSha256&&
    analysis.releaseReady===false;
  return {schema:'soui.acceptance-long-capture/v1',runId:operation.runId,config:operation.config,
    operationPassed:operation.passed,operationResult:operationPin,policy:policyPin,bindingFile:evidence.bindingFile,
    analysis:evidence.analysis?.file||null,gates:matched?analysis.gates:null,longRunGate:matched?analysis.longRunGate:operation.passed===false?'failed':NE,
    resourceGate:matched?analysis.resourceGate:NE,eightHourGate:matched?analysis.eightHourGate:NE,
    thousandCycleOperationGate:matched?analysis.thousandCycleOperationGate:NE,
    passed:matched&&analysis.passed===true&&analysis.longRunGate==='passed',releaseReady:false,
    failure:matched?analysis.failure:contractFailure||evidence.failure||operation.failure||'complete-current-long-analysis-not-bound'};
}
module.exports={nearestRank,percentCeil,amplitude,trend,checkSeries,combine,nativeArgs,namedCacheObservations,processBudget,verifyOwned,
  verifyRunClock,originFor,hourlySamples,resourceTrends,actionTrends,nativeScope,requiredGates,aggregate,checkedAggregate,analyzeLong,analyzeFile,captureResult};
