'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..'),base=path.resolve(__dirname,'../fixtures/source-regressions/before-quota'),reader=require('../acceptance/task-store-request-quota.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),clone=x=>structuredClone(x),rows=[];
function test(name,fn){fn();rows.push({name,passed:true,kind:'EXPLICIT_PURE_MODEL_OR_SOURCE_ONLY'});}
const full=fs.readFileSync(root+'/tests/acceptance-app/main.cpp','utf8'),old=fs.readFileSync(base+'/tests/acceptance-app/main.cpp.txt','utf8'),header=fs.readFileSync(root+'/tests/acceptance-app/native-owner-metrics.h','utf8'),oldHeader=fs.readFileSync(base+'/tests/acceptance-app/native-owner-metrics.h.txt','utf8');
const insertion='    soui::runtime::task_store_request_quota_snapshot request_quota;\n    auto quota_capture = begin_task_store_quota_capture(id_, handle_);\n    const auto quota_status = bridge_.read_task_store_request_quota(request_quota);\n    end_task_store_quota_capture(quota_capture, handle_);\n';
const serialized='    result["taskStoreRequestQuota"] = serialize_task_store_request_quota(request_quota, quota_status,\n        quota_capture, bridge.task_service.observation);\n';
const lf=x=>x.replace(/\r\n/g,'\n');
test('entire-acceptance-main-raw-reversal-exact',()=>{const crlf=old.includes('\r\n'),spell=x=>crlf?x.replaceAll('\n','\r\n'):x;assert.equal(full.replace(spell(insertion),'').replace(spell(serialized),''),old);});
test('all-old-storage-schema1-and-null-serialization-raw-exact',()=>{const start=header.indexOf('// Independent caller envelope'),end=header.indexOf('#undef SOUI_ACCEPTANCE_COUNT',start);assert(start>0&&end>start);const crlf=oldHeader.includes('\r\n'),eol=crlf?'\r\n':'\n';const actual=(header.slice(0,start)+header.slice(end)).replace('#include <soui/runtime/core/task/task_store_quota.h>'+eol,'').replace('#include <windows.h>'+eol,'');assert.equal(actual,oldHeader);});
test('legacy-long-owner-reader-byteexact-not-imported-or-weakened',()=>assert(fs.readFileSync(root+'/tests/acceptance/long-run-owners.cjs').equals(fs.readFileSync(base+'/tests/acceptance/long-run-owners.cjs.txt'))));
test('qpc-getter-actual-order-and-not-old-diagnostic-end',()=>{const n=full.indexOf('soui::acceptance::metrics::json native_owner_metrics('),tail=full.slice(n,full.indexOf('  static LRESULT',n));assert(tail.indexOf('begin_task_store_quota_capture')<tail.indexOf('bridge_.read_task_store_request_quota'));assert(tail.indexOf('bridge_.read_task_store_request_quota')<tail.indexOf('end_task_store_quota_capture'));assert(tail.indexOf('end_task_store_quota_capture')<tail.indexOf('serialize_task_store_request_quota'));assert(!/end_monotonic_us|new |std::thread/.test(tail));assert(tail.indexOf('bridge_.read_task_store_request_quota')<tail.indexOf('if (storage_observation)'));});
test('native-caller-real-identity-qpc-and-failed-clock-null',()=>{const frame=header.slice(header.indexOf('// Independent caller envelope'));for(const name of ['GetCurrentProcessId()','GetCurrentThreadId()','GetProcessTimes(','GetWindowThreadProcessId(','QueryPerformanceFrequency(','QueryPerformanceCounter(','"captureState"','"frequency", nullptr','"begin", nullptr','"end", nullptr'])assert(frame.includes(name));assert(!/new |std::thread|task_store\s+[a-z]|steady_clock|begin_monotonic_us|end_monotonic_us/.test(frame));});
test('existing-bridge-construction-default-and-closures-remain-original',()=>{assert(lf(full).includes('bridge_("app://acceptance-" + std::to_string(id), soui::sdk::diagnostics::mode::scalar)'));assert(lf(full).includes('last_retired_owner_ = window->native_owner_metrics(true, settings_.cache_observation);\n          window.reset();'));});

function fixture(policy=null){
  let tick=9007199254740993n,sequence=0;const frequency='10000000';
  const parent=end=>({valid:true,qpc:String(BigInt(end)+1n),frequency,captureThreadId:'42',phase:'synchronous-native-read-complete'});
  const view=retired=>{const begin=tick;tick+=100n;const end=begin+10n;++sequence;
    const q={schema:'soui.acceptance-task-store-request-quota/1',scope:'task-store-private-request-byte-quota',state:'available',getterStatus:'available',captureState:'valid',maxLiveRequestedBytes:policy,poolEpoch:'17',poolEpochValid:true,creditValid:true,chargedLiveRequestedBytes:policy==='0'?'0':'4',chargedPeakRequestedBytes:policy==='0'?'0':String(10+sequence),successfulLiveRequestedBytes:policy==='0'?'0':'4',successfulPeakRequestedBytes:policy==='0'?'0':'8',deniedRequests:'0',deniedRequestsExact:true,diagnosticsServiceEpoch:'31',diagnosticsServiceState:'ready',capture:{windowId:'1',windowHandle:'12345',pid:'71',creationFileTime:'134000000000000001',captureThreadId:'42',windowThreadId:'42',identityValid:true,clock:{source:'QueryPerformanceCounter',unit:'ticks',frequency,begin:String(begin),end:String(end),valid:true}},productionDefaultGate:'not-evaluated',fullCacheBudgetGate:'not-evaluated',calibrationPin:null};
    return {window:'1',ownerAlive:true,hostClosed:retired,lifetime:retired?'after-host-close-before-owner-destruction':'owner-alive',captureThreadId:'42',bridge:{task_service:{observation:{state:'ready',owner_epoch:'31'}}},taskStoreRequestQuota:q};};
  const sample=(phase,cycle,v)=>{const clock=parent(v?v.taskStoreRequestQuota.capture.clock.end:String(tick));if(!v)tick+=100n;return {event:'sample',phase,cycle,nativeSdk:{producer:'native-sdk-owner',pid:'71',creationFileTime:'134000000000000001',captureThreadId:'42',views:v?[v]:[],captureClock:clock}};};
  const records=[{event:'run.begin',mode:'reload',cycles:2,warmup:1,pid:71,creationFileTime:'134000000000000001'},sample('process',0,null),{event:'step.complete',action:'mount',window:1,cycle:0},sample('mounted',0,view(false))];
  for(let cycle=1;cycle<=3;cycle++)records.push({event:'step.complete',action:'reload',window:1,cycle},sample('cycle',cycle,view(false)));
  const retired=view(true);records.push({event:'step.complete',action:'close',window:1,cycle:4,retiredOwner:retired,nativeRenderer:{captureClock:parent(retired.taskStoreRequestQuota.capture.clock.end)}},sample('closed',4,null),sample('runtime-closed',4,null),{event:'run.complete'});
  const binding={schema:'soui.acceptance-task-store-quota-binding/1',inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',sourceSha256:'a'.repeat(64),payloadSha256:'b'.repeat(64),consumerBuildId:'EXPLICIT-MODEL-NOT-NATIVE',rawLogSha256:'0'.repeat(64),ownedLogSha256:'c'.repeat(64),contextSha256:'d'.repeat(64),subject:{pid:71,creationFileTime:'134000000000000001'},executablePin:{path:'C:/model/SouiAcceptance.exe',bytes:1,sha256:'e'.repeat(64)},expectedPolicy:policy,expectedRun:{mode:'reload',cycles:'2',warmup:'1'}};
  return {records,binding};
}
const encode=records=>Buffer.from(records.map(x=>JSON.stringify(x)).join('\n')+'\n','utf8');
function evaluate(f){const raw=encode(f.records);f.binding.rawLogSha256=sha(raw);return reader.analyzeTaskStoreQuota(raw,f.binding);}
function gate(name,mutate,wanted='invalid'){test(name,()=>{const f=fixture();mutate(f);const out=evaluate(f);assert.equal(out.dataGate,wanted);assert.equal(out.qualified,false);assert.equal(out.releaseReady,false);assert.equal(out.productionDefaultGate,'not-evaluated');assert.equal(out.fullCacheBudgetGate,'not-evaluated');});}
const first=f=>f.records[3].nativeSdk.views[0],next=f=>f.records[5].nativeSdk.views[0],q=f=>first(f).taskStoreRequestQuota;
test('serialized-null-unconfigured-model-still-never-budget-pass',()=>{const out=evaluate(fixture());assert.equal(out.dataGate,'matched');assert.equal(out.observedPolicy.kind,'unconfigured');assert.equal(out.counts.available,5);assert.equal(out.cycleSamplesObserved,'3');assert.equal(out.finalDisposalReceiptObserved,false);assert.equal(out.qualified,false);assert.equal(out.calibrationPin,null);});
for(const policy of ['0','100','18446744073709551615'])test('serialized-explicit-policy-'+policy,()=>{const out=evaluate(fixture(policy));assert.equal(out.dataGate,'matched');assert.equal(out.observedPolicy.maxLiveRequestedBytes,policy);assert.equal(out.productionDefaultGate,'not-evaluated');});
test('same-numeric-pool-and-diagnostic-epoch-stay-separate',()=>{const f=fixture();for(const row of f.records){const views=row.retiredOwner?[row.retiredOwner]:row.nativeSdk?.views||[];for(const v of views){v.taskStoreRequestQuota.poolEpoch='31';}}assert.equal(evaluate(f).dataGate,'matched');});
test('canonical-u64-and-qpc-above-2pow53-not-rounded',()=>{assert.equal(reader.u64('9007199254740993'),9007199254740993n);const out=evaluate(fixture());assert.equal(out.points[0].observation.beginQpc,'9007199254741093');});
test('serialized-uint64-counters-above-2pow53-preserve-original-decimals',()=>{const f=fixture();for(const row of f.records){for(const v of row.retiredOwner?[row.retiredOwner]:row.nativeSdk?.views||[]){Object.assign(v.taskStoreRequestQuota,{chargedLiveRequestedBytes:'9007199254740993',chargedPeakRequestedBytes:'9007199254741993',successfulLiveRequestedBytes:'9007199254740991',successfulPeakRequestedBytes:'9007199254741991'});}}const out=evaluate(f);assert.equal(out.dataGate,'matched');assert.equal(out.points[0].observation.chargedLiveRequestedBytes,'9007199254740993');assert.equal(out.qualified,false);});
test('valid-unicode-scalar-utf8-surrogate-pair-decodes',()=>assert.equal(reader.decodeRecords(Buffer.from('{"event":"😀"}\n'))[0].event,'😀'));
for(const bad of ['-1','01','1.0','1e3','18446744073709551616',9007199254740993,null])test('canonical-u64-reject-'+String(bad),()=>assert.throws(()=>reader.u64(bad)));
gate('missing-quota-record-is-partial',f=>delete first(f).taskStoreRequestQuota,'partial');
gate('missing-field',f=>delete q(f).creditValid);
gate('unknown-field',f=>q(f).extra=true);
gate('wrong-schema',f=>q(f).schema='soui.sdk-storage/1');
gate('getter-state-mismatch',f=>q(f).getterStatus='busy');
gate('null-cannot-borrow-finite-expected-policy',f=>f.binding.expectedPolicy='100');
gate('finite-policy-cannot-be-null',f=>q(f).maxLiveRequestedBytes='0');
gate('available-credit-invalid',f=>q(f).creditValid=false);
gate('owner-epoch-zero-invalid',f=>q(f).poolEpoch='0');
gate('owner-epoch-sentinel-never-valid',f=>q(f).poolEpoch='18446744073709551615');
gate('owner-epoch-changed-within-bridge',f=>next(f).taskStoreRequestQuota.poolEpoch='18');
gate('diagnostics-service-owner-changed',f=>{next(f).taskStoreRequestQuota.diagnosticsServiceEpoch='32';next(f).bridge.task_service.observation.owner_epoch='32';});
gate('diagnostics-epoch-not-borrowed-from-pool',f=>q(f).diagnosticsServiceEpoch='17');
gate('success-live-over-charged',f=>q(f).successfulLiveRequestedBytes='5');
gate('live-over-peak',f=>q(f).chargedLiveRequestedBytes='20');
gate('successful-peak-over-charged-peak',f=>q(f).successfulPeakRequestedBytes='99');
gate('available-denial-inexact',f=>q(f).deniedRequestsExact=false);
gate('saturated-denial-invalid-not-later-green',f=>{Object.assign(q(f),{state:'invalid',getterStatus:'invalid',deniedRequests:'18446744073709551615',deniedRequestsExact:false});});
gate('denial-reversed',f=>q(f).deniedRequests='1');
gate('charged-peak-reversed',f=>q(f).chargedPeakRequestedBytes='100');
gate('window-id-wrong',f=>q(f).capture.windowId='2');
gate('window-handle-changed',f=>next(f).taskStoreRequestQuota.capture.windowHandle='12346');
gate('native-pid-wrong',f=>q(f).capture.pid='72');
gate('native-creation-time-wrong',f=>q(f).capture.creationFileTime='134000000000000002');
gate('thread-is-not-native-window-owner',f=>q(f).capture.windowThreadId='43');
gate('native-identity-api-failed',f=>q(f).capture.identityValid=false);
gate('native-capture-invalid-has-no-zero-clock-success',f=>{q(f).captureState='invalid';Object.assign(q(f).capture.clock,{valid:false,begin:null,end:null,frequency:null});});
gate('clock-invalid',f=>q(f).capture.clock.valid=false);
gate('clock-end-before-begin',f=>q(f).capture.clock.end='1');
gate('clock-unit-wrong',f=>q(f).capture.clock.unit='microseconds');
gate('clock-source-wrong',f=>q(f).capture.clock.source='diagnostics.end');
gate('parent-clock-predates-real-quota-read',f=>f.records[3].nativeSdk.captureClock.qpc=q(f).capture.clock.begin);
gate('frequency-differs',f=>q(f).capture.clock.frequency='123');
gate('capture-replay-not-fresh',f=>next(f).taskStoreRequestQuota.capture.clock=clone(q(f).capture.clock));
gate('parent-thread-wrong',f=>f.records[3].nativeSdk.captureClock.captureThreadId='43');
gate('live-owner-wrong-lifetime',f=>first(f).ownerAlive=false);
gate('missing-one-cycle-sample-is-partial',f=>f.records.splice(5,1),'partial');
gate('wrong-reload-order',f=>f.records[4].cycle=2);
gate('duplicated-live-view',f=>f.records[3].nativeSdk.views.push(clone(first(f))));
gate('retired-copy-replayed-after-destruction',f=>f.records.push(clone(f.records[10])));
gate('native-failure-before-complete-stays-invalid',f=>f.records.splice(11,0,{event:'run.failed',message:'model explicit failure'}));
gate('cleanup-failure-after-complete-stays-invalid',f=>f.records.push({event:'cleanup.failed',code:'MODEL_FAILURE'}));
gate('early-complete-not-finite-reload-evidence',f=>f.records[4]={event:'run.complete'});
gate('fake-production-budget-pass',f=>q(f).productionDefaultGate='passed');
gate('fake-calibration',f=>q(f).calibrationPin={sha256:'e'.repeat(64)});
gate('binding-source-sha-malformed',f=>f.binding.sourceSha256='bad');
gate('binding-extra-field',f=>f.binding.actorAccepted=true);
gate('complete-physical-inputs-mismatch-is-invalid',f=>f.binding.inputsMatched=false);
gate('wrong-expected-run-mode',f=>f.binding.expectedRun.mode='bridge');
gate('wrong-bind-pid',f=>f.binding.subject.pid=72);
for(const state of ['busy','unavailable'])gate('cleared-'+state+'-cannot-be-null-configured-green',f=>{Object.assign(q(f),{state,getterStatus:state,poolEpoch:'0',poolEpochValid:false,creditValid:false,chargedLiveRequestedBytes:'0',chargedPeakRequestedBytes:'0',successfulLiveRequestedBytes:'0',successfulPeakRequestedBytes:'0',deniedRequests:'0',deniedRequestsExact:false});},'partial');
gate('busy-leaked-counter-invalid',f=>Object.assign(q(f),{state:'busy',getterStatus:'busy'}));
test('partial-busy-cannot-hide-later-definite-failure',()=>{const f=fixture();Object.assign(q(f),{state:'busy',getterStatus:'busy',poolEpoch:'0',poolEpochValid:false,creditValid:false,chargedLiveRequestedBytes:'0',chargedPeakRequestedBytes:'0',successfulLiveRequestedBytes:'0',successfulPeakRequestedBytes:'0',deniedRequests:'0',deniedRequestsExact:false});next(f).taskStoreRequestQuota.creditValid=false;const out=evaluate(f);assert.equal(out.dataGate,'invalid');assert.equal(out.failure,'quota-available-invalid');});
test('old-diagnostics-disabled-does-not-disable-independent-quota',()=>{const f=fixture();for(const row of f.records){for(const v of row.retiredOwner?[row.retiredOwner]:row.nativeSdk?.views||[]){v.bridge.task_service.observation={state:'disabled',owner_epoch:'0'};v.taskStoreRequestQuota.diagnosticsServiceState='disabled';v.taskStoreRequestQuota.diagnosticsServiceEpoch='0';}}assert.equal(evaluate(f).dataGate,'matched');});
for(const [name,change]of [['duplicate-field',raw=>Buffer.from(raw.toString().replace('"creditValid":true','"creditValid":false,"creditValid":true'))],['missing-final-lf',raw=>raw.subarray(0,raw.length-1)],['bom',raw=>Buffer.concat([Buffer.from([0xef,0xbb,0xbf]),raw])],['invalid-utf8',raw=>Buffer.concat([raw.subarray(0,8),Buffer.from([0xff]),raw.subarray(8)])],['crlf-not-native-binary-lf',raw=>Buffer.from(raw.toString().replaceAll('\n','\r\n'))],['too-large',()=>Buffer.alloc(32*1024*1024+1,32)],['too-many-rows',()=>Buffer.from('{}\n'.repeat(8193))],['depth-bound',()=>Buffer.from('['.repeat(65)+'0'+']'.repeat(65)+'\n')],['unpaired-surrogate',raw=>Buffer.from(raw.toString().replace('EXPLICIT-ABSENT','unused').replace('"run.begin"','"\\ud800"'))]])test('raw-'+name,()=>{const f=fixture(),raw=change(encode(f.records));f.binding.rawLogSha256=sha(raw);const out=reader.analyzeTaskStoreQuota(raw,f.binding);assert.equal(out.dataGate,'invalid');assert.equal(out.qualified,false);});
test('raw-original-sha-must-match-not-decoded-projection',()=>{const f=fixture(),raw=encode(f.records);assert.equal(reader.analyzeTaskStoreQuota(raw,f.binding).failure,'quota-raw-log-sha-differs');});
for(const thrown of [undefined,null,'',Object.create(null)])test('binding-getter-throw-never-false-green-'+String(thrown===null?'null':typeof thrown),()=>{const f=fixture(),raw=encode(f.records);f.binding.rawLogSha256=sha(raw);Object.defineProperty(f.binding,'expectedPolicy',{enumerable:true,get(){throw thrown;}});const out=reader.analyzeTaskStoreQuota(raw,f.binding);assert.equal(out.dataGate,'invalid');assert.equal(out.qualified,false);});

// Explicit serialized regressions for the independent review's two real reader gaps.
const oldReader=require('../fixtures/source-regressions/quota-reader-v1.cjs');
function retime(records){let tick=9007199254740993n;for(const row of records){for(const v of row.retiredOwner?[row.retiredOwner]:row.nativeSdk?.views||[]){const c=v.taskStoreRequestQuota.capture.clock;c.begin=String(tick+=10n);c.end=String(tick+=10n);}if(row.nativeSdk?.captureClock)row.nativeSdk.captureClock.qpc=String(tick+=10n);if(row.nativeRenderer?.captureClock)row.nativeRenderer.captureClock.qpc=String(tick+=10n);}}
function extraLifetime(){const f=fixture(),mount=clone(f.records[2]),close=clone(f.records[10]),v=close.retiredOwner;v.taskStoreRequestQuota.poolEpoch='16';v.taskStoreRequestQuota.diagnosticsServiceEpoch='30';v.bridge.task_service.observation.owner_epoch='30';f.records.splice(2,0,mount,close);retime(f.records);return f;}
function swapClosePhases(){const f=fixture(),a=f.records.find(r=>r.phase==='closed'),b=f.records.find(r=>r.phase==='runtime-closed');a.phase='runtime-closed';b.phase='closed';return f;}
for(const [name,make]of [['extra-owner-before-known-single-lifetime',extraLifetime],['runtime-closed-before-closed',swapClosePhases]])test('peer-regression-'+name,()=>{const f=make(),raw=encode(f.records);f.binding.rawLogSha256=sha(raw);assert.equal(oldReader.analyzeTaskStoreQuota(raw,f.binding).dataGate,'matched');assert.equal(reader.analyzeTaskStoreQuota(raw,f.binding).dataGate,'invalid');});
gate('second-mount-after-real-close-is-invalid',f=>f.records.splice(11,0,clone(f.records[2])));
gate('premature-close-before-declared-reloads-is-invalid',f=>f.records.splice(4,0,clone(f.records[10])));
gate('mounted-sample-before-process-is-invalid',f=>{const i=f.records.findIndex(r=>r.phase==='process');f.records[i].phase='mounted';});
gate('cycle-sample-after-closed-is-invalid',f=>{const a=f.records.find(r=>r.phase==='runtime-closed');a.phase='cycle';a.cycle=3;});
test('one-missing-cycle-remains-partial-not-complete',()=>{const f=fixture();f.records.splice(5,1);assert.equal(evaluate(f).dataGate,'partial');});


const secondReader=require('../fixtures/source-regressions/quota-reader-v2.cjs');
function wrongTerminalCycle(){const f=fixture();for(const row of f.records)if((row.event==='step.complete'&&row.action==='close')||row.phase==='closed'||row.phase==='runtime-closed')row.cycle=0;return f;}
test('peer-residual-wrong-terminal-cycle-old-readers-match-new-invalid',()=>{const f=wrongTerminalCycle(),raw=encode(f.records);f.binding.rawLogSha256=sha(raw);assert.equal(oldReader.analyzeTaskStoreQuota(raw,f.binding).dataGate,'matched');assert.equal(secondReader.analyzeTaskStoreQuota(raw,f.binding).dataGate,'matched');assert.equal(reader.analyzeTaskStoreQuota(raw,f.binding).dataGate,'invalid');});
for(const phase of ['process','mounted','closed','runtime-closed'])gate('exact-known-cycle-for-'+phase,f=>{const row=f.records.find(r=>r.phase===phase);row.cycle=phase==='process'||phase==='mounted'?1:0;});
gate('mount-cycle-must-be-zero',f=>f.records[2].cycle=1);
gate('close-cycle-must-be-declared-total-plus-one',f=>f.records[10].cycle=0);
gate('terminal-cycle-unsafe-number-is-invalid',f=>f.records.find(r=>r.phase==='closed').cycle=9007199254740993);
gate('terminal-cycle-string-is-invalid',f=>f.records.find(r=>r.phase==='closed').cycle='4');

const report={schema:'soui.task-store-quota-acceptance-source-pure/1',sourceOnly:true,sourceAuthor:'/root',passed:rows.length,failed:0,tests:rows,allFixturesExplicitModels:true,modelsAreNativeEvidence:false,cppCompiled:false,cppExecuted:false,actualNative:null,actualSdk:null,actualAcceptance:null,actual100Reload:null,calibrationPin:null,productionDefaultQuota:null,productionDefaultGate:'not-evaluated',fullCacheBudgetGate:'not-evaluated',qualificationPassed:false,releaseReady:false};
fs.writeFileSync(process.argv[2]||root+'/PURE.json',JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({sourceOnly:true,passed:rows.length,failed:0,modelsAreNativeEvidence:false,actualActors:0,releaseReady:false}));
