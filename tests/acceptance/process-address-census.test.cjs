'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {scope,fields,states,analyzeAddressSpace}=require('./process-address-census.cjs');
function fixture(){return {schemaVersion:1,scope,state:'complete',nonAtomic:true,physicalMemory:false,
  copyOnWriteReclassified:false,pointerBytes:4,processId:'9',captureThreadId:'10',queryLimit:32768,
  timeLimitMs:50,queries:5,win32Error:0,beginQpc:'100',endQpc:'150',qpcFrequency:'1000',
  minimumAddress:'4096',maximumAddress:'24575',nextAddress:'24576',coveredBytes:'20480',
  committedPrivateBytes:'4096',committedMappedBytes:'4096',committedImageBytes:'4096',
  committedTotalBytes:'12288',reservedBytes:'4096',freeBytes:'4096',
  committedPrivateRegions:1,committedMappedRegions:1,committedImageRegions:1};}
function bindings(){return {subject:{pid:9,creationFileTime:'123'},pointerBytes:4,
  nativeIdentity:{pid:'9',creationFileTime:'123',captureThreadId:'10'},
  captureClock:{valid:true,qpc:'200',frequency:'1000',captureThreadId:'10'}};}
test('complete observation at exact time limit describes scope without qualifying resources',()=>{
  const out=analyzeAddressSpace(fixture(),bindings());assert.equal(out.dataGate,'matched');
  assert.equal(out.resourceGate,'not-evaluated');assert.equal(out.cacheBudgetGate,'not-evaluated');
  assert.equal(out.qualified,false);assert.equal(out.releaseReady,false);
});
test('missing/future envelopes and external bindings never become zero or complete',()=>{
  for(const value of [null,undefined,{...fixture(),schemaVersion:2},{...fixture(),scope:'physical'}])
    assert.equal(analyzeAddressSpace(value,bindings()).dataGate,'not-evaluated');
  for(const key of Object.keys(bindings())){const input=bindings();delete input[key];
    assert.equal(analyzeAddressSpace(fixture(),input).dataGate,'not-evaluated');}
});
test('every failure state retains incomplete status and never passes',()=>{
  for(const state of states.filter(value=>value!=='complete'))
    assert.equal(analyzeAddressSpace({...fixture(),state},bindings()).dataGate,'partial');
  assert.equal(analyzeAddressSpace({...fixture(),state:'passed'},bindings()).dataGate,'invalid');
});
test('canonical uint64 data rejects truncation, signs, numbers and overflows',()=>{
  for(const field of fields)for(const value of [1,'01','-1','1e3','18446744073709551616'])
    assert.equal(analyzeAddressSpace({...fixture(),[field]:value},bindings()).dataGate,'invalid',field);
});
test('OS scope, bounds, time, count and exact range contradictions reject',()=>{
  const changes=[{nonAtomic:false},{physicalMemory:true},{copyOnWriteReclassified:true},
    {queryLimit:32769},{timeLimitMs:51},{queries:32769},{queries:0},{queries:2},{win32Error:5},
    {minimumAddress:'0'},{maximumAddress:'4095'},{maximumAddress:'4294967295'},
    {nextAddress:'24575'},{coveredBytes:'20479'},{committedTotalBytes:'12289'},
    {committedPrivateRegions:0},{committedMappedRegions:4097},{committedImageRegions:1.5},
    {beginQpc:'151'},{beginQpc:'0'},{endQpc:'151'},{endQpc:'201'},{qpcFrequency:'0'},
    {qpcFrequency:'1001'},{pointerBytes:16}];
  for(const change of changes)assert.equal(analyzeAddressSpace({...fixture(),...change},bindings()).dataGate,'invalid',JSON.stringify(change));
});
test('process lifetime, capture thread and architecture require external agreement',()=>{
  for(const change of [{processId:'8'},{processId:'4294967296'},{captureThreadId:'11'},{pointerBytes:8}])
    assert.equal(analyzeAddressSpace({...fixture(),...change},bindings()).dataGate,'invalid');
  for(const [key,value]of [['subject',{pid:9,creationFileTime:'124'}],['nativeIdentity',{pid:'9',creationFileTime:'123',captureThreadId:'11'}],
    ['captureClock',{valid:true,qpc:'200',frequency:'1000',captureThreadId:'11'}]])
    assert.equal(analyzeAddressSpace(fixture(),{...bindings(),[key]:value}).dataGate,'invalid');
});
test('unsigned DTO encoding cannot admit an impossible signed Windows performance counter',()=>{
  const outside='9223372036854775808';
  const timestamp={...fixture(),beginQpc:outside,endQpc:'9223372036854775858'};
  assert.equal(analyzeAddressSpace(timestamp,{...bindings(),captureClock:{...bindings().captureClock,qpc:'9223372036854775859'}}).dataGate,'invalid');
  assert.equal(analyzeAddressSpace({...fixture(),qpcFrequency:outside},
    {...bindings(),captureClock:{...bindings().captureClock,frequency:outside}}).dataGate,'invalid');
  assert.equal(analyzeAddressSpace(fixture(),{...bindings(),captureClock:{...bindings().captureClock,qpc:outside}}).dataGate,'invalid');
});
test('sequence validates identity/time while allowing memory to shrink',()=>{
  const previous=fixture();previous.beginQpc='50';previous.endQpc='99';
  const input={...bindings(),previous};assert.equal(analyzeAddressSpace(fixture(),input).dataGate,'matched');
  for(const change of [{processId:'8'},{qpcFrequency:'999'},{endQpc:'101'},{maximumAddress:'24576'}])
    assert.equal(analyzeAddressSpace(fixture(),{...input,previous:{...previous,...change}}).dataGate,'invalid');
  const smaller={...fixture(),committedPrivateBytes:'0',committedPrivateRegions:0,committedTotalBytes:'8192',freeBytes:'8192'};
  assert.equal(analyzeAddressSpace(smaller,input).dataGate,'matched');
});
test('series retains every sample, missing/partial/invalid observations and exact peaks',()=>{
  const {addressSpaceObservations}=require('./process-address-census.cjs'),input=bindings();
  const row={event:'sample',phase:'cycle',cycle:10,nativeSdk:{...input.nativeIdentity,
    captureClock:input.captureClock,addressSpace:fixture()}};
  const read=rows=>addressSpaceObservations(rows,input.subject,'x86');
  const complete=read([row]);assert.equal(complete.dataGate,'matched');
  assert.deepEqual(complete.peaksFromMatchedSamples,{private:'4096',mapped:'4096',image:'4096'});
  assert.equal(complete.cacheBudgetGate,'not-evaluated');assert.equal(complete.qualified,false);
  for(const missing of [{event:'sample'},{...row,nativeSdk:{...row.nativeSdk,addressSpace:{...fixture(),state:'time-budget'}}}]){
    const out=read([row,missing]);assert.equal(out.points.length,2);assert.equal(out.dataGate,'not-evaluated');
    assert.equal(out.counts.matched,1);
  }
  assert.equal(read([row,row]).dataGate,'invalid'); // Replayed completed QPC interval.
  assert.equal(read([]).dataGate,'not-evaluated');assert.equal(read([]).peaksFromMatchedSamples,null);
  assert.equal(addressSpaceObservations([row],input.subject,'arm64').dataGate,'not-evaluated');
});
