'use strict';
// Source declarations only in this packet. Run after composing with the pinned e45 dependencies.
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const p=require('../acceptance/long-run-policy.cjs'),v=require('../acceptance/long-run-validation.cjs');
const o=require('../acceptance/long-run-owners.cjs'),runner=require('../acceptance/runner.cjs');
const encode=value=>Buffer.from(JSON.stringify(value)+'\n');
const clone=value=>JSON.parse(JSON.stringify(value));
// Synthetic private DTOs exercise reader boundaries only, never Native collection.
function privateView(epoch='7',time='10'){
  const request={current_bytes:'10',high_water_bytes:'40',current_allocations:'1',high_water_allocations:'4',
    allocations_total:'3',deallocations_total:'2',failed_allocations_total:'1',allocated_bytes_total:'30',deallocated_bytes_total:'20',
    current_bytes_exact:true,current_allocations_exact:true};
  const aggregate={...request,current_bytes:'20',high_water_bytes:'60',current_allocations:'2',high_water_allocations:'6',
    allocations_total:'6',deallocations_total:'4',failed_allocations_total:'2',allocated_bytes_total:'60',deallocated_bytes_total:'40'};
  return {...view(),taskStorePrivateStorageStatus:'ready',taskStorePrivateStorage:{schemaVersion:1,scope:o.privateScope,
    observation:{state:'ready',owner_epoch:epoch,begin_monotonic_us:time,end_monotonic_us:String(BigInt(time)+1n),
      clock_invalid:false,counter_saturated:false},map_requests:{...request},private_string_requests:{...request},aggregate_requests:aggregate,
    task_entries:'1',operation_entries:'1',scope_entries:'1',coverage_mask:'7',gap_mask:'31',
    privateStringAllocatorRequestsCovered:true,requestsIncludePrivatePreparationAndRetirement:true,
    publicCopiesObserved:false,phasePartitionObserved:false,wholeHeapObserved:false,finalDisposalReceiptObserved:false,
    cacheBudgetGate:'not-evaluated',configuration:null,budgets:null,quota:null,calibrationPin:null}};
}
const privateFixtureBinding=()=>o.privateBinding({sourceSha256:'a'.repeat(64),payloadSha256:'b'.repeat(64),consumerBuildId:'synthetic',
  logSha256:'c'.repeat(64)},{pid:9,creationFileTime:'123'},{path:'C:/fixture/SouiAcceptance.exe',bytes:100,sha256:'d'.repeat(64)},
  {ownedLogSha256:'e'.repeat(64),contextSha256:'f'.repeat(64)},true);
const privateSample=(value,cycle=1)=>({event:'sample',phase:'cycle',cycle,elapsedMs:cycle,
  nativeSdk:{producer:'native-sdk-owner',pid:'9',creationFileTime:'123',captureClock:{valid:true,qpc:'100',frequency:'1000'},views:[value]}});

test('private request U64 and bool shapes reject numbers, leading zeros, overflow and truthy strings',()=>{
  for(const value of [1,'01','-1','18446744073709551616','1.0']){
    const v=privateView();v.taskStorePrivateStorage.map_requests.current_bytes=value;
    assert.equal(o.privateStoragePoint(v).state,'invalid');
  }
  const v=privateView();v.taskStorePrivateStorage.observation.clock_invalid='false';
  assert.equal(o.privateStoragePoint(v).state,'invalid');
  delete v.taskStorePrivateStorage.observation.clock_invalid;
  assert.equal(o.privateStoragePoint(v).state,'partial');
});
test('private aggregate checks actual refunds but retains independent event peaks',()=>{
  const v=privateView(),out=o.privateStoragePoint(v);
  assert.equal(out.state,'ready');assert.equal(out.derived.domains.aggregate_requests.high_water_bytes,'60');
  assert.equal(BigInt(out.raw.map_requests.high_water_bytes)+BigInt(out.raw.private_string_requests.high_water_bytes),80n);
  v.taskStorePrivateStorage.aggregate_requests.current_bytes='21';assert.equal(o.privateStoragePoint(v).state,'invalid');
  const bad=privateView();bad.taskStorePrivateStorage.map_requests.deallocations_total='3';
  assert.equal(o.privateStoragePoint(bad).state,'invalid');
  const peak=privateView();peak.taskStorePrivateStorage.aggregate_requests.high_water_bytes='39';
  assert.equal(o.privateStoragePoint(peak).state,'invalid');
});
test('busy default zero, saturation and inexact requests retain raw with no derived zero',()=>{
  for(const mutate of [v=>{v.taskStorePrivateStorageStatus='busy';v.taskStorePrivateStorage.observation.state='busy';
      for(const name of o.domainNames)for(const key of o.requestFields)v.taskStorePrivateStorage[name][key]='0';},
    v=>v.taskStorePrivateStorage.observation.counter_saturated=true,
    v=>v.taskStorePrivateStorage.map_requests.high_water_bytes='18446744073709551615',
    v=>v.taskStorePrivateStorage.map_requests.current_bytes_exact=false]){
    const v=privateView();mutate(v);const out=o.privateStoragePoint(v);
    assert.equal(out.state,'partial');assert.equal(out.derived,null);assert.deepEqual(out.raw,v.taskStorePrivateStorage);
  }
});
test('Store epoch is stable within a mounted owner and may change only on a new lifetime',()=>{
  const mount={event:'step.complete',action:'mount',window:1},first=privateSample(privateView()),second=privateSample(privateView('8','20'),2);
  assert.equal(o.privateStorageObservations([mount,first,second],privateFixtureBinding()).dataGate,'invalid');
  const closed={event:'step.complete',action:'close',window:1,retiredOwner:{...privateView(),hostClosed:true,
    lifetime:'after-host-close-before-owner-destruction'}};
  const out=o.privateStorageObservations([mount,first,closed,closed,mount,second],privateFixtureBinding());
  assert.notEqual(out.dataGate,'invalid');assert.equal(out.counts.retired,2);
  assert.equal(out.counts.repeated,2);
  assert.equal(out.points.at(-1).derived.cumulativeDelta,null);
  assert.equal(out.points[1].derived,null);assert.equal(out.points[2].derived,null);
  closed.retiredOwner.taskStorePrivateStorage.aggregate_requests.high_water_bytes='61';
  const changed=clone(closed);changed.retiredOwner.taskStorePrivateStorage.aggregate_requests.high_water_bytes='62';
  assert.equal(o.privateStorageObservations([mount,first,closed,changed],privateFixtureBinding()).dataGate,'invalid');
});
test('same Store copies, reversed clocks and missing capture clocks never create fresh points',()=>{
  const mount={event:'step.complete',action:'mount',window:1},row=privateSample(privateView());
  const out=o.privateStorageObservations([mount,row,clone(row)],privateFixtureBinding());
  assert.equal(out.counts.fresh,1);assert.equal(out.counts.repeated,1);assert.equal(out.points[1].derived,null);
  const reverse=privateSample(privateView('7','9'));
  assert.equal(o.privateStorageObservations([mount,row,reverse],privateFixtureBinding()).dataGate,'invalid');
  const duplicate=clone(row);duplicate.nativeSdk.views.push(privateView());
  assert.equal(o.privateStorageObservations([mount,duplicate],privateFixtureBinding()).dataGate,'invalid');
  assert.notEqual(o.privateStorageObservations([mount,{event:'sample',phase:'cycle'}],privateFixtureBinding()).dataGate,'invalid');
  row.nativeSdk.views[0].taskStorePrivateStorage.aggregate_requests.current_bytes='21';delete row.nativeSdk.captureClock;
  assert.equal(o.privateStorageObservations([mount,row],privateFixtureBinding()).dataGate,'invalid');
});
test('finite cache revision freezes temporal coverage with no empirical byte allowance',()=>{
  assert.equal(p.RULE.revision,'0.5.0-m0-named-cache-configuration-v2');
  const rule=p.RULE.namedCacheConfigurationPolicy;
  assert.equal(rule.maxCompletedCopyAgeMs,180000);assert.equal(rule.minNewSamplesPerHour,30);
  assert.equal(rule.maxNewSampleGapMs,180000);assert.equal(rule.calibratedByteBudgets,null);
  assert.equal(rule.broadCacheCoverage,false);
});
const obs=()=>({state:'ready',owner_epoch:'1',begin_monotonic_us:'10',end_monotonic_us:'11',clock_invalid:false,counter_saturated:false});
const fifo=()=>({observation:obs(),has_capacity_bound:true,capacity_bound:'580',queued:'0',executing:'0',high_water:'0'});
const tasks=()=>({observation:obs(),worker_limit:'2',inflight_limit:'32',subscription_limit:'128',store_limit:'1024',
  worker_slots:'2',active_handlers:'0',inflight:'0',queued:'0',inflight_high_water:'1',queued_high_water:'1',
  subscriptions:'0',subscription_high_water:'1',store_entries:'2',terminal_entries:'2',queued_entries:'0',running_entries:'0',
  timeout_observed_entries:'0',cancel_requested_entries:'0',owner_entries:'1',operation_entries:'2',result_cache_entries:'2',
  store_string_size_bytes:'20',store_string_capacity_bytes:'40',result_string_size_bytes:'10',result_string_capacity_bytes:'20'});
const permissions=()=>({observation:obs(),has_scope_capacity_bound:true,stored_scope_limit:'128',committed_alias_limit:'256',
  renewal_alias_transient_limit:'257',module_scope_limit:'512',stored_scope_owners:'0',pending_scope_reservations:'0',
  bearer_alias_entries:'0',pending_alias_reservations:'0',bearer_alias_high_water:'1',module_scope_slots_used:'2'});
const requests=()=>({observation:obs(),...Object.fromEntries([['ordinary',256],['control',128],['renewal',1]]
  .map(([key,limit])=>[key,{limit:String(limit),outstanding:'0',high_water:'1',acquired_total:'2',released_total:'2'}]))});
function view(retired=false){
  return {window:'1',ownerAlive:true,hostClosed:retired,lifetime:retired?'after-host-close-before-owner-destruction':'owner-alive',
    bridgeStatus:'ready',adapterStatus:retired?'not_started':'ready',bridge:{task_service:tasks(),permission_store:permissions(),
      gui_commands:{observation:{state:'not_started'}}},adapter:{process_engine_scope:'process',requests:requests(),
      bridge_worker_fifo:retired?{observation:{state:'not_started'}}:fifo(),bridge_accepting:!retired,context_receipt_before:'3',context_receipt_after:'3'}};
}
function ownerRows(){
  const process={scope:'process',engine_fifo:fifo(),mounted_hosts:'1',initialized:true,retired:false,terminal_snapshot:false};
  return [{event:'step.complete',action:'mount',window:1},
    {event:'sample',phase:'anchor',nativeSdk:{producer:'native-sdk-owner',processStatus:'ready',process,views:[view()]}},
    {event:'step.complete',action:'close',window:1,retiredOwner:view(true)},
    {event:'sample',phase:'runtime-closed',nativeSdk:{producer:'native-sdk-owner',processStatus:'ready',
      process:{...process,mounted_hosts:'0',initialized:false,retired:true,terminal_snapshot:true},views:[]}}];
}
function samples(mode='mixed'){
  const phase=mode==='session'?'anchor':'cycle',origin=mode==='session'?2000:0;
  const rows=mode==='session'?[{event:'session.report',window:1,elapsedMs:1000,data:{phase:'started'}},
    {event:'sample',phase:'anchor',cycle:0,elapsedMs:origin}]:[{event:'cycle.begin',cycle:1,elapsedMs:0}];
  for(let hour=0;hour<8;hour++)for(let i=0;i<30;i++)rows.push({event:'sample',phase,cycle:1+hour*30+i,
    elapsedMs:origin+hour*p.HOUR+60000+i*120000,privateBytes:100000,workingSetBytes:200000,
    handles:10,threads:2,gdiObjects:2,userObjects:3,windowClasses:{A:1}});
  return rows;
}
function actions(){
  const rows=[];
  for(let hour=0;hour<8;hour++)for(let i=0;i<30;i++){
    const start=hour*p.HOUR+60000+i*120000,cycle=hour*30+i+1;
    rows.push({event:'step.begin',action:'reload',window:1,cycle,elapsedMs:start},
      {event:'step.complete',action:'reload',window:1,cycle,elapsedMs:start+10},
      {event:'step.begin',action:'reload',window:1,cycle,elapsedMs:start+20},
      {event:'step.complete',action:'reload',window:1,cycle,elapsedMs:start+40});
  }return rows;
}
function ownedFixture(){
  const config=p.fixedConfig('mixed'),runRoot=path.resolve('out/acceptance/fixed/mixed-00000000-0000-4000-8000-000000000001');
  const exe=path.join(path.dirname(runRoot),'runtime/SouiAcceptance.exe'),eventLogPath=path.join(runRoot,'events.ndjson');
  const runnerHash='a'.repeat(64),exeHash='b'.repeat(64),timeout=v.processBudget(config)+180000;
  const start={schema:'soui.owned-process-run/v1',event:'start',pid:42,creationFileTime:'134363106954925971',
    timeoutMs:timeout,drainMs:2000,jobAssignedBeforeResume:true,breakawayAllowed:false};
  const terminal={event:'terminal',reason:'completed',rootExited:true,rootExitCode:0,activeBeforeCleanup:0,
    activeProcesses:0,totalProcesses:1,terminatedOwnedJob:false,limitTerminatedProcesses:0,closureConfirmed:true,passed:true,elapsedMs:v.processBudget(config)};
  const rows=[start,{event:'owned-member',pid:42,creationFileTime:start.creationFileTime,image:exe},terminal];
  const context={schema:'soui.owned-process-context/v1',executable:exe,cwd:path.dirname(exe),args:v.nativeArgs(config,eventLogPath),
    timeoutMs:timeout,drainMs:2000,runnerSha256:runnerHash,executableSha256:exeHash};
  const raw=Buffer.from(rows.map(row=>JSON.stringify(row)).join('\n')+'\n');
  const finished={pid:42,creationFileTime:start.creationFileTime,exitCode:0,supervisorExitCode:0,passed:true,
    stopRequested:false,fallbackUsed:false,closureConfirmed:true,terminal,runnerSha256:runnerHash,executableSha256:exeHash,reportSha256:p.hash(raw)};
  return {config,result:{outcome:{pid:42,exitCode:0,timedOut:false,processTimeoutMs:v.processBudget(config),ownedProcess:finished}},
    evidence:{ownedRaw:raw,contextRaw:encode(context),executablePath:exe,payloadRoot:path.dirname(exe),
      runnerSha256:runnerHash,executableSha256:exeHash,eventLogPath},rows,context};
}
function changeOwned(f,mutate){
  mutate(f.rows);const raw=Buffer.from(f.rows.map(row=>JSON.stringify(row)).join('\n')+'\n');
  f.evidence.ownedRaw=raw;f.result.outcome.ownedProcess.reportSha256=p.hash(raw);
  f.result.outcome.ownedProcess.terminal=f.rows.at(-1);
}
test('session config accepts its real existing key order and rejects extra knobs',()=>{
  const real=runner.sessionSettings(28800000,500,20000,60000);assert.ok(p.policySelection(real));
  assert.throws(()=>p.policySelection({...real,forcedGc:true}));
  assert.throws(()=>p.policySelection({...real,durationMs:28799999}));
});
test('fixed command selects 1000 for each public mode and exact independent duration modes',()=>{
  for(const mode of ['reload','recreate','multiwindow','bridge'])assert.equal(runner.parseRunCommand('run-long',[mode]).config.cycles,1000);
  assert.equal(runner.parseRunCommand('run-long-session',[]).config.idleMs,60000);
  assert.equal(runner.parseRunCommand('run-long-mixed',[]).config.idleMs,10000);
  assert.throws(()=>runner.parseRunCommand('run-long-session',['1000']));
  assert.throws(()=>runner.parseRunCommand('run-long',['all']));
});
test('legacy 100reload resource command and normal default remain separate',()=>{
  assert.deepEqual(runner.parseRunCommand('run-resource',[]).config,{mode:'reload',cycles:100,warmup:10,settleMs:5000,timeoutMs:20000});
  assert.equal(runner.resourceBinding(runner.settings('reload',3,0,500,20000)),null);
  assert.throws(()=>runner.longRunBinding(p.fixedConfig('reload'),{longRun:true,resource:true}));
});
test('nearest-rank p50/p95 and percentage ceiling are exact',()=>{
  const values=Array.from({length:20},(_,i)=>i);assert.equal(v.nearestRank(values,50),9);assert.equal(v.nearestRank(values,95),18);
  assert.equal(v.percentCeil(1,20),1n);assert.equal(v.amplitude(1,32,20),32n);
});
test('eight point exact TS contains 28 pairs and rejects a positive trend',()=>{
  assert.deepEqual(v.trend(Array(8).fill(5)),{numerator:'0',denominator:'1',pairCount:28,nonIncreasing:true});
  assert.equal(v.trend([0,1,2,3,4,5,6,7]).nonIncreasing,false);
});
test('amplitude allowance and TS are separate requirements',()=>{
  assert.equal(v.checkSeries(Array.from({length:8},(_,i)=>({p50:i===7?125:100,p95:100})),25,100,25,25).gate,'passed');
  assert.equal(v.checkSeries(Array.from({length:8},(_,i)=>({p50:i===7?126:100,p95:100})),25,100,25,25).gate,'failed');
  assert.equal(v.checkSeries(Array.from({length:8},(_,i)=>({p50:100+i,p95:100+i})),25,100,25,25).gate,'failed');
});
test('all eight resource hours include start/end edge gaps',()=>{
  const grouped=v.hourlySamples(samples(),p.fixedConfig('mixed'));assert.equal(grouped.gate,'passed');
  assert.equal(grouped.cadence.length,8);assert.equal(grouped.cadence[0].maxGapMs,120000);
  const leading=samples();leading.filter(row=>row.event==='sample'&&row.elapsedMs<p.HOUR)
    .forEach((row,i)=>row.elapsedMs=200000+i*110000);
  assert.equal(v.hourlySamples(leading,p.fixedConfig('mixed')).gate,'not-evaluated');
  const trailing=samples();trailing.filter(row=>row.event==='sample'&&row.elapsedMs<p.HOUR)
    .forEach((row,i)=>row.elapsedMs=1000+i*110000);
  assert.equal(v.hourlySamples(trailing,p.fixedConfig('mixed')).gate,'not-evaluated');
});
test('short cadence cannot borrow points from another phase',()=>{
  const rows=samples();rows.find(row=>row.event==='sample').phase='closed';
  assert.equal(v.hourlySamples(rows,p.fixedConfig('mixed')).gate,'not-evaluated');
});
test('session origin binds actual started and anchor0, not arbitrary run begin',()=>{
  const rows=samples('session');assert.equal(v.originFor(rows,p.fixedConfig('session')),2000);
  assert.throws(()=>v.originFor(rows.filter(row=>row.event!=='session.report'),p.fixedConfig('session')));
});
test('tail and final boundary remain separate without deleting raw samples',()=>{
  const rows=samples();rows.push({...rows.at(-1),elapsedMs:p.DURATION+5000}, {event:'sample',phase:'runtime-closed',elapsedMs:p.DURATION+6000});
  const group=v.hourlySamples(rows,p.fixedConfig('mixed'));assert.equal(group.tailSamples,1);
  assert.equal(group.hours.flat().length,240);assert.equal(group.retainedBoundarySamples.length,1);
});
test('same phase class change fails, working set only describes',()=>{
  const grouped=v.hourlySamples(samples(),p.fixedConfig('mixed'));
  grouped.hours[7][0].workingSetBytes=999999999;assert.equal(v.resourceTrends(grouped).gate,'passed');
  grouped.hours[7][0].windowClasses={A:2};assert.equal(v.resourceTrends(grouped).windowClassesGate,'failed');
});
test('counter p95 increase fails even if TS remains zero',()=>{
  const grouped=v.hourlySamples(samples(),p.fixedConfig('mixed'));
  grouped.hours[7].forEach(row=>row.handles=11);assert.equal(v.resourceTrends(grouped).metrics.handles.gate,'failed');
});
test('response actions remain distinct by window and within-cycle position',()=>{
  const result=v.actionTrends(actions(),0);assert.equal(result.gate,'passed');assert.equal(result.groups.length,2);
  assert.ok(result.groups.every(group=>group.key.includes('window=1|position=')));
});
test('missing hour in repeated business action does not pass via boundary actions',()=>{
  const rows=actions().filter(row=>row.cycle<=210);
  rows.push({event:'step.begin',action:'runtime.shutdown',window:0,cycle:9,elapsedMs:p.DURATION},
    {event:'step.complete',action:'runtime.shutdown',window:0,cycle:9,elapsedMs:p.DURATION+1});
  assert.equal(v.actionTrends(rows,0).gate,'not-evaluated');
});
test('crossing tail action is retained outside full-hour response statistics',()=>{
  const rows=actions();rows.push({event:'step.begin',action:'mount',window:2,cycle:10,elapsedMs:p.DURATION-1},
    {event:'step.complete',action:'mount',window:2,cycle:10,elapsedMs:p.DURATION+2});
  assert.equal(v.actionTrends(rows,0).tailActions.length,1);
});
test('queue capacity excludes the independently executing callback',()=>{
  const value=fifo();value.queued='580';value.executing='1';value.high_water='580';
  assert.doesNotThrow(()=>o.queue(value,new Map(),'engine'));
  value.queued='581';assert.throws(()=>o.queue(value,new Map(),'engine'),error=>error.failed===true);
});
test('retired cancelled terminal history and worker slots are not false leaks',()=>{
  assert.doesNotThrow(()=>o.tasks(tasks(),new Map(),'task',true));
  const value=tasks();value.inflight='1';assert.throws(()=>o.tasks(value,new Map(),'task',true));
});
test('retired revoked module borrows do not require module zero',()=>{
  assert.doesNotThrow(()=>o.permissions(permissions(),new Map(),'permission',true));
  const value=permissions();value.module_scope_slots_used='513';assert.throws(()=>o.permissions(value,new Map(),'permission',true));
});
test('optional GUI not_started is named absence, not a fabricated zero',()=>{
  assert.deepEqual(o.gui({observation:{state:'not_started'}},new Map(),'gui',false),
    {state:'not_started',scope:'optional-gui-coordinator-not-instantiated'});
});
test('owner cap and contractual drain can pass with an explicit cache gap',()=>{
  const result=o.analyzeOwners(ownerRows());assert.equal(result.capacityGate,'passed');assert.equal(result.terminalDrainGate,'passed');
  assert.equal(result.cacheBudgetGate,'not-evaluated');assert.ok(result.cacheGaps.includes('Blink/V8/Skia and other third-party caches'));
});
test('retired adapter ready and absent adapter unavailable do not substitute stopped not_started',()=>{
  const rows=ownerRows();rows[2].retiredOwner.adapterStatus='ready';
  assert.equal(o.analyzeOwners(rows).terminalDrainGate,'not-evaluated');
  rows[2].retiredOwner.adapterStatus='unavailable';assert.equal(o.analyzeOwners(rows).terminalDrainGate,'not-evaluated');
});
test('task epoch cannot change inside a retained host lifetime',()=>{
  const rows=ownerRows();rows[2].retiredOwner.bridge.task_service.observation.owner_epoch='2';
  assert.equal(o.analyzeOwners(rows).capacityGate,'failed');
});
test('request lane outstanding matches actual acquired minus released',()=>{
  const value=requests();value.control.acquired_total='4';assert.throws(()=>o.requests(value,new Map(),'request',false));
});
test('decimal integers reject unsafe numeric DTOs and preserve all uint64 precision',()=>{
  assert.equal(o.count('18446744073709551615'),18446744073709551615n);
  assert.throws(()=>o.count(9007199254740992));assert.throws(()=>o.count('18446744073709551616'));
});
test('natural single-owned original exact duration args prove PID resource tree',()=>{
  const f=ownedFixture(),result=v.verifyOwned(f.result,f.evidence,f.config);
  assert.equal(result.treeGate,'passed');assert.equal(result.subject.creationFileTime,'134363106954925971');
});
test('actual child membership needs per-member metrics and is specifically NE',()=>{
  const f=ownedFixture();changeOwned(f,rows=>{rows.at(-1).totalProcesses=2;rows.splice(2,0,{event:'owned-member',pid:43,creationFileTime:'2',image:'child.exe'});});
  const result=v.verifyOwned(f.result,f.evidence,f.config);assert.equal(result.gate,'passed');assert.equal(result.treeGate,'not-evaluated');
});
test('forced cleanup, supervisor exit and FILETIME mismatch remain failures',()=>{
  const force=ownedFixture();changeOwned(force,rows=>rows.at(-1).activeBeforeCleanup=1);
  assert.throws(()=>v.verifyOwned(force.result,force.evidence,force.config),error=>error.failed===true);
  const identity=ownedFixture();identity.result.outcome.ownedProcess.creationFileTime='134363106954925972';
  assert.throws(()=>v.verifyOwned(identity.result,identity.evidence,identity.config));
  const exit=ownedFixture();exit.result.outcome.ownedProcess.supervisorExitCode=1;
  assert.throws(()=>v.verifyOwned(exit.result,exit.evidence,exit.config));
});
test('exact argv and original 2000ms drain cannot be replaced by duration strings',()=>{
  const f=ownedFixture();f.context.args[1]='--cycles=1000';f.evidence.contextRaw=encode(f.context);
  assert.throws(()=>v.verifyOwned(f.result,f.evidence,f.config));
  const drain=ownedFixture();drain.context.drainMs=1;drain.evidence.contextRaw=encode(drain.context);
  assert.throws(()=>v.verifyOwned(drain.result,drain.evidence,drain.config));
});
test('required gates conjoin; observed failure takes precedence over missing cache',()=>{
  assert.equal(v.combine(['passed','passed']),'passed');assert.equal(v.combine(['passed','not-evaluated']),'not-evaluated');
  assert.equal(v.combine(['not-evaluated','failed']),'failed');
});

test('committed alias snapshot 257 fails even though transient high water 257 is legal',()=>{
  const good=permissions();good.bearer_alias_high_water='257';assert.doesNotThrow(()=>o.permissions(good,new Map(),'permission',false));
  const bad=permissions();bad.bearer_alias_entries='257';assert.throws(()=>o.permissions(bad,new Map(),'permission',false));
});
test('all task store state counters form an exact partition and unfinished cancellation is not drain',()=>{
  const broken=tasks();broken.terminal_entries='1';assert.throws(()=>o.tasks(broken,new Map(),'task',false));
  const pending=tasks();pending.terminal_entries='1';pending.cancel_requested_entries='1';
  assert.doesNotThrow(()=>o.tasks(pending,new Map(),'task',false));assert.throws(()=>o.tasks(pending,new Map(),'task',true));
});
test('global Engine epoch cannot silently restart under a new history key',()=>{
  const rows=ownerRows();rows[3].nativeSdk.process.engine_fifo=clone(rows[3].nativeSdk.process.engine_fifo);
  rows[3].nativeSdk.process.engine_fifo.observation.owner_epoch='2';assert.equal(o.analyzeOwners(rows).capacityGate,'failed');
});
test('a ready retired FIFO must actually be drained',()=>{
  const value=fifo();value.queued='1';assert.throws(()=>o.queue(value,new Map(),'retired',true));
});
test('session final anchor close is retained as tail rather than a fabricated recurring response group',()=>{
  const rows=actions();rows.push({event:'step.begin',action:'close',window:3,cycle:99,elapsedMs:p.DURATION+1},
    {event:'step.complete',action:'close',window:3,cycle:99,elapsedMs:p.DURATION+2});
  const analysis=v.actionTrends(rows,0);assert.equal(analysis.gate,'passed');assert.equal(analysis.groups.length,2);
  assert.equal(analysis.tailActions.length,1);
});
test('request pins bind all exact current identities and disallow duration/cycle mixing',()=>{
  const config=p.fixedConfig('session'),selection=p.policySelection(config),runId='00000000-0000-4000-8000-000000000001';
  const build={consumerBuildId:'c'.repeat(64),sourceSha256:'d'.repeat(64),payload:{sha256:'e'.repeat(64)}};
  const record={schema:'soui.acceptance-request/v1',runId,...config,...selection.requestFields,consumerBuildId:build.consumerBuildId,
    sourceSha256:build.sourceSha256,payloadSha256:build.payload.sha256,startedAt:'2026-10-13T00:00:00.000Z'};
  const root=path.resolve('out/acceptance/fixed/session-'+runId);
  assert.deepEqual(p.validateFrozenRequest(record,build,root),config);
  assert.throws(()=>p.validateFrozenRequest({...record,cycles:1000},build,root));
  assert.throws(()=>p.validateFrozenRequest({...record,sourceSha256:'f'.repeat(64)},build,root));
  assert.throws(()=>p.validateFrozenRequest({...record,longRunPolicySha256:'0'.repeat(64)},build,root));
});
test('file source input cannot manufacture a native compatibility model observation',()=>{
  assert.throws(()=>v.nativeScope([{event:'sample',phase:'cycle',nativeSdk:{pid:'42',creationFileTime:'1',captureThreadId:'2',
    captureClock:{captureThreadId:'2'},rendererModel:{state:'ready',providerId:1,backendId:1}}}],{pid:42,creationFileTime:'1'},{path:'C:/node.dll'}));
});

function modelFixture(closed=false,created=2){
  const raw={schemaVersion:1,scope:'bound-dll-completed-compositor-calls',state:closed?'closed':'ready',modulePath:'C:/native/node.dll',
    moduleMatches:true,identityBound:true,terminalSnapshot:closed,structSize:168,abiVersion:1,status:closed?4:1,flags:closed?47:39,lastDrawKind:1,reserved:0,
    processId:'42',processCreationFileTime:'1',engineOwnerThreadId:'5',qpcFrequency:'1000',recordingEpoch:'10',initializedQpc:'10',
    renderersCreatedTotal:String(created),renderersClosedTotal:closed?String(created):'0',renderersLive:closed?'0':String(created),
    rasterDrawsTotal:closed?'12':'10',gpuSkiaDrawsTotal:'0',drawEventsTotal:closed?'12':'10',lastDrawQpc:closed?'100':'80',
    lastDrawThreadId:'5',lastDrawProcessId:'42',lastRendererCloseQpc:closed?'110':'0',closedSnapshotQpc:closed?'110':'0',engineFinalizedQpc:closed?'120':'0'};
  return {schemaVersion:2,scope:'bound-compatibility-backend-workload',state:raw.state,modulePath:raw.modulePath,
    moduleMatches:true,identityBound:true,terminalSnapshot:closed,structSize:264,abiVersion:2,status:raw.status,flags:19,
    providerId:1,backendId:1,factoryCoverageMask:127,compiledGpuSupport:0,backendInitializationQpc:'9',compositor:raw,
    mediaFactoryAttempts:'0',pluginFactoryAttempts:'0',offscreenFactoryAttempts:'0',sharedContextFactoryAttempts:'0',
    externalFactoryAttempts:'0',platformMismatchTotal:'0',unknownRouteTotal:'0'};
}
function nativeRows(){
  const model=modelFixture(),closed=modelFixture(true),clock=qpc=>({valid:true,phase:'synchronous-native-read-complete',qpc:String(qpc),frequency:'1000',captureThreadId:'6'});
  return [{event:'sample',phase:'anchor',nativeSdk:{pid:'42',creationFileTime:'1',captureThreadId:'6',captureClock:clock(100),
    renderer:{...model.compositor,renderersCreatedTotal:'1',renderersLive:'1'},rendererModel:model}},
    {event:'sample',phase:'runtime-closed',nativeSdk:{pid:'42',creationFileTime:'1',captureThreadId:'6',captureClock:clock(120),
      renderer:clone(closed.compositor),rendererModel:closed}},
    {event:'step.complete',action:'runtime.shutdown-idempotent',nativeRenderer:{captureClock:clock(130),renderer:clone(closed.compositor),model:clone(closed)}}];
}
test('sequential live getters may advance without pretending to be an atomic snapshot',()=>{
  const result=v.nativeScope(nativeRows(),{pid:42,creationFileTime:'1'},{path:'C:/native/node.dll'});
  assert.equal(result.gate,'passed');assert.equal(result.terminalSnapshot.compositor.renderersLive,'0');
});
test('separately observed GPU history cannot disappear in the later model getter',()=>{
  const rows=nativeRows();rows[0].nativeSdk.renderer.gpuSkiaDrawsTotal='1';rows[0].nativeSdk.renderer.drawEventsTotal='11';
  assert.throws(()=>v.nativeScope(rows,{pid:42,creationFileTime:'1'},{path:'C:/native/node.dll'}),error=>error.failed===true);
});
test('a partial close with another live renderer does not imply a whole-process terminal',()=>{
  const rows=nativeRows();rows[0].nativeSdk.rendererModel.compositor.renderersClosedTotal='1';
  rows[0].nativeSdk.rendererModel.compositor.renderersLive='1';rows[0].nativeSdk.rendererModel.compositor.lastRendererCloseQpc='70';
  assert.equal(v.nativeScope(rows,{pid:42,creationFileTime:'1'},{path:'C:/native/node.dll'}).gate,'passed');
});
test('first ready no-draw prefix is retained but terminal must contain a real completed CPU draw',()=>{
  const rows=nativeRows();for(const raw of [rows[0].nativeSdk.renderer,rows[0].nativeSdk.rendererModel.compositor]){
    Object.assign(raw,{flags:7,lastDrawKind:0,rasterDrawsTotal:'0',drawEventsTotal:'0',lastDrawQpc:'0',lastDrawThreadId:'0',lastDrawProcessId:'0'});
  }
  assert.equal(v.nativeScope(rows,{pid:42,creationFileTime:'1'},{path:'C:/native/node.dll'}).gate,'passed');
});
test('the first native terminal cannot be rewritten on the idempotent read',()=>{
  const rows=nativeRows();rows[2].nativeRenderer.model.compositor.engineFinalizedQpc='121';
  assert.throws(()=>v.nativeScope(rows,{pid:42,creationFileTime:'1'},{path:'C:/native/node.dll'}),error=>error.failed===true);
});
function captureFixture(mode='reload'){
  const config=p.fixedConfig(mode),policyPin={path:path.resolve('long-run-policy.json'),bytes:p.policyBytes(mode).length,sha256:p.hash(p.policyBytes(mode))};
  const operation={schema:'soui.acceptance-run/v1',runId:'00000000-0000-4000-8000-000000000001',config,passed:true,
    consumerBuildId:'a'.repeat(64),sourceSha256:'b'.repeat(64),payloadSha256:'c'.repeat(64),logSha256:'d'.repeat(64),releaseReady:false};
  const gates=Object.fromEntries((['mixed','session'].includes(mode)?v.requiredGates(mode):['inputs','operations','owned','tree']).map(name=>[name,'passed']));
  const analysis={schema:'soui.long-run-acceptance-analysis/v1',mode,runId:operation.runId,gates,...v.aggregate(mode,gates),
    consumerBuildId:operation.consumerBuildId,sourceSha256:operation.sourceSha256,payloadSha256:operation.payloadSha256,
    policySha256:policyPin.sha256,logSha256:operation.logSha256,releaseReady:false};
  const actualElapsed=['mixed','session'].includes(mode)?p.DURATION+1000:60000;
  operation.outcome={ownedProcess:{terminal:{elapsedMs:actualElapsed}}};
  analysis.actualClock={gate:'passed',nativeRunCompleteElapsedMs:actualElapsed-1,ownedTerminalElapsedMs:actualElapsed,
    requestedDurationMs:config.durationMs??null};
  if(config.durationMs!==undefined)analysis.duration={gate:'passed',originMs:0,endMs:p.DURATION,elapsedMs:p.DURATION,ownedElapsedMs:actualElapsed};
  const evidence={bindingFile:{path:path.resolve('file-bindings.json'),bytes:1,sha256:'e'.repeat(64)},
    inputs:{inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',closureObserved:false,nativeScopeGate:'not-observed',
      resourceGate:'not-evaluated',passed:false,releaseReady:false},analysis:{record:analysis,file:{path:path.resolve('long-run-analysis.json'),bytes:1,sha256:'f'.repeat(64)}},
    finalVerified:true,failure:null};
  return {operation,operationPin:{path:path.resolve('operation-result.json'),bytes:1,sha256:'0'.repeat(64)},policyPin,evidence};
}
const captured=f=>v.captureResult(f.operation,f.operationPin,f.policyPin,f.evidence);
test('a coherent ordinary classifier DTO closes only the thousand-operation scope',()=>{
  const result=captured(captureFixture());assert.equal(result.passed,true);assert.equal(result.thousandCycleOperationGate,'passed');
  assert.equal(result.eightHourGate,'not-evaluated');assert.equal(result.releaseReady,false);
});
test('a single changed operations gate cannot retain a previously green classifier',()=>{
  const f=captureFixture();f.evidence.analysis.record.gates.operations='failed';
  assert.equal(captured(f).passed,false);assert.equal(captured(f).longRunGate,'not-evaluated');
});
test('a single missing cache gate cannot retain a previously green duration classifier',()=>{
  const f=captureFixture('mixed');f.evidence.analysis.record.gates.cacheBudget='not-evaluated';
  assert.equal(captured(f).passed,false);assert.equal(captured(f).eightHourGate,'not-evaluated');
});
test('mode-specific aggregate, passed and resource/eight-hour fields must all agree',()=>{
  for(const field of ['requiredGates','passed','resourceGate','eightHourGate','thousandCycleOperationGate']){
    const f=captureFixture();f.evidence.analysis.record[field]=field==='requiredGates'?['operations']:field==='passed'?false:f.evidence.analysis.record[field]==='passed'?'not-evaluated':'passed';
    assert.equal(captured(f).passed,false);
  }
});
test('a coherent measured failure outranks a separate missing-cache gate',()=>{
  const f=captureFixture('session'),analysis=f.evidence.analysis.record;
  analysis.gates.cacheBudget='not-evaluated';analysis.gates.actions='failed';Object.assign(analysis,v.aggregate('session',analysis.gates));
  assert.equal(captured(f).longRunGate,'failed');assert.equal(captured(f).passed,false);
});

test('finite named cache failure outranks full budget NE in both long aggregate and classifier',()=>{
  for(const mode of ['mixed','session']){
    const f=captureFixture(mode),analysis=f.evidence.analysis.record;
    analysis.gates.cacheBudget='not-evaluated';analysis.gates.namedCacheConfiguration='failed';
    analysis.gates.namedCacheCoverage='not-evaluated';Object.assign(analysis,v.aggregate(mode,analysis.gates));
    assert.equal(analysis.longRunGate,'failed');assert.equal(analysis.resourceGate,'failed');
    assert.equal(captured(f).longRunGate,'failed');assert.equal(captured(f).passed,false);
    analysis.gates.namedCacheConfiguration='passed';Object.assign(analysis,v.aggregate(mode,analysis.gates));
    assert.equal(captured(f).longRunGate,'not-evaluated');assert.equal(analysis.gates.cacheBudget,'not-evaluated');
  }
});

test('long classifier cannot hide a failed named point or manufacture a calibrated whole budget',()=>{
  const f=captureFixture('mixed'),analysis=f.evidence.analysis.record;
  assert.equal(captured(f).passed,false);
  analysis.gates.cacheBudget='not-evaluated';analysis.gates.namedCacheConfiguration='failed';
  analysis.namedCaches={configurationPoints:[{configurationGate:'failed'}],configurationGate:'failed',
    freshCoverageGate:'passed',cacheBudgetGate:'not-evaluated',calibratedByteBudgetGate:'not-evaluated',calibratedByteBudgets:null};
  Object.assign(analysis,v.aggregate('mixed',analysis.gates));assert.equal(captured(f).longRunGate,'failed');
  analysis.gates.namedCacheConfiguration='passed';Object.assign(analysis,v.aggregate('mixed',analysis.gates));
  assert.equal(captured(f).longRunGate,'not-evaluated');assert.equal(captured(f).passed,false);
});
test('missing group response samples never borrow resource cadence',()=>{
  const rows=actions().filter(row=>row.cycle%30!==0);assert.equal(v.actionTrends(rows,0).gate,'not-evaluated');
});

test('duplicate live owner DTO cannot hide another actual window',()=>{
  const rows=ownerRows();rows.splice(1,0,{event:'step.complete',action:'mount',window:2});
  rows[2].nativeSdk.views=[view(),view()];assert.equal(o.analyzeOwners(rows).capacityGate,'failed');
});
test('retired owner cannot close a different live window',()=>{
  const rows=ownerRows();rows[2].window=2;assert.equal(o.analyzeOwners(rows).terminalDrainGate,'failed');
});
test('a previously measured GUI cannot disappear into optional-not-instantiated exemption',()=>{
  const rows=ownerRows();rows[1].nativeSdk.views[0].bridge.gui_commands={observation:obs(),context_limit:'16',effect_queue_limit:'16',
    context_slots_used:'0',pending_live_contexts:'0',retained_weak_slots:'0',queued_effects:'0',effect_queue_high_water:'0',
    effect_borrows:'0',closed:false,owner_poll_active:true};
  assert.equal(o.analyzeOwners(rows).capacityGate,'not-evaluated');
});

test('F3 actual original Owned terminal time is retained in the lawful positive',()=>{
  const f=ownedFixture(),owned=v.verifyOwned(f.result,f.evidence,f.config);
  assert.equal(owned.elapsedMs,v.processBudget(f.config));
  const clock=v.verifyRunClock([{event:'run.complete',elapsedMs:p.DURATION}],f.config,owned);
  assert.equal(clock.gate,'passed');assert.equal(clock.ownedTerminalElapsedMs,v.processBudget(f.config));
});
test('F3 absent and illegal elapsed fields cannot use matching natural closure as clock proof',()=>{
  for(const value of [undefined,-1,1.5,'28800000',Number.MAX_SAFE_INTEGER+1]){
    const f=ownedFixture();changeOwned(f,rows=>{if(value===undefined)delete rows.at(-1).elapsedMs;else rows.at(-1).elapsedMs=value;});
    assert.throws(()=>v.verifyOwned(f.result,f.evidence,f.config));
  }
});
test('F3 synchronized report and result with actual short Owned time fail the requested eight hours',()=>{
  const f=ownedFixture();changeOwned(f,rows=>rows.at(-1).elapsedMs=p.DURATION-1);
  assert.equal(p.hash(f.evidence.ownedRaw),f.result.outcome.ownedProcess.reportSha256);
  assert.throws(()=>v.verifyOwned(f.result,f.evidence,f.config),error=>error.failed===true);
});
test('F3 synchronized hash/result still reject longer Native clock than actual Owned observation',()=>{
  const f=ownedFixture();changeOwned(f,rows=>rows.at(-1).elapsedMs=p.DURATION);
  const owned=v.verifyOwned(f.result,f.evidence,f.config);
  assert.equal(p.hash(f.evidence.ownedRaw),f.result.outcome.ownedProcess.reportSha256);
  assert.equal(owned.elapsedMs,p.DURATION);
  assert.throws(()=>v.verifyRunClock([{event:'run.complete',elapsedMs:p.DURATION+1}],f.config,owned),
    error=>error.failed===true&&error.message==='native-clock-exceeds-actual-owned-time');
});
test('F3 ordinary native completion is also bounded by actual Owned time',()=>{
  const config=p.fixedConfig('reload');
  assert.equal(v.verifyRunClock([{event:'run.complete',elapsedMs:20}],config,{elapsedMs:20}).gate,'passed');
  assert.throws(()=>v.verifyRunClock([{event:'run.complete',elapsedMs:21}],config,{elapsedMs:20}),error=>error.failed===true);
});
test('F3 known clock failure cannot disappear behind a not-evaluated cache',()=>{
  const f=ownedFixture();changeOwned(f,rows=>rows.at(-1).elapsedMs=p.DURATION);
  const owned=v.verifyOwned(f.result,f.evidence,f.config);let clockGate='passed';
  try{v.verifyRunClock([{event:'run.complete',elapsedMs:p.DURATION+1}],f.config,owned);}catch(error){clockGate=error.failed?'failed':'not-evaluated';}
  assert.equal(v.combine([clockGate,'not-evaluated']),'failed');
});
test('F3 observed time cannot exceed the unchanged original client watchdog range',()=>{
  const f=ownedFixture();changeOwned(f,rows=>rows.at(-1).elapsedMs=f.context.timeoutMs+f.context.drainMs+15001);
  assert.throws(()=>v.verifyOwned(f.result,f.evidence,f.config),error=>error.failed===true);
});

test('F3 classifier refuses a single contradictory actual clock field',()=>{
  const f=captureFixture('mixed');f.evidence.analysis.record.actualClock.ownedTerminalElapsedMs=1;
  assert.equal(captured(f).passed,false);assert.equal(captured(f).longRunGate,'not-evaluated');
});
test('F3 classifier cannot omit actual clock while retaining passed operation/Owned gates',()=>{
  const f=captureFixture();delete f.evidence.analysis.record.actualClock;
  assert.equal(captured(f).passed,false);
});
test('F3 clock contradiction remains failed through analyzer and classifier before missing cache evidence',()=>{
  const f=ownedFixture();changeOwned(f,rows=>rows.at(-1).elapsedMs=p.DURATION);
  const config=f.config,runRoot=path.dirname(f.evidence.eventLogPath),runId=path.basename(runRoot).slice('mixed-'.length);
  const policyRaw=p.policyBytes('mixed'),selection=p.policySelection(config);
  const request={schema:'soui.acceptance-request/v1',runId,...config,...selection.requestFields,
    consumerBuildId:'a'.repeat(64),sourceSha256:'b'.repeat(64),payloadSha256:'c'.repeat(64),startedAt:'2026-10-13T00:00:00.000Z'};
  const eventRaw=Buffer.from(JSON.stringify({event:'run.begin',pid:42,creationFileTime:f.result.outcome.ownedProcess.creationFileTime,...config,elapsedMs:0})+'\n'+
    JSON.stringify({event:'run.complete',elapsedMs:p.DURATION+1})+'\n');
  const operation={...f.result,schema:'soui.acceptance-run/v1',runId,config,passed:true,releaseReady:false,
    consumerBuildId:request.consumerBuildId,sourceSha256:request.sourceSha256,payloadSha256:request.payloadSha256,logSha256:p.hash(eventRaw)};
  const analysis=v.analyzeLong(operation,eventRaw,{...f.evidence,runRoot,policyRaw,requestRaw:encode(request),currentInputsMatched:true,
    sourceSha256:request.sourceSha256,payloadSha256:request.payloadSha256});
  assert.equal(analysis.longRunGate,'failed');assert.equal(analysis.gates.operations,'failed');assert.equal(analysis.gates.cacheBudget,'not-evaluated');
  assert.equal(analysis.logSha256,p.hash(eventRaw));
  const fixture=captureFixture('mixed');fixture.operation=operation;fixture.policyPin.sha256=p.hash(policyRaw);fixture.evidence.analysis.record=analysis;
  assert.equal(captured(fixture).longRunGate,'failed');assert.equal(captured(fixture).passed,false);
});
