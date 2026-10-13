'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const {expectedSteps} = require('./runner.cjs');
const {POLICY_SHA256, policyBytes, medianRatio, theilSen, analyzeResource, analyzeFile, hash} = require('./resource-validation.cjs');
const cacheReader=require('./native-cache-validation.cjs'),resourceReader=require('./resource-validation.cjs');

const config = {mode:'reload', cycles:100, warmup:10, settleMs:5000, timeoutMs:20000};
const scope = {kernel:'miniblink', renderer:'software', processModel:'single-process'};
const pid = 125, creationFileTime = '134360198253710104';
const encode = value => Buffer.from(JSON.stringify(value) + '\n');
const ndjson = values => Buffer.from(values.map(value => JSON.stringify(value)).join('\n') + '\n');

// Synthetic unit data checks the reader; it is never native acceptance evidence.
function fixture() {
  const records = [], values = {privateBytes:32*1024*1024, workingSetBytes:20*1024*1024,
    handles:100, threads:5, gdiObjects:20, userObjects:10, windowClasses:{SouiAcceptance:1}};
  let elapsedMs = 0;
  const emit = value => records.push({...value, elapsedMs:elapsedMs++});
  const sample = (phase, cycle) => emit({event:'sample', phase, cycle, ...structuredClone(values)});
  emit({event:'run.begin', ...config, pid, creationFileTime}); sample('process',0);
  for (const step of expectedSteps(config)) {
    emit({event:'step.begin', ...step}); emit({event:'step.complete', ...step});
    if (step.action === 'runtime.reject-busy') { elapsedMs += 5000; sample('mounted',0); }
    if (step.action === 'reload') { elapsedMs += 5000; sample('cycle',step.cycle); }
    if (step.action === 'close') { elapsedMs += 5000; sample('closed',step.cycle); }
    if (step.action === 'runtime.shutdown') sample('runtime-closed',step.cycle);
  }
  emit({event:'run.complete'});
  const terminal = {event:'terminal', reason:'completed', elapsedMs,
    rootExited:true, rootExitCode:0, activeBeforeCleanup:0, terminatedOwnedJob:false,
    closureConfirmed:true, activeProcesses:0, totalProcesses:1, limitTerminatedProcesses:0, passed:true};
  const ownedRows = [{schema:'soui.owned-process-run/v1', event:'start', pid, creationFileTime,
    timeoutMs:1200000, drainMs:2000, jobAssignedBeforeResume:true, breakawayAllowed:false},
    {event:'owned-member', pid, creationFileTime, image:'C:/payload/SouiAcceptance.exe'}, terminal];
  const context = {schema:'soui.owned-process-context/v1', runnerSha256:'a'.repeat(64),
    executableSha256:'b'.repeat(64), executable:'C:/payload/SouiAcceptance.exe', cwd:'C:/payload',
    args:['--mode=reload','--cycles=100','--warmup=10','--settle-ms=5000','--timeout-ms=20000','--report=C:/events.ndjson'],
    timeoutMs:1200000, drainMs:2000};
  const request = {schema:'soui.acceptance-request/v1', runId:'unit-run', ...config,
    consumerBuildId:'unit-build', sourceSha256:'c'.repeat(64), payloadSha256:'d'.repeat(64),
    resourcePolicySha256:POLICY_SHA256, resourceScope:structuredClone(scope)};
  const result = {...request, schema:'soui.acceptance-run/v1', config:structuredClone(config), passed:true, releaseReady:false,
    validation:{completedSteps:116, measuredCycles:100},
    outcome:{pid, exitCode:0, signal:null, timedOut:false,
      ownedProcess:{pid, creationFileTime, exitCode:0, supervisorExitCode:0, signal:null,
        spawnError:null, reportError:null, stopRequested:false, fallbackUsed:false,
        closureConfirmed:true, passed:true, terminal, runnerSha256:context.runnerSha256,
        executableSha256:context.executableSha256}}};
  return {records, ownedRows, context, request, result};
}
function evaluate(data, editEvidence = value => value) {
  const raw = ndjson(data.records), ownedRaw = ndjson(data.ownedRows);
  const result = structuredClone(data.result);
  result.logSha256 = hash(raw); result.outcome.ownedProcess.reportSha256 = hash(ownedRaw);
  return analyzeResource(result, raw, editEvidence({policyRaw:policyBytes(), requestRaw:encode(data.request),
    ownedRaw, contextRaw:encode(data.context), currentInputsMatched:true,
    sourceSha256:data.request.sourceSha256, payloadSha256:data.request.payloadSha256,
    runnerSha256:data.context.runnerSha256, executableSha256:data.context.executableSha256,
    executablePath:data.context.executable, payloadRoot:data.context.cwd, eventLogPath:'C:/events.ndjson'}));
}
function measured(data) { return data.records.filter(record => record.event==='sample' && record.phase==='cycle' && record.cycle>10); }

const cacheModulePin={path:'C:\\payload\\node.dll',bytes:1234,sha256:'e'.repeat(64)};
function cacheFixture(){
  const data=fixture();
  Object.assign(data.request,{namedCacheObservation:cacheReader.scope,resourcePolicySha256:resourceReader.CALIBRATION_POLICY_SHA256});
  data.context.args=resourceReader.resourceNativeArgs(data.request,'C:/events.ndjson');
  for(const row of data.records.filter(row=>row.event==='sample'&&row.phase!=='process')){
    const qpc=String(row.elapsedMs+100);
    row.nativeSdk={namedCaches:{...Object.fromEntries(cacheReader.u64Names.map(name=>[name,'0'])),
      schemaVersion:1,scope:cacheReader.scope,state:'sampled',hasCompletedSample:true,identityBound:true,moduleMatches:true,
      modulePath:cacheModulePin.path,inflight:false,runtimeRetired:false,structSize:216,abiVersion:1,status:1,reserved:0,
      blinkReserved:0,glyphReserved:0,blinkStatus:1,glyphStatus:1,resourceStatus:1,resourceDiscardable:0,
      processId:String(pid),processCreationFileTime:creationFileTime,engineOwnerThreadId:'42',recordingEpoch:'100',
      initializedQpc:'100',sampleQpc:qpc,qpcFrequency:'1000',resourceByteLimit:'1000',resourceEffectiveSingleLimit:'1000'},
      captureClock:{valid:true,frequency:'1000',qpc:String(row.elapsedMs+110)}};
    if(row.phase==='runtime-closed'){row.nativeSdk.namedCaches.state='unavailable';row.nativeSdk.namedCaches.runtimeRetired=true;}
  }
  return data;
}
const evaluateCache=data=>evaluate(data,value=>({...value,policyRaw:resourceReader.calibrationPolicyBytes(),modulePin:cacheModulePin}));

test('explicit cache calibration retains all101 required copies and never creates a byte budget',()=>{
  const out=evaluateCache(cacheFixture());
  assert.equal(out.resourceGate,'passed');assert.equal(out.cacheObservation,true);
  const cache=out.namedCacheCalibration;
  assert.equal(cache.configurationGate,'passed');assert.equal(cache.freshCoverageGate,'passed');assert.equal(cache.finiteGate,'passed');
  assert.equal(cache.observedPoints,101);assert.equal(cache.points[0].cycle,10);assert.equal(cache.points.at(-1).cycle,110);
  assert.equal(cache.cacheBudgetGate,'not-evaluated');assert.equal(cache.calibratedByteBudgets,null);assert.equal(cache.qualified,false);
});
test('missing, partial, stale, repeated or retired required calibration copies stay unqualified',()=>{
  for(const edit of [row=>delete row.nativeSdk.namedCaches,row=>row.nativeSdk.namedCaches.glyphStatus=2,
    row=>row.nativeSdk.captureClock.qpc=String(BigInt(row.nativeSdk.namedCaches.sampleQpc)+180001n),
    (row,data)=>row.nativeSdk.namedCaches.sampleQpc=measured(data)[1].nativeSdk.namedCaches.sampleQpc,
    row=>{row.nativeSdk.namedCaches.runtimeRetired=true;row.nativeSdk.namedCaches.state='unavailable';}]){
    const data=cacheFixture();edit(measured(data)[2],data);
    const out=evaluateCache(data);assert.equal(out.resourceGate,'passed');assert.equal(out.namedCacheCalibration.finiteGate,'not-evaluated');
    assert.equal(out.namedCacheCalibration.freshCoverageGate,'not-evaluated');
    assert.equal(out.namedCacheCalibration.cacheBudgetGate,'not-evaluated');
  }
});
test('known cache contradictions or retained violations outrank a separate missing calibration copy',()=>{
  for(const edit of [row=>row.nativeSdk.namedCaches.resourceRetainedBytes='1000',
    row=>row.nativeSdk.namedCaches.structSize=215]){
    const data=cacheFixture();delete measured(data)[1].nativeSdk.namedCaches;edit(measured(data)[2]);
    const cache=evaluateCache(data).namedCacheCalibration;
    assert.equal(cache.finiteGate,'failed');assert.equal(cache.configurationGate,'failed');
    assert.equal(cache.cacheBudgetGate,'not-evaluated');
  }
});

test('a required reload copy with known ABI contradiction and missing clock still fails calibration',()=>{
  const data=cacheFixture(),row=measured(data)[4];
  row.nativeSdk.namedCaches.structSize=215;delete row.nativeSdk.captureClock;
  const out=evaluateCache(data);
  assert.equal(out.resourceGate,'passed');assert.equal(out.namedCacheCalibration.diagnostics.invalidSamples,1);
  assert.equal(out.namedCacheCalibration.finiteGate,'failed');assert.equal(out.namedCacheCalibration.cacheBudgetGate,'not-evaluated');
});
test('calibration selection requires matching original policy bytes and actual seven-argument Owned context',()=>{
  for(const edit of [data=>data.request.resourcePolicySha256=POLICY_SHA256,
    data=>delete data.request.namedCacheObservation,
    data=>data.context.args.pop(),data=>data.context.args.push('--cache-observation=1')]){
    const data=cacheFixture();edit(data);assert.equal(evaluateCache(data).resourceGate,'not-evaluated');
  }
  assert.equal(evaluate(fixture()).namedCacheCalibration,undefined);
});

test('missing or empty complete-analysis records cannot inherit native, model, resource or calibration qualification',()=>{
  const analysis={resourceGate:'passed',passed:true,cacheObservation:true,
    namedCacheCalibration:{finiteGate:'passed'},subject:{pid,creationFileTime}};
  const complete={inputsMatched:true,nativeScopeObserved:false,inputBindingScope:'complete-acceptance-inputs',
    nativeToolchainProfile:'LLVM22MSVC142',loadedPayloadModule:cacheModulePin};
  for(const rows of [undefined,[]]){
    const out=resourceReader.completeResourceAnalysis(analysis,complete,rows);
    assert.equal(out.nativeScopeGate,'not-observed');assert.equal(out.nativeSoftwareGate,'not-evaluated');
    assert.equal(out.softwareProcessModelGate,'not-evaluated');assert.equal(out.resourceGate,'not-evaluated');
    assert.equal(out.namedCacheCalibration.finiteGate,'not-evaluated');assert.equal(out.passed,false);
  }
});

test('complete constant100 samples pass only the finite reload resource gate', () => {
  const result = evaluate(fixture());
  assert.equal(result.resourceGate,'passed'); assert.equal(result.measuredSamples.length,100);
  assert.equal(result.baseline.cycle,10); assert.equal(result.releaseReady,false);
  assert.equal(result.m0Complete,false); assert.equal(result.eightHourGate,'not-evaluated');
});
test('median uses both middle values and exact rational arithmetic', () => {
  assert.deepEqual(medianRatio([0,1,10,20]),{n:11n,d:2n});
  assert.deepEqual(medianRatio([Number.MAX_SAFE_INTEGER-1,Number.MAX_SAFE_INTEGER]),{n:18014398509481981n,d:2n});
});
test('Theil-Sen uses every pair and measured cycle position', () => {
  assert.deepEqual(theilSen([9,11,13,15]),{n:2n,d:1n});
  assert.throws(() => theilSen([1]), /two/);
});
test('private median at16MiB allowance passes; one byte beyond fails', () => {
  const data=fixture(); for(const sample of measured(data))sample.privateBytes+=16*1024*1024;
  assert.equal(evaluate(data).resourceGate,'passed');
  for(const sample of measured(data))sample.privateBytes++;
  assert.equal(evaluate(data).resourceGate,'failed');
});
test('ten-percent allowance rounds upward exactly', () => {
  const data=fixture(); for(const sample of data.records.filter(value=>value.event==='sample'))sample.privateBytes=200000001;
  for(const sample of measured(data))sample.privateBytes+=20000001;
  assert.equal(evaluate(data).resourceGate,'passed');
  for(const sample of measured(data))sample.privateBytes++;
  assert.equal(evaluate(data).resourceGate,'failed');
});
test('positive all100 private slope fails even when the last10 median stays within budget', () => {
  const data=fixture(); const list=measured(data);
  list.forEach((sample,index)=>{sample.privateBytes=10*1024*1024+index*200000;});
  const result=evaluate(data); assert.equal(result.resources.privateBytes.tailMedianWithinLimit,true);
  assert.equal(result.resources.privateBytes.slopeWithinLimit,false); assert.equal(result.resourceGate,'failed');
});
test('private slope at an exact integer allowance passes; one byte percycle beyond fails', () => {
  const data=fixture(); for(const sample of data.records.filter(value=>value.event==='sample'))sample.privateBytes=200000000;
  measured(data).forEach((sample,index)=>{sample.privateBytes=10000000+index*200000;});
  assert.equal(evaluate(data).resourceGate,'passed');
  measured(data).forEach((sample,index)=>{sample.privateBytes=10000000+index*200001;});
  assert.equal(evaluate(data).resourceGate,'failed');
});
test('working set is retained as observation rather than an unauthorized growth budget', () => {
  const data=fixture(); measured(data).forEach((sample,index)=>{sample.workingSetBytes+=index*1024*1024;});
  const result=evaluate(data); assert.equal(result.resourceGate,'passed');
  assert.equal(result.resources.workingSetBytes.gated,false); assert.equal(result.measuredSamples.at(-1).workingSetBytes,119*1024*1024);
});
test('an isolated peak stays in all100 raw samples and descriptive maximum', () => {
  const data=fixture(); measured(data)[40].privateBytes=256*1024*1024;
  const result=evaluate(data); assert.equal(result.measuredSamples.length,100);
  assert.equal(result.resources.privateBytes.max,256*1024*1024);
});
for(const metric of ['handles','threads','gdiObjects','userObjects']) {
  test(metric+' retained last10 increment fails without private-memory compensation', () => {
    const data=fixture(); for(const sample of measured(data).slice(-10))sample[metric]++;
    assert.equal(evaluate(data).resourceGate,'failed');
  });
}
test('positive handle slope fails even with tail values below the baseline', () => {
  const data=fixture(); measured(data).forEach((sample,index)=>{sample.handles=index;});
  const result=evaluate(data); assert.equal(result.resources.handles.tailMedianWithinLimit,true);
  assert.equal(result.resources.handles.slopeWithinLimit,false); assert.equal(result.resourceGate,'failed');
});
test('one extra window or class at any measured point fails even if it disappears', () => {
  const data=fixture(); measured(data)[3].windowClasses.MbToolTip=1;
  assert.equal(evaluate(data).resourceGate,'failed');
});
test('missing classes or resource sampling is not-evaluated', () => {
  const data=fixture(); delete measured(data)[2].windowClasses;
  assert.equal(evaluate(data).resourceGate,'not-evaluated');
  data.records=data.records.filter(value=>!(value.event==='sample' && value.cycle===20));
  assert.equal(evaluate(data).resourceGate,'not-evaluated');
});
test('old1000-operation, shortsettle or threewarmup data cannot inherit this policy', () => {
  for(const change of [{cycles:1000},{settleMs:20},{settleMs:1000},{warmup:3},{mode:'recreate'}]) {
    const data=fixture(); Object.assign(data.result.config,change); Object.assign(data.request,change);
    assert.equal(evaluate(data).resourceGate,'failed');
  }
});
test('declared5000ms without actual full settled interval fails', () => {
  const data=fixture(); const sample=measured(data)[4]; const at=data.records.indexOf(sample);
  sample.elapsedMs=data.records[at-1].elapsedMs+4999;
  assert.equal(evaluate(data).resourceGate,'failed');
});
test('missing, duplicate or wrong-window lifecycle success never grants a resource pass', () => {
  for(const edit of [data=>{data.records=data.records.filter(value=>!(value.event==='step.begin'&&value.action==='reload'&&value.cycle===20));},
    data=>{data.records.find(value=>value.event==='step.complete'&&value.action==='reload').window=2;}]) {
    const data=fixture(); edit(data); assert.equal(evaluate(data).resourceGate,'failed');
  }
});
test('numeric corruption and truncated rawlog cannot be dropped', () => {
  const data=fixture(); measured(data)[4].handles=-1;
  assert.equal(evaluate(data).resourceGate,'not-evaluated');
  assert.equal(analyzeResource(data.result,Buffer.from('{}'),{}).resourceGate,'not-evaluated');
});
test('missing policy/currentinputs and changed raw identity remain not-evaluated', () => {
  for(const edit of [value=>({...value,policyRaw:undefined}),value=>({...value,currentInputsMatched:false}),
    value=>({...value,sourceSha256:'e'.repeat(64)}),value=>({...value,payloadSha256:'e'.repeat(64)}),
    value=>({...value,ownedRaw:Buffer.from('{}\n')})])assert.equal(evaluate(fixture(),edit).resourceGate,'not-evaluated');
});
test('numericFILETIME, different PID and forced closure cannot qualify resource scope', () => {
  for(const edit of [data=>{data.result.outcome.ownedProcess.creationFileTime=134360198253710104;},
    data=>{data.ownedRows[0].pid++;},data=>{data.ownedRows.at(-1).terminatedOwnedJob=true;}]) {
    const data=fixture(); edit(data); assert.equal(evaluate(data).resourceGate,'not-evaluated');
  }
});
test('missing native creationFILETIME is not-evaluated even with an exact Owned receipt', () => {
  const data=fixture(); delete data.records[0].creationFileTime;
  const result=evaluate(data);
  assert.equal(result.resourceGate,'not-evaluated'); assert.equal(result.passed,false);
  assert.equal(result.failure,'native-creation-file-time-not-bound');
});
test('changed or noncanonical native creationFILETIME cannot inherit the Owned identity', () => {
  for(const value of ['134360198253710105','0134360198253710104']) {
    const data=fixture(); data.records[0].creationFileTime=value;
    const result=evaluate(data);
    assert.equal(result.resourceGate,'not-evaluated'); assert.equal(result.passed,false);
    assert.equal(result.failure,'native-creation-file-time-not-bound');
  }
});
test('numeric native creationFILETIME cannot substitute for the decimal string', () => {
  const data=fixture(); data.records[0].creationFileTime=Number(creationFileTime);
  const result=evaluate(data);
  assert.equal(result.resourceGate,'not-evaluated'); assert.equal(result.passed,false);
  assert.equal(result.failure,'native-creation-file-time-not-bound');
});
test('a second actual Job member rejects singleprocess even if it exits naturally', () => {
  const data=fixture(); data.ownedRows.at(-1).totalProcesses=2;
  data.ownedRows.splice(2,0,{event:'owned-member',pid:126,creationFileTime:'134360198253710105',image:'C:/helper.exe'});
  assert.equal(evaluate(data).resourceGate,'not-evaluated');
});
test('raw context argv, executable, runner and scope are exact bindings', () => {
  for(const edit of [data=>{data.context.args[1]='--cycles=99';},data=>{data.context.runnerSha256='e'.repeat(64);},
    data=>{data.request.resourceScope.renderer='gpu';}]) {
    const data=fixture(); edit(data); assert.equal(evaluate(data).resourceGate,'not-evaluated');
  }
});
test('native elapsed time cannot exceed the actual Owned interval', () => {
  const data=fixture(); data.ownedRows.at(-1).elapsedMs=500;
  assert.equal(evaluate(data).resourceGate,'failed');
});

function fileFixture() {
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-resource-reader-'));
  const source=path.join(root,'source'), payload=path.join(root,'payload'), output=path.join(root,'run');
  for(const directory of [source,payload,output])fs.mkdirSync(directory);
  const write=(file,bytes)=>{fs.writeFileSync(file,bytes);return{path:file,bytes:bytes.length,sha256:hash(bytes)};};
  const sourcePin=write(path.join(source,'source.cpp'),Buffer.from('synthetic source fixture\n'));
  const executable=write(path.join(payload,'SouiAcceptance.exe'),Buffer.from('synthetic never executed EXE fixture'));
  const runner=write(path.join(root,'runner.exe'),Buffer.from('synthetic never executed runner fixture'));
  const policy=write(path.join(root,'policy.json'),policyBytes());
  const snapshot=(directory,pins)=>{
    const files=pins.map(pin=>({path:path.basename(pin.path),bytes:pin.bytes,sha256:pin.sha256}));
    return{root:directory,files,sha256:hash(Buffer.from(JSON.stringify(files,null,2)+'\n')),inventoryRoots:['']};
  };
  const sourceSnapshot=snapshot(source,[sourcePin]), payloadSnapshot=snapshot(payload,[executable]);
  const data=fixture(); data.request.sourceSha256=data.result.sourceSha256=sourceSnapshot.sha256;
  data.request.payloadSha256=data.result.payloadSha256=payloadSnapshot.sha256;
  const events=path.join(output,'events.ndjson'), report=path.join(output,'owned-process.ndjson');
  Object.assign(data.context,{executable:executable.path,cwd:payload,executableSha256:executable.sha256,runnerSha256:runner.sha256});
  data.context.args[5]='--report='+events; data.ownedRows[1].image=executable.path;
  const ownedRaw=ndjson(data.ownedRows), raw=ndjson(data.records);
  Object.assign(data.result.outcome.ownedProcess,{report,reportSha256:hash(ownedRaw),
    executableSha256:executable.sha256,runnerSha256:runner.sha256}); data.result.logSha256=hash(raw);
  write(events,raw); write(report,ownedRaw); write(report+'.context.json',encode(data.context));
  write(path.join(output,'request.json'),encode(data.request));
  const resultPath=path.join(output,'result.json'), bindingPath=path.join(root,'bindings.json');
  write(resultPath,encode(data.result)); write(bindingPath,encode({schema:'soui.resource-file-bindings/v1',
    policy,runner,source:sourceSnapshot,payload:payloadSnapshot,executablePath:executable.path}));
  return{root,source,payload,output,resultPath,bindingPath};
}
function removeFixture(root) {
  const parent=fs.realpathSync(os.tmpdir()), target=fs.realpathSync(root);
  assert.ok(target.startsWith(parent+path.sep)&&path.basename(target).startsWith('soui-resource-reader-'));
  fs.rmSync(target,{recursive:true,force:true});
}
test('file reader verifies actual source/payload bytes and directory membership while keeping legacy software qualification pending', () => {
  const data=fileFixture();
  try {
    const observed=analyzeFile(data.resultPath,data.bindingPath);
    assert.equal(observed.resourceRuleObservation,'passed');assert.equal(observed.resourceGate,'not-evaluated');
    assert.equal(observed.nativeScopeGate,'not-bound-by-v1');
    fs.writeFileSync(path.join(data.source,'source.cpp'),'changed actual source');
    assert.equal(analyzeFile(data.resultPath,data.bindingPath).resourceGate,'not-evaluated');
  } finally { removeFixture(data.root); }
});
test('unmanifested current payload file, different rawlog and absent result are not-evaluated', () => {
  for(const edit of [data=>fs.writeFileSync(path.join(data.payload,'unlisted.dll'),'additional input'),
    data=>fs.appendFileSync(path.join(data.output,'events.ndjson'),'{}\n'),data=>fs.unlinkSync(data.resultPath)]) {
    const data=fileFixture();
    try {edit(data);assert.equal(analyzeFile(data.resultPath,data.bindingPath).resourceGate,'not-evaluated');}
    finally {removeFixture(data.root);}
  }
});

function sdkSourceFixture() {
  const data = fileFixture();
  const identity = require('../../tools/internal/delivery-identity.cjs');
  fs.mkdirSync(path.join(data.source,'sdk'));
  fs.writeFileSync(path.join(data.source,'sdk','api.h'),'synthetic sdk source\r\n');
  fs.writeFileSync(path.join(data.source,'VERSION'),'0.5.0-preview.1\r\n');
  const binding = JSON.parse(fs.readFileSync(data.bindingPath,'utf8'));
  binding.source = identity.sourceSnapshot(data.source);
  fs.writeFileSync(data.bindingPath, encode(binding));
  for (const name of ['request.json','result.json']) {
    const file = path.join(data.output,name), record = JSON.parse(fs.readFileSync(file,'utf8'));
    record.sourceSha256 = binding.source.sha256;
    fs.writeFileSync(file, encode(record));
  }
  return data;
}

test('explicit full SDK source binding tolerates generated React/Vue output without hiding new source', () => {
  const data = sdkSourceFixture();
  try {
    for (const dir of ['dist-react','dist-vue']) {
      fs.mkdirSync(path.join(data.source,'sdk',dir));
      fs.writeFileSync(path.join(data.source,'sdk',dir,'compiled.js'),'generated');
    }
    const observed=analyzeFile(data.resultPath,data.bindingPath);
    assert.equal(observed.resourceRuleObservation,'passed');assert.equal(observed.resourceGate,'not-evaluated');
    assert.equal(observed.nativeScopeGate,'not-bound-by-v1');
    fs.writeFileSync(path.join(data.source,'sdk','unlisted.h'),'new actual input');
    const result = analyzeFile(data.resultPath,data.bindingPath);
    assert.equal(result.resourceGate,'not-evaluated');
    assert.equal(result.failure,'current-sdk-source-inventory-changed');
  } finally { removeFixture(data.root); }
});

test('partial roots, extra files and unknown inventory modes cannot assert full SDK coverage', () => {
  const edits = [binding => {binding.source.inventoryRoots = ['sdk'];},
    binding => {binding.source.inventoryRoots.reverse();},
    binding => {binding.source.inventoryMode = 'unsupported';},
    binding => {
      const bytes = fs.readFileSync(path.join(binding.source.root,'source.cpp'));
      binding.source.files.push({path:'source.cpp',bytes:bytes.length,sha256:hash(bytes),normalization:'none'});
      binding.source.sha256 = hash(Buffer.from(JSON.stringify(binding.source.files,null,2)+'\n'));
    }];
  for (const edit of edits) {
    const data = sdkSourceFixture();
    try {
      const binding = JSON.parse(fs.readFileSync(data.bindingPath,'utf8'));
      edit(binding); fs.writeFileSync(data.bindingPath,encode(binding));
      assert.equal(analyzeFile(data.resultPath,data.bindingPath).resourceGate,'not-evaluated');
    } finally { removeFixture(data.root); }
  }
});

test('source-only exclusions do not hide an unmanifested generated directory in the runtime payload', () => {
  const data = sdkSourceFixture();
  try {
    fs.mkdirSync(path.join(data.payload,'dist-react'));
    fs.writeFileSync(path.join(data.payload,'dist-react','unexpected.js'),'unmanifested runtime input');
    assert.equal(analyzeFile(data.resultPath,data.bindingPath).resourceGate,'not-evaluated');
  } finally { removeFixture(data.root); }
});
