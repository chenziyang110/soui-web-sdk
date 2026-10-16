'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {StringDecoder} = require('node:string_decoder');
const {launchOwned} = require('../../tools/internal/owned-process.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const builds = require('../../tools/internal/build-evidence.cjs');
const {captureTimeout} = require('./timeout-diagnostics.cjs');
const {createSupervisor} = require('./monotonic-supervisor.cjs');
const {mixedSettings, validateMixedRecords} = require('./mixed-validation.cjs');
const {sessionSettings, validateSessionRecords} = require('./session-validation.cjs');
const root = path.resolve(__dirname, '../..');
const modes = ['reload', 'recreate', 'multiwindow', 'bridge'];

function settings(mode, cycles, warmup, settleMs, timeoutMs) {
  if (!modes.includes(mode)) throw new Error('Unknown acceptance mode');
  const values = {cycles, warmup, settleMs, timeoutMs};
  for (const [key, maximum] of Object.entries({cycles:10000,warmup:100,settleMs:60000,timeoutMs:120000})) {
    if (!Number.isSafeInteger(values[key]) || values[key] < 0 || values[key] > maximum) throw new Error(`Invalid ${key}`);
  }
  if (!cycles || !timeoutMs) throw new Error('Cycles and timeout must be positive');
  return {mode, ...values};
}
function expectedSteps(config) {
  const {mode, cycles, warmup} = settings(...['mode','cycles','warmup','settleMs','timeoutMs'].map(k=>config[k]));
  const total = cycles + warmup;
  const steps = [];
  const add = (action, window, cycle) => steps.push({action, window, cycle});
  if (mode === 'reload') {
    add('mount', 1, 0);
    add('runtime.reject-busy',0,0);
    for (let cycle=1;cycle<=total;cycle++) add('reload', 1, cycle);
    add('close', 1, total+1);
  } else for (let cycle=1;cycle<=total;cycle++) {
    add('mount', 1, cycle);
    if (cycle===1) add('runtime.reject-busy',0,cycle);
    if (mode === 'multiwindow') {
      add('mount', 2, cycle); add('mount', 3, cycle); add('close', 2, cycle);
      add('reload', 1, cycle); add('reload', 3, cycle); add('mount', 2, cycle);
      add('close', 3, cycle); add('close', 2, cycle);
    }
    add('close', 1, cycle);
  }
  add('runtime.shutdown', 0, total+1);
  add('runtime.reject-restart', 0, total+1);
  add('runtime.shutdown-idempotent', 0, total+1);
  return steps;
}
function verifyNativeIdentity(records, outcome) {
  const start = Array.isArray(records) ? records[0] : null;
  const owned = outcome?.ownedProcess;
  const fileTime = value => typeof value === 'string' && /^[1-9]\d{0,19}$/.test(value) &&
    BigInt(value) <= 18446744073709551615n;
  if (!start || start.event !== 'run.begin' || !Number.isSafeInteger(start.pid) ||
      start.pid <= 0 || start.pid > 0xffffffff || !fileTime(start.creationFileTime) ||
      !owned || !Number.isSafeInteger(owned.pid) || !fileTime(owned.creationFileTime) ||
      start.pid !== outcome.pid || start.pid !== owned.pid ||
      start.creationFileTime !== owned.creationFileTime)
    throw new Error('Native process identity must match the actual Owned PID and creation FILETIME');
  return {pid:owned.pid, creationFileTime:owned.creationFileTime};
}
function resourceBinding(config, options) {
  if (options === undefined) return null;
  if (!options || typeof options !== 'object' || Array.isArray(options) ||
      Object.keys(options).some(key=>!['resource','cacheObservation'].includes(key)) || typeof options.resource !== 'boolean' ||
      (Object.prototype.hasOwnProperty.call(options,'cacheObservation')&&
        (options.resource!==true||options.cacheObservation!==true)))
    throw new Error('Only an explicit trusted resource boolean is supported');
  if (!options.resource) return null;
  if (config.mode !== 'reload' || config.cycles !== 100 || config.warmup !== 10 ||
      config.settleMs !== 5000 || config.timeoutMs !== 20000)
    throw new Error('Resource entry requires the frozen 100+10 software reload protocol');
  // Root pins this actual module and its dependencies before loading the runner.
  // Neither a request file nor a caller-provided hash selects the policy.
  const reader = require('./resource-validation.cjs');
  const cache=options.cacheObservation===true;
  const policyRaw = cache?reader.calibrationPolicyBytes():reader.policyBytes();
  const policySha256 = identity.hash(policyRaw);
  if (!Buffer.isBuffer(policyRaw) || policySha256 !== (cache?reader.CALIBRATION_POLICY_SHA256:reader.POLICY_SHA256))
    throw new Error('Loaded resource policy bytes do not match the reader');
  return {policyRaw, requestFields:{resourcePolicySha256:policySha256,
    resourceScope:{kernel:'miniblink', renderer:'software', processModel:'single-process'},
    ...(cache?{namedCacheObservation:reader.CALIBRATION_POLICY.namedCacheObservation}:{})}};
}
function longRunBinding(config,options){
  if(options===undefined||!Object.prototype.hasOwnProperty.call(options,'longRun'))return null;
  if(!options||typeof options!=='object'||Array.isArray(options)||Object.keys(options).length!==1||options.longRun!==true)
    throw Error('Long capture requires only the explicit trusted longRun=true option');
  return require('./long-run-policy.cjs').policySelection(config);
}
function parseRunCommand(command, args) {
  if (!Array.isArray(args)) throw new Error('Invalid acceptance command arguments');
  if(command==='run-long'){
    if(args.length!==1||!modes.includes(args[0]))throw Error('Frozen thousand-cycle capture takes SUITE and one public mode');
    return {config:require('./long-run-policy.cjs').fixedConfig(args[0]),options:{longRun:true}};
  }
  if(command==='run-long-mixed'||command==='run-long-session'){
    if(args.length)throw Error('Frozen eight-hour capture takes only SUITE');
    return {config:require('./long-run-policy.cjs').fixedConfig(command==='run-long-session'?'session':'mixed'),options:{longRun:true}};
  }
  if (command === 'run-resource') {
    if (args.length&&!(args.length===1&&args[0]==='--cache-observation=1'))
      throw new Error('Resource capture takes SUITE and only the explicit cache opt-in; its protocol cannot be overridden');
    return {config:settings('reload',100,10,5000,20000), options:{resource:true,...(args.length?{cacheObservation:true}:{})}};
  }
  if (command === 'run' && args.length === 5)
    return {config:settings(args[0],...args.slice(1).map(Number))};
  if (command === 'run-mixed' && args.length === 4)
    return {config:mixedSettings(...args.map(Number))};
  if (command === 'run-session' && args.length === 4)
    return {config:sessionSettings(...args.map(Number))};
  throw new Error('Invalid acceptance command');
}
function filePin(file) {
  const current=identity.safePath(file);
  if (!fs.statSync(current).isFile()) throw new Error('Resource capture evidence must be a regular file');
  const bytes=fs.readFileSync(current);
  return {path:current,bytes:bytes.length,sha256:identity.hash(bytes)};
}
function resourceCaptureResult(operation, operationPin, policyPin) {
  if (operation?.schema !== 'soui.acceptance-run/v1' || typeof operation.passed !== 'boolean')
    throw new Error('A retained lifecycle operation result is required');
  return {schema:'soui.acceptance-resource-capture/v1',runId:operation.runId,config:operation.config,
    operationPassed:operation.passed,operationFailure:operation.failure || null,
    resourceGate:'not-evaluated',passed:false,releaseReady:false,m0Complete:false,
    operationResult:operationPin,policy:policyPin,bindings:null,analysis:null,
    failure:'Trusted complete input bindings and actual software scope are not yet independently verified',
    requiredEvidence:['complete-current-source-and-consumer-input-bindings','actual-native-software-scope',
      'independent-resource-file-analysis'],
    scope:'Frozen resource sampling only; lifecycle completion is not a resource gate'};
}
function matchResourceBindingPin(expected, current, output) {
  const fixed=path.join(output,'file-bindings.json');
  for(const pin of [expected,current]) {
    if(!pin || path.resolve(pin.path)!==path.resolve(fixed) || !Number.isSafeInteger(pin.bytes) ||
        pin.bytes<0 || pin.bytes>16*1024*1024 || typeof pin.sha256!=='string' || !/^[a-f0-9]{64}$/.test(pin.sha256))
      throw new Error('Frozen resource binding must have its exact run file pin');
  }
  if(expected.bytes!==current.bytes || expected.sha256!==current.sha256)
    throw new Error('Frozen resource binding bytes changed');
  return Object.freeze({path:fixed,bytes:expected.bytes,sha256:expected.sha256});
}
function resourceBindingFilePin(output) {
  const file=identity.safePath(output,'file-bindings.json'),stat=fs.statSync(file);
  if(!stat.isFile() || stat.size>16*1024*1024) throw new Error('Frozen resource binding is not a bounded regular file');
  return filePin(file);
}
function completeResourceCaptureResult(operation, operationPin, policyPin, evidence) {
  const capture=resourceCaptureResult(operation,operationPin,policyPin);
  if(!evidence || typeof evidence!=='object' || Array.isArray(evidence) ||
      Object.keys(evidence).some(key=>!['bindingFile','inputs','analysis','failure','finalVerified'].includes(key)))
    throw new Error('Fixed complete binding observations are required');
  const inputs=evidence.inputs,analysis=evidence.analysis?.record;
  verifyPrivateStorageAggregate(analysis,operation,inputs);
  const finalMatched=operation.passed===true && !operation.failure && evidence.finalVerified===true && evidence.failure===null && !!inputs;
  const workloadAnalysis=analysis?.nativeScopeGate==='matched-compatibility-workload';
  if(inputs && (inputs.inputsMatched!==true || inputs.inputBindingScope!=='complete-acceptance-inputs' ||
      inputs.closureObserved!==false || inputs.resourceGate!=='not-evaluated' || inputs.passed!==false ||
      inputs.nativeScopeGate!=='not-observed' || inputs.releaseReady!==false))
    throw new Error('An input-only recheck cannot assert resource or native qualification');
  if(analysis && !workloadAnalysis && (analysis.resourceGate!=='not-evaluated' || analysis.passed!==false ||
      analysis.releaseReady!==false || ![undefined,'not-observed','matched-compositor-only'].includes(analysis.nativeScopeGate) ||
      ![undefined,'matched','failed','not-evaluated'].includes(analysis.nativeSoftwareGate) ||
      (analysis.nativeSoftwareGate==='matched' && analysis.nativeScopeGate!=='matched-compositor-only') ||
      (analysis.nativeScopeGate==='matched-compositor-only' && (analysis.nativeSoftwareGate!=='matched' ||
        analysis.nativeSoftware?.schema!=='soui.bound-dll-compositor-observation/v1' ||
        analysis.nativeSoftware.scope!=='bound-dll-completed-compositor-calls' ||
        analysis.nativeSoftware.nativeSoftwareGate!=='matched' || analysis.nativeSoftware.qualified!==false ||
        analysis.softwareProcessModelGate!=='not-evaluated'))))
    throw new Error('Complete file analysis cannot assert wider native software qualification');
  if(workloadAnalysis && (!['passed','failed','not-evaluated'].includes(analysis.resourceGate) ||
      analysis.resourceRuleObservation!==analysis.resourceGate ||
      analysis.passed!==(analysis.resourceGate==='passed') || analysis.releaseReady!==false || analysis.m0Complete!==false ||
      (analysis.resourceGate==='passed'&&analysis.failure!==null) ||
      analysis.softwareProcessModelGate!=='matched-bound-workload' || analysis.nativeSoftwareGate!=='matched' ||
      analysis.nativeSoftware?.schema!=='soui.bound-dll-compositor-observation/v1' ||
      analysis.nativeSoftware.scope!=='bound-dll-completed-compositor-calls' || analysis.nativeSoftware.qualified!==false ||
      analysis.nativeSoftware.nativeSoftwareGate!=='matched' ||
      analysis.nativeModel?.schema!=='soui.bound-compatibility-backend-observation/v2' ||
      analysis.nativeModel.scope!=='bound-compatibility-backend-workload' || analysis.nativeModel.qualified!==false ||
      analysis.nativeModel.softwareWorkloadGate!=='matched' || analysis.nativeModel.failure!==null ||
      ['sdkPrivateCacheQueueGate','thousandCycleOperationGate','eightHourGate','targetOsGate']
        .some(key=>analysis[key]!=='not-evaluated')))
    throw new Error('Only the finite frozen Compatibility workload resource gate can be evaluated');
  const compositorMatched=finalMatched && analysis?.inputsMatched===true &&
    analysis.inputBindingScope==='complete-acceptance-inputs' && analysis.nativeScopeGate==='matched-compositor-only';
  const workloadMatched=finalMatched && analysis?.inputsMatched===true &&
    analysis.inputBindingScope==='complete-acceptance-inputs' && workloadAnalysis;
  const resourceEvaluated=workloadMatched && ['passed','failed'].includes(analysis.resourceGate);
  const cacheEnabled=policyPin?.sha256===require('./resource-validation.cjs').CALIBRATION_POLICY_SHA256;
  let cacheGate='not-evaluated';
  if(analysis?.cacheObservation===true&&!cacheEnabled)
    throw new Error('Cache observation requires its exact explicit frozen policy');
  if(cacheEnabled&&analysis?.namedCacheCalibration){
    const reader=require('./resource-validation.cjs');
    const expected=reader.cacheCalibration(analysis.allSamples||[],analysis.subject,analysis.cacheModulePin);
    if(analysis.cacheObservation!==true||analysis.policySha256!==policyPin.sha256||
      JSON.stringify(analysis.namedCacheCalibration)!==JSON.stringify(expected)||
      (workloadMatched&&JSON.stringify(analysis.cacheModulePin)!==JSON.stringify(analysis.nativeModel.modulePin)))
      throw new Error('Cache calibration must retain the bound samples and exact finite aggregate');
    if(finalMatched&&analysis.inputsMatched===true)cacheGate=expected.finiteGate;
  }
  const captureGate=cacheEnabled?require('./long-run-validation.cjs').combine([
    resourceEvaluated?analysis.resourceGate:'not-evaluated',cacheGate]):resourceEvaluated?analysis.resourceGate:'not-evaluated';
  return {...capture,bindings:evidence.bindingFile,inputBindingGate:finalMatched?'matched':'not-evaluated',
    inputRecheck:finalMatched?inputs:null,inputRecheckDiagnostic:inputs || null,analysis:evidence.analysis?.file || null,
    resourceRuleObservation:analysis?.resourceRuleObservation || 'not-evaluated',
    observationFailure:analysis?.observationFailure || null,
    nativeScopeGate:workloadMatched?'matched-compatibility-workload':compositorMatched?'matched-compositor-only':'not-observed',
    nativeSoftwareGate:(workloadMatched||compositorMatched)?'matched':finalMatched?(analysis?.nativeSoftwareGate || 'not-evaluated'):'not-evaluated',
    nativeSoftware:finalMatched?(analysis?.nativeSoftware || null):null,
    nativeModel:finalMatched?(analysis?.nativeModel || null):null,
    softwareProcessModelGate:workloadMatched?'matched-bound-workload':'not-evaluated',
    resourceGate:resourceEvaluated?analysis.resourceGate:'not-evaluated',passed:captureGate==='passed',
    ...(cacheEnabled?{cacheObservationGate:cacheGate,captureGate,namedCacheCalibration:finalMatched?analysis?.namedCacheCalibration||null:null,
      privateStorageCalibration:finalMatched?analysis?.privateStorageCalibration||null:null,
      cacheBudgetGate:'not-evaluated',calibratedByteBudgets:null,cacheRequiredEvidence:[
        ...(cacheGate==='passed'?[]:['fresh-complete-named-cache-calibration']),
        'current-calibrated-byte-budgets-and-broader-cache-coverage']}:{}),
    failure:operation.failure || evidence.failure || analysis?.failure ||
      (cacheEnabled&&cacheGate!=='passed'?'named-cache-calibration:'+cacheGate:null) || (resourceEvaluated?null:'native-software-scope-not-observed'),
    requiredEvidence:finalMatched?(analysis?.inputsMatched===true&&analysis?.inputBindingScope==='complete-acceptance-inputs'?
      workloadMatched?resourceEvaluated?[]:['complete-original-resource-observations']:
        compositorMatched?['broader-software-process-workload-coverage']:['actual-native-software-scope']:
      ['actual-native-software-scope','independent-resource-file-analysis']):capture.requiredEvidence,
    scope:'Only frozen100reload Compatibility workload; global SDK queues,1000cycles,8hour,OS,ABI and release remain pending'};
}
// These pins come from the already rechecked fixed binding and Owned context,
// separately from node.dll's named-cache identity.
function privateStorageInputs(inputs,bindingPin,operation){
  const read=(file,limit)=>{const current=identity.safePath(file),stat=fs.statSync(current);
    if(!stat.isFile()||stat.size<=0||stat.size>limit)throw new Error('Private storage requires bounded original files');
    return fs.readFileSync(current);};
  const binding=JSON.parse(read(bindingPin.path,16*1024*1024).toString('utf8'));
  const report=operation.outcome.ownedProcess.report,eventRaw=read(path.join(path.dirname(report),'events.ndjson'),256*1024*1024);
  if(identity.hash(eventRaw)!==operation.logSha256)throw new Error('Private storage original event log changed');
  const events=eventRaw.toString('utf8').trimEnd().split('\n').map(line=>JSON.parse(line));
  return {...inputs,privateStorageProof:{executablePin:binding.additionalInputs.executable,
    contextSha256:identity.hash(read(report+'.context.json',16*1024*1024)),
    allSamplesSha256:identity.hash(Buffer.from(JSON.stringify(events)))}};
}
function verifyPrivateStorageAggregate(analysis,operation,inputs){
  const calibration=analysis?.privateStorageCalibration,observed=analysis?.privateStorage;
  if(!calibration&&!observed){
    if(inputs?.privateStorageProof&&(analysis?.cacheObservation===true||
      ['mixed','session'].includes(operation.config?.mode)&&analysis?.allSamples))
      throw new Error('Private storage analysis is missing its bound partial aggregate');
    return;
  }
  const owners=require('./long-run-owners.cjs'),binding=analysis.privateStorageBinding;
  if(calibration&&observed||!Array.isArray(analysis.allSamples)||
      identity.hash(Buffer.from(JSON.stringify(analysis.allSamples)))!==inputs?.privateStorageProof?.allSamplesSha256)
    throw new Error('Private storage must retain the original bound event sequence');
  const expectedBinding=owners.privateBinding(operation,
    {pid:operation.outcome?.pid,creationFileTime:operation.outcome?.ownedProcess?.creationFileTime},
    inputs?.privateStorageProof?.executablePin,
    {ownedLogSha256:operation.outcome?.ownedProcess?.reportSha256,contextSha256:inputs?.privateStorageProof?.contextSha256},
    inputs?.inputsMatched===true&&inputs.inputBindingScope==='complete-acceptance-inputs'&&
      inputs.sourceSha256===operation.sourceSha256&&inputs.payloadSha256===operation.payloadSha256&&
      inputs.consumerBuildId===operation.consumerBuildId);
  if(JSON.stringify(binding)!==JSON.stringify(expectedBinding))
    throw new Error('Private storage Source/payload/Owned/SDK EXE binding differs');
  const expected=calibration?require('./resource-validation.cjs').privateStorageCalibration(analysis.allSamples||[],expectedBinding):
    owners.privateStorageObservations(analysis.allSamples||[],expectedBinding);
  if(JSON.stringify(calibration||observed)!==JSON.stringify(expected))
    throw new Error('Private storage must retain the same events and exact partial aggregate');
}
function validateRecords(records, config, outcome) {
  if (outcome.timedOut !== false || outcome.exitCode !== 0 || outcome.signal || !Number.isInteger(outcome.pid) || outcome.pid <= 0)
    throw new Error('Process failed, timed out, or has no valid identity');
  if (!Array.isArray(records) || records.length < 4 || records[0].event !== 'run.begin' || records.at(-1).event !== 'run.complete')
    throw new Error('Missing run boundaries');
  verifyNativeIdentity(records, outcome);
  const start = records[0];
  if (start.pid !== outcome.pid || Object.keys(config).some(k=>start[k]!==config[k])) throw new Error('Scenario parameters or process identity differ');
  const steps = expectedSteps(config);
  const expectedEvents = steps.flatMap(step=>[{event:'step.begin',...step},{event:'step.complete',...step}]);
  const actualEvents=[];
  const samples=[];
  const samplePositions=[];
  let elapsed=-1;
  for (let i=0;i<records.length;i++) {
    const record=records[i];
    if (!Number.isSafeInteger(record.elapsedMs) || record.elapsedMs < elapsed) throw new Error('Invalid event timestamp');
    elapsed=record.elapsedMs;
    if (record.event==='run.begin' && i===0 || record.event==='run.complete' && i===records.length-1) continue;
    if (record.event==='sample') {
      for (const k of ['privateBytes','workingSetBytes','handles','threads','gdiObjects','userObjects'])
        if (!Number.isSafeInteger(record[k]) || record[k] < 0) throw new Error(`Missing resource measurement: ${k}`);
      samples.push(record);
      samplePositions.push([record.phase,record.cycle,actualEvents.length/2]);
      continue;
    }
    if (!['step.begin','step.complete'].includes(record.event)) throw new Error(`Failed or unknown event: ${record.event}`);
    actualEvents.push(Object.fromEntries(['event','action','window','cycle'].map(k=>[k,record[k]])));
  }
  if (JSON.stringify(actualEvents)!==JSON.stringify(expectedEvents)) throw new Error('Missing, duplicated, or reordered lifecycle steps');
  const total=config.cycles+config.warmup;
  const completedAtCycle=cycle=>config.mode==='reload'?2+cycle:(config.mode==='multiwindow'?10:2)*cycle+1;
  const expectedSamples=[['process',0,0],...(config.mode==='reload'?[['mounted',0,2]]:[]),
    ...Array.from({length:total},(_,i)=>['cycle',i+1,completedAtCycle(i+1)]),
    ['closed',total+1,steps.length-3],['runtime-closed',total+1,steps.length-2]];
  if (JSON.stringify(samplePositions)!==JSON.stringify(expectedSamples)) throw new Error('Incomplete or misplaced resource sampling');
  // Preserve every sample. This result proves operation completion, not a memory/soak gate.
  return {completedSteps:steps.length, measuredCycles:config.cycles, samples, resourceGate:'not-evaluated', releaseReady:false};
}
function directory(suite) {
  const result=identity.safePath(suite);
  const parent=path.join(root,'out','acceptance')+path.sep;
  if (!result.startsWith(parent) || path.dirname(result)!==parent.slice(0,-1)) throw new Error('Suite must be a direct child of out/acceptance');
  return result;
}
function write(file,value) { fs.writeFileSync(file,identity.encoded(value),{flag:'wx'}); }
function payload(bin) {
  return identity.snapshot(bin,identity.walk(bin,'',name=>name==='resources/source-navigation.log'));
}
function prepare(suite,architecture,configuration,runtimeBinary,fixture='ordinary',nativeToolchain) {
  if(!['ordinary','message-port'].includes(fixture)) throw new Error('Unknown trusted acceptance fixture');
  if(nativeToolchain && fixture!=='ordinary') throw new Error('Installed LLVM acceptance supports only the ordinary fixture');
  suite=directory(suite);
  const build=nativeToolchain===undefined?builds.verifyBuild(root,architecture,configuration,true):
    require('../../tools/internal/llvm-installed-acceptance.cjs').current(root,architecture,configuration,nativeToolchain).build;
  const webRecord=identity.readJson(path.join(root,'out','web-sdk.json'));
  if(webRecord.schema!=='soui.web-build/v1' || webRecord.sourceSha256!==build.source.sha256 ||
     identity.snapshot(root,identity.walk(root,'sdk/web/dist')).sha256!==webRecord.artifacts.sha256)
    throw new Error('The public Web SDK must be built from the same source');
  const prebuilt=path.join(root,'third_party','prebuilt',architecture,'miniblink','node.dll');
  const input=identity.safePath(runtimeBinary||prebuilt);
  if(!fs.statSync(input).isFile()) throw new Error('Runtime candidate must be a regular DLL file');
  const bytes=fs.readFileSync(input);
  const kernel={kind:input===prebuilt?'prebuilt':'explicit-candidate',bytes:bytes.length,sha256:identity.hash(bytes)};
  fs.mkdirSync(path.join(suite,'kernel-input'));
  fs.writeFileSync(path.join(suite,'kernel-input','node.dll'),bytes,{flag:'wx'});
  const original=path.join(root,'tests',fixture==='message-port'?'message-port-app':'acceptance-app');
  const consumerSource=identity.snapshot(original,identity.walk(original));
  const copy=path.join(suite,'consumer-source');
  fs.cpSync(original,copy,{recursive:true,errorOnExist:true,force:false,filter:()=>true});
  if(identity.snapshot(copy,identity.walk(copy)).sha256!==consumerSource.sha256) throw new Error('Consumer source changed during snapshot');
  const webSdkRoot=path.join(suite,'web-sdk');
  fs.cpSync(path.join(root,'sdk/web/dist'),path.join(webSdkRoot,'dist'),{recursive:true,errorOnExist:true,force:false,filter:()=>true});
  fs.copyFileSync(path.join(root,'sdk/web/package.json'),path.join(webSdkRoot,'package.json'));
  const webSdk=identity.snapshot(webSdkRoot,identity.walk(webSdkRoot));
  for(const file of webRecord.artifacts.files) {
    if(identity.hash(fs.readFileSync(path.join(webSdkRoot,file.path.slice('sdk/web/'.length))))!==file.sha256)
      throw new Error('Public Web SDK changed during snapshot');
  }
  write(path.join(suite,'pending.json'),{architecture,configuration,sourceSha256:build.source.sha256,buildId:build.buildId,version:build.version,consumerSource,webSdk,kernel,
    ...(nativeToolchain===undefined?{}:{nativeToolchain})});
  return {buildId:build.buildId,sourceSha256:build.source.sha256};
}
function stage(suite) {
  suite=directory(suite);
  const pending=identity.readJson(path.join(suite,'pending.json'));
  const {architecture,configuration}=pending;
  const native=pending.nativeToolchain===undefined?null:
    require('../../tools/internal/llvm-installed-acceptance.cjs').verify(root,suite,pending.nativeToolchain);
  const build=native?native.build:builds.verifyBuild(root,architecture,configuration,true);
  if (build.buildId!==pending.buildId || build.source.sha256!==pending.sourceSha256) throw new Error('SDK/source changed during consumer build');
  const consumerSource=path.join(suite,'consumer-source');
  if(identity.snapshot(consumerSource,identity.walk(consumerSource)).sha256!==pending.consumerSource.sha256)
    throw new Error('Independent consumer source changed during build');
  const web=verifyWeb(suite,pending);
  const kernel=fs.readFileSync(path.join(suite,'kernel-input','node.dll'));
  if(identity.hash(kernel)!==pending.kernel.sha256) throw new Error('Runtime candidate changed during build');
  const install=path.join(suite,'install');
  const library=fs.readFileSync(path.join(install,'lib',configuration,'soui_sdk.lib'));
  if (identity.hash(library)!==build.library.files[0].sha256) throw new Error('Installed library is not the verified SDK');
  const sourceHeaders=identity.walk(root,'sdk/include');
  for (const name of sourceHeaders) {
    if (identity.hash(fs.readFileSync(path.join(root,name)))!==identity.hash(fs.readFileSync(path.join(install,name.slice(4)))))
      throw new Error('Installed SDK header differs: '+name);
  }
  const bin=path.join(suite,'runtime');
  fs.mkdirSync(bin);
  fs.copyFileSync(path.join(suite,'consumer','bin',configuration,'SouiAcceptance.exe'),path.join(bin,'SouiAcceptance.exe'));
  fs.writeFileSync(path.join(bin,'node.dll'),kernel,{flag:'wx'});
  fs.cpSync(path.join(suite,'consumer','resources'),path.join(bin,'resources'),{recursive:true,errorOnExist:true,force:false,filter:()=>true});
  for(let window=1;window<=3;window++) {
    const resources=path.join(bin,'resources',`window-${window}`);
    if(identity.snapshot(resources,identity.walk(resources)).sha256!==web.output.sha256)
      throw new Error('Compiled consumer did not deploy the verified Web output');
  }
  const dependency=`third_party/prebuilt/${architecture}`;
  const dlls=[...identity.walk(root,`${dependency}/runtime`).filter(f=>/\.dll$/i.test(f)),
    ...identity.walk(root,`${dependency}/soui/bin`).filter(f=>/\.dll$/i.test(f)&&!/(souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\.dll$/i.test(f))];
  for (const file of dlls) {
    const destination=path.join(bin,path.basename(file));
    if (fs.existsSync(destination)) throw new Error('Runtime basename collision: '+file);
    fs.copyFileSync(path.join(root,file),destination);
  }
  const evidence={schema:'soui.acceptance-build/v3',...pending,web,consumerBuildId:crypto.randomUUID(),
    ...(native?{nativeToolchain:{...pending.nativeToolchain,consumer:native.consumer}}:{}),
    installedSdk:identity.snapshot(install,identity.walk(install)),payload:payload(bin),
    environment:{platform:os.platform(),release:os.release(),architecture:os.arch(),node:process.version,cpu:os.cpus()[0]?.model,logicalProcessors:os.cpus().length},
    completedAt:new Date().toISOString(),releaseReady:false};
  write(path.join(suite,'build.json'),evidence);
  return {consumerBuildId:evidence.consumerBuildId,payloadSha256:evidence.payload.sha256};
}
function verifyWeb(suite,build) {
  const web=identity.readJson(path.join(suite,'web-build.json'));
  const sdkRoot=path.join(suite,'web-sdk'),output=path.join(suite,'web-output');
  const typecheck=web.typecheck;
  const typecheckConfig=path.join(suite,'web-typecheck.tsconfig.json');
  const typecheckLog=path.join(suite,'web-typecheck.log');
  const expectedTypeScript=identity.readJson(path.join(root,'tests','acceptance','package.json')).devDependencies.typescript;
  if(web.schema!=='soui.acceptance-web/v1' || web.consumerSourceSha256!==build.consumerSource.sha256 ||
     web.webSdkSha256!==build.webSdk.sha256 || identity.snapshot(sdkRoot,identity.walk(sdkRoot)).sha256!==build.webSdk.sha256 ||
     !typecheck || typecheck.passed!==true || typecheck.typescript!==expectedTypeScript ||
     identity.hash(fs.readFileSync(typecheckConfig))!==typecheck.configSha256 ||
     identity.hash(fs.readFileSync(typecheckLog))!==typecheck.logSha256 ||
     identity.snapshot(output,identity.walk(output)).sha256!==web.output.sha256)
    throw new Error('Independent public Web SDK inputs/output drift');
  return web;
}
function verifyInputs(suite) {
  const build=identity.readJson(path.join(suite,'build.json'));
  if(build.schema!=='soui.acceptance-build/v3') throw new Error('A fresh acceptance build with the public Web SDK consumer is required');
  if(JSON.stringify(verifyWeb(suite,build))!==JSON.stringify(build.web))throw new Error('Independent Web build record drift');
  const installed=build.nativeToolchain===undefined?null:
    require('../../tools/internal/llvm-installed-acceptance.cjs').verifyWithInputs(root,suite,build.nativeToolchain);
  const sdk=installed?installed.sdk.build:builds.verifyBuild(root,build.architecture,build.configuration,true);
  if (sdk.buildId!==build.buildId || sdk.source.sha256!==build.sourceSha256 || payload(path.join(suite,'runtime')).sha256!==build.payload.sha256)
    throw new Error('Acceptance source, SDK build, or runtime drift');
  const install=path.join(suite,'install');
  if (identity.snapshot(install,identity.walk(install)).sha256!==build.installedSdk.sha256) throw new Error('Installed SDK drift');
  const consumerSource=path.join(suite,'consumer-source');
  if(identity.snapshot(consumerSource,identity.walk(consumerSource)).sha256!==build.consumerSource.sha256)
    throw new Error('Independent consumer source drift');
  if(identity.hash(fs.readFileSync(path.join(suite,'kernel-input','node.dll')))!==build.kernel.sha256)
    throw new Error('Runtime candidate input drift');
  return {build,sdk,installed};
}
function verify(suite) { return verifyInputs(suite).build; }
function finishLongCapture(output,operation,summary,frozenBinding,files){
  const evidence={bindingFile:frozenBinding,inputs:null,analysis:null,failure:null,finalVerified:false};
  if(operation.passed){
    try{
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      evidence.inputs=files.recheck(frozenBinding.path);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      const analysis=require('./long-run-validation.cjs').analyzeFile(path.join(output,'operation-result.json'),frozenBinding.path);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      evidence.inputs=files.recheck(frozenBinding.path);
      if(['mixed','session'].includes(operation.config.mode))evidence.inputs=privateStorageInputs(evidence.inputs,frozenBinding,operation);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      verifyPrivateStorageAggregate(analysis,operation,evidence.inputs);
      const analysisPath=path.join(output,'long-run-analysis.json');write(analysisPath,analysis);
      evidence.analysis={record:analysis,file:filePin(analysisPath)};evidence.finalVerified=true;
    }catch(error){evidence.failure=error.message;evidence.finalVerified=false;}
  }
  const reader=require('./long-run-validation.cjs');
  const capture=reader.captureResult(operation,filePin(path.join(output,'operation-result.json')),
    filePin(path.join(output,'long-run-policy.json')),evidence);
  const capturePath=path.join(output,'result.json');write(capturePath,capture);
  return {...summary,passed:capture.passed,operationPassed:capture.operationPassed,longRunGate:capture.longRunGate,
    resourceGate:capture.resourceGate,eightHourGate:capture.eightHourGate,thousandCycleOperationGate:capture.thousandCycleOperationGate,
    result:path.relative(root,capturePath).replaceAll('\\','/'),operationResult:summary.result,failure:capture.failure};
}
function measurementRunner(build) {
  const runner=build.nativeToolchain===undefined?
    (process.env.SOUI_OWNED_PROCESS_RUNNER || path.join(root,'out/benchmark-protocol/Release/OwnedProcessRunner.exe')):
    require('../../tools/internal/llvm-installed-acceptance.cjs').runtimeRunner(root,build.architecture,build.configuration,build.nativeToolchain);
  return {runner,binding:{runner:filePin(runner)}};
}
async function run(suite,config,options) {
  const longRun=longRunBinding(config,options);
  const resource = longRun?null:resourceBinding(config,options);
  const capture=longRun||resource;
  suite=directory(suite);
  const build=verify(suite);
  const runId=crypto.randomUUID();
  const output=path.join(suite,`${config.mode}-${runId}`);
  fs.mkdirSync(output);
  if(capture)fs.writeFileSync(path.join(output,longRun?'long-run-policy.json':'resource-policy.json'),capture.policyRaw,{flag:'wx'});
  write(path.join(output,'request.json'),{schema:'soui.acceptance-request/v1',runId,...config,
    ...(capture ? capture.requestFields : {}),consumerBuildId:build.consumerBuildId,
    payloadSha256:build.payload.sha256,sourceSha256:build.sourceSha256,startedAt:new Date().toISOString()});
  const resourceFiles=capture?require('./resource-file-bindings.cjs'):null;
  const frozen=resourceFiles?resourceFiles.freeze(suite,output):null;
  const frozenBinding=capture?matchResourceBindingPin(frozen.bindingFile,resourceBindingFilePin(output),output):null;
  const log=path.join(output,'events.ndjson');
  const stdout=fs.openSync(path.join(output,'stdout.log'),'wx');
  const stderr=fs.openSync(path.join(output,'stderr.log'),'wx');
  const bin=path.join(suite,'runtime');
  const mixed=config.mode==='mixed', session=config.mode==='session';
  const duration=mixed || session;
  const args=longRun?require('./long-run-validation.cjs').nativeArgs(config,log):resource?
    require('./resource-validation.cjs').resourceNativeArgs({...config,...resource.requestFields},log):duration?
    [`--mode=${config.mode}`,`--duration-ms=${config.durationMs}`,`--idle-ms=${config.idleMs}`,
      `--settle-ms=${config.settleMs}`,`--timeout-ms=${config.timeoutMs}`,`--report=${log}`]:
    [`--mode=${config.mode}`,`--cycles=${config.cycles}`,`--warmup=${config.warmup}`,
      `--settle-ms=${config.settleMs}`,`--timeout-ms=${config.timeoutMs}`,`--report=${log}`];
  const processTimeoutMs=duration?config.durationMs+Math.max(180000,2*config.timeoutMs+config.idleMs):
    Math.min(12*60*60*1000,Math.max(60000,expectedSteps(config).length*(config.timeoutMs+config.settleMs)+30000));
  let timedOut=false;
  let timeoutReason=null;
  let child;
  let outcome;
  let preparedRunner;
  let timeoutDiagnosticPromise=Promise.resolve(null);
  let timer;
  let monitor;
  try {
      preparedRunner=measurementRunner(build);
      child=launchOwned(path.join(bin,'SouiAcceptance.exe'),args,{
        runner:preparedRunner.runner,
        cwd:bin,report:path.join(output,'owned-process.ndjson'),
        timeoutMs:processTimeoutMs+180000,stdout,stderr});
      await child.ready;
      let observedBytes=0;
      let partial='';
      const decoder=new StringDecoder('utf8');
      const supervisor=createSupervisor(config);
      const expire=reason=>{
        if(timedOut)return;
        timedOut=true;
        timeoutReason=reason;
        timeoutDiagnosticPromise=captureTimeout({pid:child.pid,output,nativeLog:log,
          snapshot:{schema:'soui.acceptance-timeout-observation/v1',runId,reason,pid:child.pid,
            observedBytes,partialBytes:Buffer.byteLength(partial,'utf8'),...supervisor.snapshot()},
          debuggerExe:process.env.SOUI_ACCEPTANCE_DEBUGGER,
          dumpRoot:process.env.SOUI_ACCEPTANCE_DUMP_ROOT})
          .catch(error=>({debugger:{status:'failed',error:error.message}}))
          .finally(()=>child.stop());
      };
      timer=setTimeout(()=>expire('process-deadline'),processTimeoutMs);
      // Per-step supervision also covers a blocked native mount/destroy where
      // the in-process readiness timer cannot run. Read only appended bytes.
      monitor=setInterval(()=>{
        try {
          if(fs.existsSync(log)) {
            const size=fs.statSync(log).size;
            if(size<observedBytes) return expire('native-log-truncated');
            if(size>observedBytes) {
              const file=fs.openSync(log,'r');
              const buffer=Buffer.alloc(size-observedBytes);
              let bytesRead;
              try{bytesRead=fs.readSync(file,buffer,0,buffer.length,observedBytes);}finally{fs.closeSync(file);}
              observedBytes+=bytesRead;
              const lines=(partial+decoder.write(buffer.subarray(0,bytesRead))).split('\n'); partial=lines.pop();
              for(const line of lines){
                supervisor.observe(JSON.parse(line));
              }
            }
          }
          const reason=supervisor.deadlineReason();
          if(reason)expire(reason);
        } catch(error){expire('native-observation-failed:'+error.message);}
      },250);
      const owned=await child.finished;
      outcome={pid:owned.pid,exitCode:owned.exitCode,signal:owned.signal,
        timedOut:timedOut || owned.terminal?.reason==='timeout',timeoutReason,
        spawnError:owned.spawnError,processTimeoutMs,ownedProcess:owned};
  } catch(error) {
      child?.stop();
      const owned=child?await child.finished:null;
      outcome={pid:owned?.pid??null,exitCode:owned?.exitCode??null,signal:owned?.signal??null,
        timedOut,timeoutReason,spawnError:error.message,processTimeoutMs,ownedProcess:owned};
  } finally { clearTimeout(timer);clearInterval(monitor);fs.closeSync(stdout);fs.closeSync(stderr); }
  outcome.timeoutDiagnostic=await timeoutDiagnosticPromise;
  const result={schema:'soui.acceptance-run/v1',runId,config,consumerBuildId:build.consumerBuildId,
    sourceSha256:build.sourceSha256,payloadSha256:build.payload.sha256,outcome,passed:false,releaseReady:false,
    finishedAt:new Date().toISOString()};
  try {
    if(outcome.spawnError || outcome.timedOut || outcome.ownedProcess?.passed!==true)
      throw new Error('Owned process exit, supervision or complete Job cleanup was not verified');
    const currentRunner=measurementRunner(build).binding.runner;
    if(outcome.ownedProcess.runnerSha256!==preparedRunner.binding.runner.sha256 ||
        currentRunner.path!==preparedRunner.binding.runner.path ||
        currentRunner.bytes!==preparedRunner.binding.runner.bytes ||
        currentRunner.sha256!==preparedRunner.binding.runner.sha256)
      throw new Error('Actual Owned Runner differs from the selected current runner');
    verify(suite);
    const bytes=fs.readFileSync(log);
    result.logSha256=identity.hash(bytes);
    if (!bytes.toString('utf8').endsWith('\n')) throw new Error('Truncated native log');
    const records=bytes.toString('utf8').trimEnd().split('\n').map(line=>JSON.parse(line));
    verifyNativeIdentity(records,outcome);
    result.validation=session?validateSessionRecords(records,config,outcome):mixed?validateMixedRecords(records,config,outcome):validateRecords(records,config,outcome);
    result.passed=true;
  } catch(error) { result.failure=error.message; }
  // The original operation schema remains the strict reader's lifecycle input.
  // Resource capture has its own overall result and cannot report a green gate.
  const operationPath=path.join(output,capture?'operation-result.json':'result.json');
  write(operationPath,result);
  const summary={passed:result.passed,mode:config.mode,runId,
    result:path.relative(root,operationPath).replaceAll('\\','/'),failure:result.failure,outcome};
  if(longRun)return finishLongCapture(output,result,summary,frozenBinding,resourceFiles);
  if (!resource) return summary;
  const bindingEvidence={bindingFile:frozenBinding,inputs:null,analysis:null,failure:null,finalVerified:false};
  if(result.passed) {
    try {
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      bindingEvidence.inputs=resourceFiles.recheck(frozenBinding.path);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      const analysis=require('./resource-validation.cjs').analyzeFile(operationPath,frozenBinding.path);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      bindingEvidence.inputs=resourceFiles.recheck(frozenBinding.path);
      if(analysis.cacheObservation===true)bindingEvidence.inputs=privateStorageInputs(bindingEvidence.inputs,frozenBinding,result);
      matchResourceBindingPin(frozenBinding,resourceBindingFilePin(output),output);
      verifyPrivateStorageAggregate(analysis,result,bindingEvidence.inputs);
      const analysisPath=path.join(output,'resource-analysis.json');
      write(analysisPath,analysis);
      bindingEvidence.analysis={record:analysis,file:filePin(analysisPath)};
      bindingEvidence.finalVerified=true;
    } catch(error) { bindingEvidence.finalVerified=false;bindingEvidence.failure=error.message; }
  }
  const captured=completeResourceCaptureResult(result,filePin(operationPath),filePin(path.join(output,'resource-policy.json')),bindingEvidence);
  const capturePath=path.join(output,'result.json');
  write(capturePath,captured);
  return {...summary,passed:captured.passed,operationPassed:captured.operationPassed,
    resourceGate:captured.resourceGate,...(resource.requestFields.namedCacheObservation!==undefined?{cacheObservationGate:captured.cacheObservationGate,
      captureGate:captured.captureGate,cacheBudgetGate:captured.cacheBudgetGate}:{}),result:path.relative(root,capturePath).replaceAll('\\','/'),
    operationResult:summary.result,failure:result.failure || captured.failure};
}
module.exports={settings,expectedSteps,validateRecords,mixedSettings,validateMixedRecords,sessionSettings,validateSessionRecords,verifyNativeIdentity,resourceBinding,longRunBinding,parseRunCommand,resourceCaptureResult,matchResourceBindingPin,completeResourceCaptureResult,verifyPrivateStorageAggregate,prepare,stage,verify,verifyInputs,run};
if(require.main===module) (async()=>{
  const [command,suite,...args]=process.argv.slice(2);
  let result;
  if(command==='prepare'&&(args.length===2||args.length===3))result=prepare(suite,...args);
  else if(command==='prepare-llvm'&&args.length===4) {
    const selected=require('../../tools/internal/llvm-installed-acceptance.cjs').select(root,args[0],args[1],args[3]);
    result=prepare(suite,args[0],args[1],args[2],'ordinary',selected.selection);
  }
  else if(command==='prepare-message-port'&&(args.length===2||args.length===3))result=prepare(suite,args[0],args[1],args[2],'message-port');
  else if(command==='stage'&&!args.length)result=stage(suite);
  else if(['run','run-resource','run-mixed','run-session','run-long','run-long-mixed','run-long-session'].includes(command)) {
    const selected=parseRunCommand(command,args);
    result=await run(suite,selected.config,selected.options);
  }
  else throw new Error('Invalid acceptance command');
  console.log(identity.encoded(result));
  if(result.passed===false)process.exitCode=1;
})().catch(error=>{console.error(error.message);process.exitCode=1;});
