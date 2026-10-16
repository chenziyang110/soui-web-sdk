'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const reader=require('./task-store-request-quota.cjs');
const policy=require('./long-run-policy.cjs');
const hash=raw=>crypto.createHash('sha256').update(raw).digest('hex');
const cap='134217728';
// These small records exercise the parser and lifecycle only, never actual
// eight-hour duration, CPU, native application or complete cache qualification.
const {fixture}=require('./long-run-task-quota-fixture.cjs');
function evaluate(f){const raw=Buffer.from(f.records.map(row=>JSON.stringify(row)).join('\n')+'\n');f.binding.rawLogSha256=hash(raw);return reader.analyzeLongTaskStoreQuota(raw,f.binding);}
const first=f=>f.records.find(row=>row.phase==='anchor').nativeSdk.views[0].taskStoreRequestQuota;
const retired=f=>f.records.find(row=>row.retiredOwner).retiredOwner.taskStoreRequestQuota;
for(const mode of ['session','mixed'])test(mode+' evaluates every native owner including slot reuse with a distinct pool',()=>{
  const out=evaluate(fixture(mode));assert.equal(out.dataGate,'matched',out.failure);assert.equal(out.quotaGate,'passed');assert.equal(out.ownerLifetimes,mode==='session'?5:8);
  assert.equal(out.retiredOwners,out.ownerLifetimes);assert.equal(out.fullCacheBudgetGate,'not-evaluated');assert.equal(out.finalDisposalReceiptObserved,false);assert.equal(out.releaseReady,false);
});
function reject(name,change,expected='failed'){test(name,()=>{const f=fixture();change(f);const out=evaluate(f);assert.equal(out.quotaGate,expected,out.failure);assert.equal(out.releaseReady,false);});}
reject('configured bytes differ from the frozen production quota',f=>first(f).maxLiveRequestedBytes='268435456');
reject('a transient peak above the cap fails even when current usage is small',f=>first(f).chargedPeakRequestedBytes='134217729');
reject('same document cannot change pool identity',f=>{const q=f.records.filter(row=>row.phase==='anchor')[1].nativeSdk.views[0].taskStoreRequestQuota;q.poolEpoch='999';});
reject('retired pool cannot be reused by another lifetime',f=>{const rows=f.records.filter(row=>row.retiredOwner);rows[2].retiredOwner.taskStoreRequestQuota.poolEpoch=rows[0].retiredOwner.taskStoreRequestQuota.poolEpoch;});
reject('missing retired observations retain an incomplete gate',f=>{delete f.records.find(row=>row.retiredOwner).retiredOwner;},'not-evaluated');
reject('nonempty current roster cannot be removed',f=>{f.records.find(row=>row.phase==='anchor').nativeSdk.views=[];});
reject('a view cannot be observed after its owner has closed',f=>{f.records.find(row=>row.phase==='closed').nativeSdk.views=[structuredClone(f.records.find(row=>row.phase==='anchor').nativeSdk.views[0])];});
reject('unknown window slots are refused',f=>{f.records.find(row=>row.action==='mount').window=4;});
reject('native process identity must match the bound consumer',f=>{first(f).capture.pid='72';});
reject('capture replay cannot count as another observation',f=>{const q=f.records.filter(row=>row.phase==='anchor')[1].nativeSdk.views[0].taskStoreRequestQuota;q.capture.clock=structuredClone(first(f).capture.clock);});
reject('reversed cumulative counters cannot hide earlier allocation pressure',f=>{first(f).deniedRequests='1';});
reject('original native failure survives later successful samples',f=>{f.records.splice(5,0,{event:'step.failed'});});
reject('caller cannot replace the fixed long workload settings',f=>{f.binding.expectedRun.durationMs=100;});
reject('caller cannot claim a different finite default',f=>{f.binding.expectedPolicy='268435456';});
reject('missing full source binding cannot qualify a quota',f=>{f.binding.inputsMatched=false;},'not-evaluated');
reject('an unclosed owner remains incomplete',f=>{const row=f.records.findLast(row=>row.action==='close');f.records.splice(f.records.indexOf(row),1);});
reject('missing terminal marker remains incomplete',f=>{f.records.pop();},'not-evaluated');
reject('busy quota observation is incomplete rather than silently zero',f=>{const q=first(f);Object.assign(q,{state:'busy',getterStatus:'busy',maxLiveRequestedBytes:null,poolEpoch:'0',poolEpochValid:false,creditValid:false,deniedRequestsExact:false});for(const n of ['chargedLiveRequestedBytes','chargedPeakRequestedBytes','successfulLiveRequestedBytes','successfulPeakRequestedBytes','deniedRequests'])q[n]='0';},'not-evaluated');
reject('unavailable quota cannot preserve a finite policy or charged data',f=>{first(f).state='unavailable';first(f).getterStatus='unavailable';});
reject('missing private quota does not become an observed zero',f=>{delete f.records.find(row=>row.phase==='anchor').nativeSdk.views[0].taskStoreRequestQuota;},'not-evaluated');
reject('a known quota violation wins over a separate missing observation',f=>{delete f.records.find(row=>row.phase==='anchor').nativeSdk.views[0].taskStoreRequestQuota;retired(f).chargedPeakRequestedBytes='134217729';});
test('a valid original byte hash is required and parsed consistency cannot authenticate raw bytes',()=>{
  const f=fixture(),raw=Buffer.from(f.records.map(row=>JSON.stringify(row)).join('\n')+'\n');
  assert.equal(reader.analyzeLongTaskStoreQuota(raw,f.binding).quotaGate,'failed');
  f.binding.rawLogSha256=hash(raw);const actual=reader.analyzeLongTaskStoreQuota(raw,f.binding);
  assert.deepEqual(reader.recheckLongTaskStoreQuota(f.records,f.binding),actual);
});
test('long logs use a separate bound without enlarging the original reload parser',()=>{
  const f=fixture();f.records.splice(3,0,...Array.from({length:9000},()=>({event:'diagnostic'})));
  assert.equal(evaluate(f).quotaGate,'passed');const raw=Buffer.from(f.records.map(row=>JSON.stringify(row)).join('\n')+'\n');assert.throws(()=>reader.decodeRecords(raw),/quota-log-row-bound/);
});
test('duplicate fields are rejected before decoding the original bytes',()=>{
  const f=fixture(),raw=Buffer.from(f.records.map(row=>JSON.stringify(row)).join('\n').replace('"event":"run.begin"','"event":"run.begin","event":"run.begin"')+'\n');f.binding.rawLogSha256=hash(raw);
  const out=reader.analyzeLongTaskStoreQuota(raw,f.binding);assert.equal(out.quotaGate,'failed');assert.match(out.failure,/duplicate/);
});
test('mixed mode must preserve one sample per completed cycle',()=>{
  const f=fixture('mixed');f.records=f.records.filter(row=>!(row.event==='sample'&&row.phase==='cycle'&&row.cycle===1));
  assert.equal(evaluate(f).quotaGate,'failed');
});
reject('retired owner must belong to the current cycle',f=>{f.records.find(row=>row.retiredOwner).cycle=99;});
module.exports={fixture,evaluate};
