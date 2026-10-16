'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const {POLICY,analyzeSoftwareCacheBudget:read,recheckSoftwareCacheBudget:recheck}=require('./software-cache-budget.cjs');
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const subject={pid:9,creationFileTime:'123',entireJobProcesses:1,naturalClosure:true,forcedClosure:false,nativeExitCode:0,supervisorExitCode:0};
function sample(bytes='12288',tick=100){const total=BigInt(bytes),end=1n<<31n;return {event:'sample',phase:'cycle',cycle:1,elapsedMs:tick,
 nativeSdk:{producer:'native-sdk-owner',pid:'9',creationFileTime:'123',captureThreadId:'10',captureClock:{valid:true,qpc:String(tick+60),frequency:'1000',captureThreadId:'10'},
 addressSpace:{schemaVersion:1,scope:'current-process-virtual-address-regions',state:'complete',nonAtomic:true,physicalMemory:false,
 copyOnWriteReclassified:false,pointerBytes:4,processId:'9',captureThreadId:'10',queryLimit:32768,timeLimitMs:50,queries:5,win32Error:0,
 beginQpc:String(tick),endQpc:String(tick+50),qpcFrequency:'1000',minimumAddress:'4096',maximumAddress:String(end-1n),nextAddress:String(end),coveredBytes:String(end-4096n),
 committedPrivateBytes:String(total-8192n),committedMappedBytes:'4096',committedImageBytes:'4096',committedTotalBytes:bytes,
 reservedBytes:'4096',freeBytes:String(end-8192n-total),committedPrivateRegions:1,committedMappedRegions:1,committedImageRegions:1}}};}
function fixture(rows=[sample()]){
 const raw=Buffer.from(rows.map(x=>JSON.stringify(x)).join('\n')+'\n');
 const binding={schema:'soui.software-cache-budget-binding/v1',inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',
 sourceSha256:'a'.repeat(64),payloadSha256:'b'.repeat(64),consumerBuildId:'SYNTHETIC-READER-TEST',rawLogSha256:hash(raw),
 ownedLogSha256:'c'.repeat(64),contextSha256:'d'.repeat(64),executablePin:{path:'C:/model/SouiAcceptance.exe',bytes:1,sha256:'e'.repeat(64)},
 subject,architecture:'x86',scope:{kernel:'miniblink',renderer:'software',processModel:'single-process'},expectedPolicy:POLICY,
 expectedRun:require('./long-run-policy.cjs').fixedConfig('mixed')};return {raw,binding,rows};
}
test('all committed types at the fixed cap satisfy only this sampled budget',()=>{
 for(const bytes of ['12288','1073741824']){const f=fixture([sample(bytes)]),out=read(f.raw,f.binding);
  assert.equal(out.budgetGate,'passed');assert.equal(out.peakCommittedBytes,bytes);assert.equal(out.points.length,1);
  assert.equal(out.allocatorHardLimit,false);assert.equal(out.perCacheOwnershipObserved,false);assert.equal(out.releaseReady,false);
  assert.deepEqual(out,recheck(f.rows,f.binding));}
});
test('one byte over the cap fails even if another point is missing',()=>{
 const f=fixture([sample('1073741825'),{event:'sample'}]),out=read(f.raw,f.binding);
 assert.equal(out.budgetGate,'failed');assert.equal(out.points[0].failure,'committed-envelope-exceeded');assert.equal(out.points.length,2);
});
test('mapped and image bytes are counted without subtracting shared/code memory',()=>{
 const row=sample('1073741825'),space=row.nativeSdk.addressSpace;
 space.committedPrivateBytes='4096';space.committedMappedBytes='1073733633';space.committedImageBytes='4096';
 const f=fixture([row]);assert.equal(read(f.raw,f.binding).budgetGate,'failed');
});
test('empty, missing, partial and unsupported samples never become zero or pass',()=>{
 const partial=sample();partial.nativeSdk.addressSpace.state='time-budget';
 const future=sample();future.nativeSdk.addressSpace.schemaVersion=2;
 for(const rows of [[],[{event:'sample'}],[partial],[future],[sample(),{event:'sample'}]]){
  const f=fixture(rows);assert.equal(read(f.raw,f.binding).budgetGate,'not-evaluated');}
});
test('invalid numeric, process/thread/range data and replay fail',()=>{
 const invalid=sample();invalid.nativeSdk.addressSpace.committedTotalBytes='01';
 const wrong=sample();wrong.nativeSdk.addressSpace.processId='11';
 const zeroDuration=sample();zeroDuration.nativeSdk.addressSpace.endQpc='100';
 for(const rows of [[invalid,{event:'sample'}],[wrong],[sample(),sample()],[zeroDuration,zeroDuration]]){
  const f=fixture(rows);assert.equal(read(f.raw,f.binding).budgetGate,'failed');}
});
test('stale capture and reversed enclosing clocks fail; missing fields stay unknown',()=>{
 const stale=sample();stale.nativeSdk.captureClock.qpc='180151';
 const reverse=sample('12288',200);reverse.nativeSdk.captureClock.qpc='199';
 for(const row of [stale,reverse]){const f=fixture([row]);assert.equal(read(f.raw,f.binding).budgetGate,'failed');}
});

test('completed scans must follow the previous enclosing capture on the same owner',()=>{
 const first=sample('12288',100);first.nativeSdk.captureClock.qpc='300';
 const regressed=sample('16384',200);regressed.nativeSdk.captureClock.qpc='260';
 const overlapsPreviousCaller=sample('16384',200);overlapsPreviousCaller.nativeSdk.captureClock.qpc='350';
 for(const next of [regressed,overlapsPreviousCaller]){
  const f=fixture([first,next]);assert.equal(read(f.raw,f.binding).budgetGate,'failed');
 }
 const next=sample('16384',300),f=fixture([first,next]);
 assert.equal(read(f.raw,f.binding).budgetGate,'passed','a new scan may begin exactly at the previous caller clock');
});
test('startup, boundary and shutdown/tail points all contribute',()=>{
 const rows=[{...sample('12288',100),phase:'process'},sample('16384',200),{...sample('20480',300),phase:'runtime-closed'}];
 const f=fixture(rows),out=read(f.raw,f.binding);assert.equal(out.budgetGate,'passed');assert.equal(out.peakCommittedBytes,'20480');
 assert.deepEqual(out.points.map(p=>p.phase),['process','cycle','runtime-closed']);
});
test('public source/payload/raw and exact software-owned scope are required',()=>{
 for(const change of [{inputsMatched:false},{architecture:'arm64'},{rawLogSha256:'f'.repeat(64)},
  {scope:{kernel:'miniblink',renderer:'gpu',processModel:'single-process'}},{subject:{...subject,entireJobProcesses:2}},
  {subject:{...subject,forcedClosure:true}},{subject:{...subject,nativeExitCode:1}},
  {expectedPolicy:{...POLICY,maximumCommittedBytes:'2147483648'}},{expectedRun:{mode:'mixed',durationMs:1000}}]){
  const f=fixture(),out=read(f.raw,{...f.binding,...change});assert.notEqual(out.budgetGate,'passed',JSON.stringify(change));}
 for(const name of ['sourceSha256','payloadSha256','ownedLogSha256','contextSha256','executablePin','subject','expectedPolicy','expectedRun']){
  const f=fixture();delete f.binding[name];assert.notEqual(read(f.raw,f.binding).budgetGate,'passed',name);}
});
test('exact policy is frozen and original byte hashes reject changed/truncated input',()=>{
 assert(Object.isFrozen(POLICY));assert.equal(POLICY.maximumCommittedBytes,'1073741824');
 const f=fixture();for(const raw of [Buffer.concat([f.raw,Buffer.from('\n')]),f.raw.subarray(0,f.raw.length-1),Buffer.from('{}\n')])
  assert.equal(read(raw,f.binding).budgetGate,'failed');
});
test('long aggregate replays the complete sampled budget and rejects forged summaries',()=>{
 const long=require('./long-run-validation.cjs'),f=fixture(),budget=read(f.raw,f.binding);
 const privateBinding={...f.binding,subject:{pid:subject.pid,creationFileTime:subject.creationFileTime}};
 for(const key of ['schema','architecture','scope','expectedPolicy','expectedRun'])delete privateBinding[key];
 const gates=Object.fromEntries(long.requiredGates('mixed').map(name=>[name,'not-evaluated']));gates.cacheBudget='passed';
 const analysis={gates,allSamples:f.rows,privateStorageBinding:privateBinding,subject,architecture:'x86',
  sourceSha256:f.binding.sourceSha256,payloadSha256:f.binding.payloadSha256,consumerBuildId:f.binding.consumerBuildId,
  logSha256:f.binding.rawLogSha256,softwareCacheBudget:budget,...long.aggregate('mixed',gates)};
 assert.equal(long.checkedAggregate(analysis,'mixed').longRunGate,'not-evaluated');
 for(const mutate of [x=>x.softwareCacheBudget.peakCommittedBytes='0',x=>x.gates.cacheBudget='not-evaluated',
  x=>x.softwareCacheBudget.binding.subject.entireJobProcesses=2,x=>x.softwareCacheBudget.binding.sourceSha256='f'.repeat(64),
  x=>x.allSamples[0].nativeSdk.addressSpace.committedTotalBytes='0',x=>x.architecture='x64']){
  const changed=structuredClone(analysis);mutate(changed);assert.throws(()=>long.checkedAggregate(changed,'mixed'),/software-cache-budget/);}
});
