'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isUtf8} = require('node:buffer');
const {validateRecords} = require('./runner.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');

const POLICY = Object.freeze({schema:'soui.software-reload-resource-policy/v1', version:'0.5.0',
  kernel:'miniblink', renderer:'software', processModel:'single-process', mode:'reload',
  warmup:10, measuredCycles:100, settleMs:5000, baselineCycle:10, tailCycles:10,
  privateMinimumGrowthBytes:16*1024*1024, privateBaselineFractionNumerator:1,
  privateBaselineFractionDenominator:10, slopeCycles:100,
  counterTailGrowth:0, counterSlope:0, windowClasses:'exact-baseline-each-measured-cycle',
  workingSet:'descriptive-only', forcedCollection:false, releaseReady:false});
const policyText = JSON.stringify(POLICY, null, 2) + '\n';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const POLICY_SHA256 = hash(Buffer.from(policyText));
const policyBytes = () => Buffer.from(policyText);
const CALIBRATION_POLICY=Object.freeze({...POLICY,revision:'0.5.0-named-cache-calibration-v1',
  namedCacheObservation:'named-process-cache-scalars',calibratedByteBudgets:null});
const calibrationText=JSON.stringify(CALIBRATION_POLICY,null,2)+'\n';
const CALIBRATION_POLICY_SHA256=hash(Buffer.from(calibrationText));
const calibrationPolicyBytes=()=>Buffer.from(calibrationText);
function resourcePolicySelection(request){
  const enabled=Object.prototype.hasOwnProperty.call(request||{},'namedCacheObservation');
  requireObservation(!enabled||request.namedCacheObservation===CALIBRATION_POLICY.namedCacheObservation,
    'invalid-resource-cache-selection');
  const digest=enabled?CALIBRATION_POLICY_SHA256:POLICY_SHA256;
  requireObservation(request?.resourcePolicySha256===digest,'request-cache-policy-choice-differs');
  return {cacheObservation:enabled,policySha256:digest,policyRaw:enabled?calibrationPolicyBytes():policyBytes()};
}
function resourceNativeArgs(request,eventLogPath){
  const selection=resourcePolicySelection(request);
  return [`--mode=${request.mode}`,`--cycles=${request.cycles}`,`--warmup=${request.warmup}`,
    `--settle-ms=${request.settleMs}`,`--timeout-ms=${request.timeoutMs}`,`--report=${eventLogPath}`,
    ...(selection.cacheObservation?['--cache-observation=1']:[])];
}
function cacheCalibration(records,subject,modulePin){
  const long=require('./long-run-validation.cjs');
  const observed=long.namedCacheObservations(records,subject,modulePin);
  const points=observed.configurationPoints.filter(row=>row.phase==='cycle'&&row.cycle>=10&&row.cycle<=110);
  const complete=points.length===101&&points.every((row,i)=>row.cycle===i+10);
  const configurationGate=observed.configurationFailures.length?'failed':
    complete?long.combine(points.map(row=>row.configurationGate)):'not-evaluated';
  const freshCoverageGate=complete?long.combine(points.map(row=>row.freshnessGate)):'not-evaluated';
  return {schema:'soui.reload-named-cache-calibration/v1',scope:'baseline-10-and-100-measured-reload-cache-copies',
    configurationGate,freshCoverageGate,finiteGate:long.combine([configurationGate,freshCoverageGate]),
    expectedPoints:101,observedPoints:points.length,points,diagnostics:observed,
    cacheBudgetGate:'not-evaluated',calibratedByteBudgetGate:'not-evaluated',calibratedByteBudgets:null,
    qualified:false,releaseReady:false};
}
function privateStorageCalibration(records,binding){
  const owners=require('./long-run-owners.cjs'),diagnostics=owners.privateStorageObservations(records,binding);
  const points=records.flatMap((row,eventIndex)=>row.event==='sample'&&row.phase==='cycle'&&row.cycle>=10&&row.cycle<=110?
    [{eventIndex,cycle:row.cycle,points:diagnostics.points.filter(point=>point.eventIndex===eventIndex)}]:[]);
  const complete=points.length===101&&points.every((point,i)=>point.cycle===i+10);
  const finiteGate=diagnostics.counts.invalid?'failed':diagnostics.bindingGate==='matched'&&complete&&
    points.every(point=>point.points.length>0&&point.points.every(value=>value.derived!==null))?'passed':'not-evaluated';
  return {schema:'soui.reload-private-storage-calibration/v1',scope:owners.privateScope,
    expectedPoints:101,observedPoints:points.length,finiteGate,points,diagnostics,
    fullCacheBudget:'not-evaluated',calibratedByteBudget:'not-evaluated',configuration:null,budgets:null,
    quota:null,calibrationPin:null,qualified:false,releaseReady:false};
}
const metrics = ['privateBytes','workingSetBytes','handles','threads','gdiObjects','userObjects'];
const counters = metrics.slice(2);
const shaPattern = /^[a-f0-9]{64}$/;
class ObservationError extends Error {
  constructor(code, evaluated=false) { super(code); this.evaluated=evaluated; }
}
const requireObservation = (condition, code) => { if(!condition)throw new ObservationError(code); };
const failRule = (condition, code) => { if(!condition)throw new ObservationError(code,true); };
function ratio(n,d=1n) {
  let left=n<0n?-n:n, right=d;
  while(right) { const remainder=left%right; left=right; right=remainder; }
  return {n:n/(left||1n),d:d/(left||1n)};
}
const compare = (a,b) => a.n*b.d < b.n*a.d ? -1 : a.n*b.d > b.n*a.d ? 1 : 0;
function middle(sorted) {
  const at=Math.floor(sorted.length/2);
  if(sorted.length%2)return sorted[at];
  const a=sorted[at-1], b=sorted[at];
  return ratio(a.n*b.d+b.n*a.d,2n*a.d*b.d);
}
function medianRatio(values) {
  if(!Array.isArray(values)||!values.length||values.some(value=>!Number.isSafeInteger(value)||value<0))
    throw new Error('Median requires nonnegative safe integer observations');
  return middle(values.map(value=>ratio(BigInt(value))).sort(compare));
}
function theilSen(values) {
  if(!Array.isArray(values)||values.length<2||values.some(value=>!Number.isSafeInteger(value)||value<0))
    throw new Error('Theil-Sen requires at least two nonnegative integer observations');
  const pairs=[];
  for(let i=0;i<values.length;i++)for(let j=i+1;j<values.length;j++)
    pairs.push(ratio(BigInt(values[j])-BigInt(values[i]),BigInt(j-i)));
  return middle(pairs.sort(compare));
}
const printable = value => ({numerator:value.n.toString(),denominator:value.d.toString(),
  value:Number(value.n)/Number(value.d)});
function parseRaw(raw, ndjson=false) {
  requireObservation(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=16*1024*1024&&isUtf8(raw),'missing-or-invalid-raw');
  const text=raw.toString('utf8');
  requireObservation(text.endsWith('\n'),'truncated-raw');
  try { return ndjson ? text.slice(0,-1).split('\n').map(line=>JSON.parse(line)) : JSON.parse(text); }
  catch { throw new ObservationError('invalid-raw-json'); }
}
function classKey(value) {
  requireObservation(value && typeof value==='object' && !Array.isArray(value),'missing-window-classes');
  const entries=Object.entries(value).sort(([a],[b])=>a<b?-1:a>b?1:0);
  requireObservation(entries.length>0&&entries.every(([name,count])=>name.length>0&&Number.isSafeInteger(count)&&count>0),'invalid-window-classes');
  return JSON.stringify(entries);
}
function verifyOwned(result,evidence,request) {
  const outcome=result.outcome, finished=outcome?.ownedProcess;
  requireObservation(outcome?.timedOut===false&&outcome.exitCode===0&&!outcome.signal&&!outcome.spawnError&&
    Number.isSafeInteger(outcome.pid)&&outcome.pid>0&&finished,'missing-natural-owned-outcome');
  requireObservation(finished.exitCode===0&&finished.supervisorExitCode===0&&finished.passed===true&&
    finished.stopRequested===false&&finished.fallbackUsed===false&&finished.closureConfirmed===true&&
    !finished.spawnError&&!finished.reportError&&!finished.signal,'owned-not-natural-success');
  const rows=parseRaw(evidence.ownedRaw,true), context=parseRaw(evidence.contextRaw);
  const start=rows[0], terminal=rows.at(-1), members=rows.filter(value=>value.event==='owned-member');
  requireObservation(start?.event==='start'&&start.schema==='soui.owned-process-run/v1'&&
    rows.filter(value=>value.event==='start').length===1&&rows.filter(value=>value.event==='terminal').length===1&&
    terminal?.event==='terminal'&&Number.isSafeInteger(start.pid)&&start.pid>0&&start.pid<=0xffffffff&&
    typeof start.creationFileTime==='string'&&/^[1-9]\d{0,19}$/.test(start.creationFileTime)&&
    BigInt(start.creationFileTime)<=0xffffffffffffffffn&&start.pid===outcome.pid&&start.pid===finished.pid&&
    start.creationFileTime===finished.creationFileTime&&start.jobAssignedBeforeResume===true&&
    start.breakawayAllowed===false,'owned-identity-not-bound');
  requireObservation(terminal.reason==='completed'&&terminal.rootExited===true&&terminal.rootExitCode===0&&
    terminal.activeBeforeCleanup===0&&terminal.terminatedOwnedJob===false&&terminal.closureConfirmed===true&&
    terminal.activeProcesses===0&&terminal.totalProcesses===1&&terminal.limitTerminatedProcesses===0&&
    terminal.passed===true&&JSON.stringify(terminal)===JSON.stringify(finished.terminal)&&
    members.length>0&&members.every(value=>value.pid===start.pid&&value.creationFileTime===start.creationFileTime),
    'entire-job-single-process-not-observed');
  const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
  const args=resourceNativeArgs(request,evidence.eventLogPath);
  requireObservation(context.schema==='soui.owned-process-context/v1'&&
    samePath(context.executable,evidence.executablePath)&&samePath(context.cwd,evidence.payloadRoot)&&
    members.every(value=>samePath(value.image,evidence.executablePath))&&
    JSON.stringify(context.args)===JSON.stringify(args)&&start.timeoutMs===context.timeoutMs&&
    start.drainMs===context.drainMs&&context.executableSha256===evidence.executableSha256&&
    context.runnerSha256===evidence.runnerSha256&&finished.runnerSha256===evidence.runnerSha256&&
    finished.executableSha256===evidence.executableSha256&&hash(evidence.ownedRaw)===finished.reportSha256,
    'owned-context-not-bound');
  return {pid:start.pid,creationFileTime:start.creationFileTime,entireJobProcesses:1,
    nativeExitCode:0,supervisorExitCode:0,naturalClosure:true,forcedClosure:false};
}
function analyzeResource(result,raw,evidence) {
  const output={schema:'soui.software-reload-resource-analysis/v1', policySha256:POLICY_SHA256,
    resourceGate:'not-evaluated', passed:false, releaseReady:false, m0Complete:false,
    sdkPrivateCacheQueueGate:'not-evaluated', thousandCycleOperationGate:'not-evaluated',
    eightHourGate:'not-evaluated', targetOsGate:'not-evaluated'};
  try {
    requireObservation(result?.schema==='soui.acceptance-run/v1'&&result.passed===true&&result.releaseReady===false&&
      typeof result.runId==='string'&&result.runId.length>0&&
      typeof result.consumerBuildId==='string'&&result.consumerBuildId.length>0&&evidence,'missing-operation-result');
    failRule(result.config?.mode==='reload'&&result.config.cycles===100&&result.config.warmup===10&&
      result.config.settleMs===5000,'wrong-resource-scenario');
    const request=parseRaw(evidence.requestRaw);
    const selection=resourcePolicySelection(request);
    requireObservation(Buffer.isBuffer(evidence.policyRaw)&&evidence.policyRaw.equals(selection.policyRaw),'missing-or-different-frozen-policy');
    output.policySha256=selection.policySha256;
    requireObservation(request.schema==='soui.acceptance-request/v1'&&request.runId===result.runId&&
      request.consumerBuildId===result.consumerBuildId&&request.resourcePolicySha256===selection.policySha256&&
      Object.keys(result.config).every(key=>request[key]===result.config[key])&&
      request.resourceScope?.kernel==='miniblink'&&request.resourceScope.renderer==='software'&&
      request.resourceScope.processModel==='single-process'&&Object.keys(request.resourceScope).length===3,
      'request-policy-scope-not-bound');
    requireObservation(evidence.currentInputsMatched===true&&shaPattern.test(evidence.sourceSha256)&&
      shaPattern.test(evidence.payloadSha256)&&shaPattern.test(evidence.executableSha256)&&shaPattern.test(evidence.runnerSha256)&&
      result.sourceSha256===evidence.sourceSha256&&request.sourceSha256===evidence.sourceSha256&&
      result.payloadSha256===evidence.payloadSha256&&request.payloadSha256===evidence.payloadSha256&&
      hash(raw)===result.logSha256,'missing-current-input-or-log-binding');
    const records=parseRaw(raw,true);
    output.subject=verifyOwned(result,evidence,request);
    if(selection.cacheObservation)output.cacheObservation=true;
    requireObservation(typeof records[0]?.creationFileTime==='string'&&
      records[0].creationFileTime===output.subject.creationFileTime,'native-creation-file-time-not-bound');
    let validated;
    try { validated=validateRecords(records,result.config,result.outcome); }
    catch(error) {
      if(/sampling|measurement|Missing run boundaries/.test(error.message))throw new ObservationError('missing-complete-sampling');
      throw new ObservationError('invalid-lifecycle-transcript',true);
    }
    failRule(Number.isSafeInteger(result.outcome.ownedProcess.terminal.elapsedMs)&&
      records.at(-1).elapsedMs<=result.outcome.ownedProcess.terminal.elapsedMs,'native-time-exceeds-owned-observation');
    requireObservation(result.validation?.completedSteps===validated.completedSteps&&
      result.validation?.measuredCycles===validated.measuredCycles,'operation-result-transcript-differs');
    const cycles=validated.samples.filter(sample=>sample.phase==='cycle');
    requireObservation(cycles.length===110,'missing-cycle-sampling');
    for(const sample of cycles) {
      const at=records.indexOf(sample), previous=records[at-1];
      failRule(previous?.event==='step.complete'&&previous.action==='reload'&&previous.cycle===sample.cycle&&
        sample.elapsedMs-previous.elapsedMs>=5000,'settled-interval-shortened');
    }
    const baseline=cycles[9], measured=cycles.slice(10), baselineClasses=classKey(baseline.windowClasses);
    const classKeys=measured.map(sample=>classKey(sample.windowClasses));
    const allowance=BigInt(Math.max(16*1024*1024,Number((BigInt(baseline.privateBytes)+9n)/10n)));
    const resources={};
    for(const metric of metrics) {
      const values=measured.map(sample=>sample[metric]), tail=medianRatio(values.slice(-10)), slope=theilSen(values);
      const growth=ratio(tail.n-BigInt(baseline[metric])*tail.d,tail.d);
      const limit=metric==='privateBytes'?allowance:0n, gated=metric!=='workingSetBytes';
      resources[metric]={baseline:baseline[metric],count:100,min:Math.min(...values),max:Math.max(...values),
        first:values[0],last:values.at(-1),tailMedian:printable(tail),tailMedianGrowth:printable(growth),
        theilSenPerCycle:printable(slope),theilSenPairs:4950,gated,
        ...(gated?{tailGrowthLimit:Number(limit),slopeLimitPerCycle:printable(ratio(limit,100n)),
          tailMedianWithinLimit:growth.n<=limit*growth.d,slopeWithinLimit:slope.n*100n<=limit*slope.d}:{})};
    }
    const classesStable=classKeys.every(key=>key===baselineClasses);
    const passed=classesStable&&['privateBytes',...counters].every(metric=>resources[metric].tailMedianWithinLimit&&resources[metric].slopeWithinLimit);
    Object.assign(output,{runId:result.runId,sourceSha256:result.sourceSha256,payloadSha256:result.payloadSha256,
      nativeLogSha256:result.logSha256,ownedLogSha256:hash(evidence.ownedRaw),contextSha256:hash(evidence.contextRaw),
      requestSha256:hash(evidence.requestRaw),policyRawSha256:hash(evidence.policyRaw),baseline,
      measuredSamples:measured,allSamples:selection.cacheObservation?records:validated.samples,completedSteps:validated.completedSteps,
      measuredCycles:100,resources,windowClassesStable:classesStable,resourceGate:passed?'passed':'failed',passed,
      ownershipScope:'process window classes and four OS counter trends; SDK private caches/queues remain pending'});
    if(selection.cacheObservation){
      output.cacheModulePin=evidence.modulePin||null;
      output.namedCacheCalibration=cacheCalibration(records,output.subject,output.cacheModulePin);
      output.consumerBuildId=result.consumerBuildId;
      output.privateStorageBinding=require('./long-run-owners.cjs').privateBinding(result,output.subject,evidence.executablePin,
        {ownedLogSha256:output.ownedLogSha256,contextSha256:output.contextSha256},evidence.completeInputsMatched===true);
      output.privateStorageCalibration=privateStorageCalibration(records,output.privateStorageBinding);
    }
  } catch(error) {
    output.resourceGate=error instanceof ObservationError&&error.evaluated?'failed':'not-evaluated';
    output.failure=error instanceof ObservationError?error.message:'invalid-observation';
  }
  return output;
}

function readPinned(pin) {
  requireObservation(pin&&path.isAbsolute(pin.path)&&Number.isSafeInteger(pin.bytes)&&pin.bytes>=0&&shaPattern.test(pin.sha256),'missing-input-pin');
  const file=identity.safePath(pin.path), bytes=fs.readFileSync(file);
  requireObservation(fs.statSync(file).isFile()&&bytes.length===pin.bytes&&hash(bytes)===pin.sha256,'input-pin-changed');
  return bytes;
}
function verifySnapshot(snapshot) {
  requireObservation(snapshot&&path.isAbsolute(snapshot.root)&&Array.isArray(snapshot.files)&&snapshot.files.length>0&&
    shaPattern.test(snapshot.sha256)&&hash(identity.encoded(snapshot.files))===snapshot.sha256&&
    Array.isArray(snapshot.inventoryRoots)&&snapshot.inventoryRoots.length>0,'missing-full-current-snapshot');
  identity.validateFiles(snapshot.files);
  if (snapshot.inventoryMode !== undefined) {
    requireObservation(snapshot.inventoryMode === 'sdk-source', 'invalid-snapshot-inventory-mode');
    const current = identity.sourceSnapshot(snapshot.root);
    requireObservation(JSON.stringify(snapshot.inventoryRoots) === JSON.stringify(current.inventoryRoots) &&
      snapshot.sha256 === current.sha256 &&
      JSON.stringify(snapshot.files) === JSON.stringify(current.files), 'current-sdk-source-inventory-changed');
  }
  const names=new Set(snapshot.files.map(file=>file.path));
  for(const file of snapshot.files) {
    const bytes=fs.readFileSync(identity.safePath(snapshot.root,file.path));
    const current=identity.normalize(bytes,file.normalization||'none');
    requireObservation(current.length===file.bytes&&hash(current)===file.sha256,'current-snapshot-changed');
  }
  // Every file in each declared input root must be in the frozen manifest.
  // The same existing source-directory exclusions apply; source coverage is
  // chosen and frozen by Root, not inferred as complete compiler provenance.
  const legacyExcludes = new Set(['out','dist','dist-legacy','node_modules','.git','.vs']);
  for(const relative of snapshot.inventoryRoots) {
    requireObservation(typeof relative==='string','invalid-snapshot-root');
    const actual=identity.walk(snapshot.root,relative,(name,leaf)=>
      (snapshot.inventoryMode === 'sdk-source' ? identity.excludeSource(name,leaf) : legacyExcludes.has(leaf)) ||
      name==='resources/source-navigation.log');
    requireObservation(actual.every(name=>names.has(name)),'current-snapshot-membership-changed');
  }
  return snapshot.sha256;
}
function completeResourceAnalysis(analysis,complete,records=[]) {
  requireObservation(complete?.inputsMatched===true&&complete.nativeScopeObserved===false&&
    ['MSVC143','LLVM22MSVC142'].includes(complete.nativeToolchainProfile),'missing-complete-native-input-selection');
  const renderer=require('./native-renderer-validation.cjs').analyzeNativeRenderer(
    records,analysis.subject,complete.loadedPayloadModule);
  const model=require('./native-renderer-model-validation.cjs').analyzeNativeModel(
    records,analysis.subject,complete.loadedPayloadModule);
  const modelMatched=model.softwareWorkloadGate==='matched';
  const modelRequested=records.some(row=>row.nativeSdk?.rendererModel||row.nativeRenderer?.model);
  const evaluated=modelMatched&&['passed','failed'].includes(analysis.resourceGate);
  return {...analysis,inputBindingScope:complete.inputBindingScope,inputsMatched:true,
    nativeToolchainProfile:complete.nativeToolchainProfile,
    ...(analysis.cacheObservation===true?{cacheModulePin:complete.loadedPayloadModule||null,
      namedCacheCalibration:cacheCalibration(records,analysis.subject,complete.loadedPayloadModule),
      privateStorageCalibration:privateStorageCalibration(records,analysis.privateStorageBinding)}:{}),
    resourceRuleObservation:analysis.resourceGate,observationFailure:analysis.failure||null,
    resourceGate:evaluated?analysis.resourceGate:'not-evaluated',passed:evaluated&&analysis.passed===true,
    nativeScopeGate:modelMatched?'matched-compatibility-workload':renderer.nativeSoftwareGate==='matched'?'matched-compositor-only':'not-observed',
    nativeSoftware:renderer,nativeSoftwareGate:renderer.nativeSoftwareGate,
    nativeModel:model,softwareProcessModelGate:modelMatched?'matched-bound-workload':'not-evaluated',releaseReady:false,
    failure:evaluated?(analysis.failure||null):analysis.failure||renderer.failure||
      (modelRequested?model.failure:null)||'broader-software-process-workload-not-observed'};
}
function analyzeFile(resultPath,bindingPath) {
  try {
    const resultRaw=fs.readFileSync(identity.safePath(resultPath)),bindingRaw=fs.readFileSync(identity.safePath(bindingPath));
    const result=JSON.parse(resultRaw.toString('utf8'));
    const binding=JSON.parse(bindingRaw.toString('utf8'));
    requireObservation(['soui.resource-file-bindings/v1','soui.resource-file-bindings/v2'].includes(binding.schema),'missing-file-bindings');
    const outputRoot=path.dirname(resultPath), owned=result.outcome?.ownedProcess;
    const complete=binding.schema==='soui.resource-file-bindings/v2'?
      require('./resource-file-bindings.cjs').verifyCompleteBinding(binding,outputRoot,result):null;
    const sourceSha256=complete?complete.sourceSha256:verifySnapshot(binding.source);
    const payloadSha256=complete?complete.payloadSha256:verifySnapshot(binding.payload);
    requireObservation(owned&&path.isAbsolute(owned.report),'missing-owned-report');
    const executable=identity.safePath(binding.payload.root,path.basename(binding.executablePath));
    requireObservation(path.resolve(executable).toLowerCase()===path.resolve(binding.executablePath).toLowerCase(),'executable-not-in-payload');
    const executableFile=binding.payload.files.find(file=>file.path===path.basename(executable));
    requireObservation(executableFile&&executableFile.normalization!=='lf','executable-not-pinned');
    const eventLogPath=path.join(outputRoot,'events.ndjson');
    const eventRaw=fs.readFileSync(identity.safePath(eventLogPath));
    const analysis=analyzeResource(result,eventRaw,{
      policyRaw:readPinned(binding.policy), requestRaw:fs.readFileSync(identity.safePath(path.join(outputRoot,'request.json'))),
      ownedRaw:fs.readFileSync(identity.safePath(owned.report)),contextRaw:fs.readFileSync(identity.safePath(owned.report+'.context.json')),
      currentInputsMatched:true,sourceSha256,payloadSha256,runnerSha256:hash(readPinned(binding.runner)),
      executableSha256:executableFile.sha256,executablePath:executable,payloadRoot:binding.payload.root,eventLogPath,
      modulePin:complete?.loadedPayloadModule,completeInputsMatched:!!complete,
      executablePin:complete?binding.additionalInputs.executable:null});
    if(complete) {
      const final=require('./resource-file-bindings.cjs').verifyCompleteBinding(binding,outputRoot,result);
      requireObservation(JSON.stringify(final)===JSON.stringify(complete)&&
        fs.readFileSync(identity.safePath(resultPath)).equals(resultRaw)&&
        fs.readFileSync(identity.safePath(bindingPath)).equals(bindingRaw)&&
        fs.readFileSync(identity.safePath(eventLogPath)).equals(eventRaw),'complete-resource-record-or-input-changed');
      return completeResourceAnalysis(analysis,final,parseRaw(eventRaw,true));
    }
    return {...analysis,inputBindingScope:'legacy-finite-input-roots',resourceRuleObservation:analysis.resourceGate,
      resourceGate:'not-evaluated',passed:false,nativeScopeGate:'not-bound-by-v1',
      failure:analysis.failure||'complete-input-and-native-software-scope-not-observed'};
  } catch(error) {
    return {schema:'soui.software-reload-resource-analysis/v1',policySha256:POLICY_SHA256,
      resourceGate:'not-evaluated',passed:false,releaseReady:false,m0Complete:false,
      sdkPrivateCacheQueueGate:'not-evaluated',thousandCycleOperationGate:'not-evaluated',
      eightHourGate:'not-evaluated',targetOsGate:'not-evaluated',
      failure:error instanceof ObservationError?error.message:'missing-or-invalid-file-observation'};
  }
}
module.exports={POLICY,POLICY_SHA256,policyBytes,CALIBRATION_POLICY,CALIBRATION_POLICY_SHA256,calibrationPolicyBytes,
  resourcePolicySelection,resourceNativeArgs,cacheCalibration,privateStorageCalibration,hash,medianRatio,theilSen,analyzeResource,analyzeFile,completeResourceAnalysis};
if(require.main===module) {
  try {
    if(process.argv.length!==5)throw new Error('Usage: resource-validation.cjs RESULT_JSON BINDINGS_JSON OUTPUT_JSON');
    const result=analyzeFile(path.resolve(process.argv[2]),path.resolve(process.argv[3]));
    fs.writeFileSync(path.resolve(process.argv[4]),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify({resourceGate:result.resourceGate,releaseReady:false}));
    if(result.resourceGate!=='passed')process.exitCode=1;
  } catch { console.error('Resource analysis could not be completed'); process.exitCode=1; }
}
