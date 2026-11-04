'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {analyzeResourceWindow,CAPABILITY}=require('../../benchmarks/protocol/manual-observation-resources.cjs');
const copy=value=>structuredClone(value);
const expected={run_id:'a'.repeat(32),targetPid:42,targetCreationFileTime:'134000000000000001',hwnd:'123',qpcFrequency:'1000',
  phaseBeginQpc:'1300',phaseEndQpc:'2100',samplerCollector:{pid:99,creationFileTime:'134000000000000002'}};
function recalculate(rows){
  const samples=rows.slice(1,-1);let previous;
  for(const sample of samples){
    for(const key of ['privateBytes','workingSetBytes','handles','threads'])sample[key]=sample.processes.reduce((total,member)=>total+member[key],0);
    if(!previous){Object.assign(sample,{cpuIntervalBeginQpc:null,cpuIntervalEndQpc:null,cpuDelta100ns:null,systemDelta100ns:null,cpuDeltaComplete:false,cpuPercent:null});}
    else{
      const prior=new Map(previous.processes.map(member=>[member.pid+':'+member.creationTime100ns,member]));
      const stable=sample.processes.length===prior.size&&sample.processes.every(member=>prior.has(member.pid+':'+member.creationTime100ns));
      const cpu=stable?sample.processes.reduce((sum,member)=>sum+BigInt(member.cpuTime100ns)-BigInt(prior.get(member.pid+':'+member.creationTime100ns).cpuTime100ns),0n):null;
      const system=BigInt(sample.systemTime100ns)-BigInt(previous.systemTime100ns);
      Object.assign(sample,{cpuIntervalBeginQpc:previous.captureBeginQpc,cpuIntervalEndQpc:sample.captureEndQpc,
        cpuDelta100ns:cpu===null?null:String(cpu),systemDelta100ns:String(system),cpuDeltaComplete:stable,
        cpuPercent:stable&&system?Number((100*Number(cpu)/Number(system)).toFixed(6)):null});
    }
    previous=sample;
  }
  return rows;
}
function fixture(){
  const begin={event:'run.begin',schema:'soui.process-tree-samples/v2',rootPid:expected.targetPid,
    rootCreationTime100ns:expected.targetCreationFileTime,expectedCreationTime100ns:expected.targetCreationFileTime,
    startedAtUtcFiletime100ns:'134000000000001000',intervalMs:200,durationMs:600000,clockCapability:CAPABILITY,
    run_id:expected.run_id,qpcFrequency:'1000',runBeginQpc:'900',collectorPid:99,collectorCreationFileTime:expected.samplerCollector.creationFileTime};
  const samples=Array.from({length:7},(_,index)=>({event:'sample',rootPid:42,elapsedMs:index*200,complete:true,
    completeScope:'observed-processes-at-poll',allLifetimeProcessesObserved:false,sharedBrowserAttributionVerified:false,
    enumeration:{complete:true,failure:'none',win32Error:0,closeFailed:false,closeError:0},captureBeginQpc:String(1000+index*200),captureEndQpc:String(1010+index*200),
    systemTime100ns:String(100000+index*20000),processes:[{pid:42,parentPid:7,creationTime100ns:expected.targetCreationFileTime,
      privateBytes:1000+index*10,workingSetBytes:2000+index*10,handles:20+index,threads:3,cpuTime100ns:String(1000+index*2000)}]}));
  return recalculate([begin,...samples,{event:'run.end',elapsedMs:1210,rootExitCode:0,durationExpired:false,samples:7,incompleteSamples:0,runEndQpc:'2300'}]);
}
test('resource CPU uses only complete contained adjacent counter intervals, not lifetime or edge-weighted CPU',()=>{
  const result=analyzeResourceWindow(fixture(),expected);
  assert.equal(result.cpu.available,true);assert.equal(result.cpu.intervals,3);assert.equal(result.cpu.cpuDelta100ns,'6000');
  assert.equal(result.cpu.systemDelta100ns,'60000');assert.equal(result.cpu.machinePercent,10);assert.equal(result.cpu.phaseComplete,false);
  assert.equal(result.cpu.unobservedHeadQpc,'100');assert.equal(result.cpu.unobservedTailQpc,'90');assert.equal(result.excluded.edgeIntervals,2);
  assert.equal(result.memory.polls,4);assert.equal(result.memory.privateBytes.observedMaximum,1050);assert.equal(result.memory.workingSetBytes.minimum,2020);
  assert.equal(result.memory.handles.observedMaximum,25);assert.equal(result.memory.threads.p95,3);
  assert.equal(result.allLifetimeProcessesObserved,false);assert.equal(result.sharedBrowserAttributionVerified,false);
  assert.equal(result.gpuMemoryMeasured,false);assert.equal(result.performanceVerified,false);assert.equal(result.releaseReady,false);
});
test('phase edges and a sample-only window never gain interpolated CPU',()=>{
  const result=analyzeResourceWindow(fixture(),{...expected,phaseBeginQpc:'1395',phaseEndQpc:'1415'});
  assert.equal(result.cpu.available,false);assert.equal(result.cpu.machinePercent,null);assert.equal(result.cpu.intervals,0);
  assert.equal(result.memory.polls,1);assert.equal(result.excluded.edgeIntervals,2);
  const absent=analyzeResourceWindow(fixture(),{...expected,phaseBeginQpc:'3000',phaseEndQpc:'4000'});
  assert.equal(absent.status,'unavailable');assert.equal(absent.cpu.available,false);assert.equal(absent.memory.privateBytes,null);
});
test('observed process birth/departure is separated and affected CPU intervals are excluded',()=>{
  const rows=fixture(),child={pid:77,parentPid:42,creationTime100ns:'134000000000000099',privateBytes:100,workingSetBytes:200,handles:2,threads:1};
  rows[4].processes.push({...child,cpuTime100ns:'100'});rows[5].processes.push({...child,cpuTime100ns:'200'});recalculate(rows);
  const result=analyzeResourceWindow(rows,expected);
  assert.equal(result.cpu.intervals,1);assert.equal(result.cpu.cpuDelta100ns,'2100');assert.equal(result.excluded.membershipIntervals,2);
  assert.equal(result.membershipChanges.length,2);assert.equal(result.membershipChanges[0].newlyObserved.length,1);
  assert.equal(result.membershipChanges[1].noLongerObserved.length,1);assert.equal(result.memory.polls,4);
});
test('a zero system counter interval retains raw observations but contributes no CPU percentage',()=>{
  const rows=fixture();rows[4].systemTime100ns=rows[3].systemTime100ns;recalculate(rows);
  const result=analyzeResourceWindow(rows,expected);assert.equal(result.excluded.zeroSystemDeltaIntervals,1);assert.equal(result.cpu.intervals,2);
});
for(const [name,mutate] of [
  ['target PID',rows=>{rows[0].rootPid=41;}],['target incarnation',rows=>{rows[0].rootCreationTime100ns='134000000000000009';}],
  ['run identity',rows=>{rows[0].run_id='b'.repeat(32);}],['QPF',rows=>{rows[0].qpcFrequency='1001';}],
  ['collector receipt',rows=>{rows[0].collectorCreationFileTime='134000000000000003';}],
  ['collector in app tree',rows=>{rows[3].processes.push({pid:99,parentPid:42,creationTime100ns:expected.samplerCollector.creationFileTime,privateBytes:1,workingSetBytes:1,handles:1,threads:1,cpuTime100ns:'1'});recalculate(rows);}],
  ['collector PID with false incarnation',rows=>{rows[3].processes.push({pid:99,parentPid:42,creationTime100ns:'134000000000000003',privateBytes:1,workingSetBytes:1,handles:1,threads:1,cpuTime100ns:'1'});recalculate(rows);}],
  ['partial enumeration',rows=>{rows[3].complete=false;rows[3].enumeration.failure='next';}],
  ['close failure',rows=>{rows[3].enumeration.closeFailed=true;rows[3].enumeration.closeError=6;}],
  ['reversed capture',rows=>{rows[3].captureEndQpc='1';}],['capture overlaps previous',rows=>{rows[3].captureBeginQpc='1205';}],
  ['CPU raw delta',rows=>{rows[4].cpuDelta100ns='1';}],['system raw delta',rows=>{rows[4].systemDelta100ns='1';}],
  ['raw CPU backward',rows=>{rows[4].processes[0].cpuTime100ns='1';}],['raw system backward',rows=>{rows[4].systemTime100ns='1';}],
  ['CPU percent',rows=>{rows[4].cpuPercent=99;}],['counter Number',rows=>{rows[4].cpuDelta100ns=2000;}],
  ['wrong interval',rows=>{rows[4].cpuIntervalBeginQpc='1';}],['first fabricated delta',rows=>{rows[1].cpuDelta100ns='0';}],
  ['resource total',rows=>{rows[4].privateBytes++;}],['duration expired',rows=>{rows.at(-1).durationExpired=true;}],
  ['root exit failed',rows=>{rows.at(-1).rootExitCode=5;}],['missing terminal',rows=>{rows.pop();}],
  ['missing CPU completeness',rows=>{delete rows[4].cpuDeltaComplete;}],['false lifetime closure',rows=>{rows[4].allLifetimeProcessesObserved=true;}]
])test('resource reader refuses '+name,()=>{const rows=fixture();mutate(rows);assert.throws(()=>analyzeResourceWindow(rows,expected));});
test('old v2 samples without the explicit QPC capability remain unknown and are never backfilled',()=>{
  const rows=fixture();delete rows[0].clockCapability;
  const result=analyzeResourceWindow(rows,expected);assert.equal(result.status,'unavailable');assert.equal(result.reason,'qpc-capability-unavailable');
  assert.equal(result.cpu,null);assert.equal(result.memory,null);assert.equal(result.performanceVerified,false);
  const unknown=fixture();unknown[0].clockCapability='future-clock';assert.throws(()=>analyzeResourceWindow(unknown,expected));
  assert.throws(()=>analyzeResourceWindow(fixture(),{...expected,samplerCollector:undefined}));
  assert.throws(()=>analyzeResourceWindow(fixture(),{...expected,phaseEndQpc:'1000'}));
});
test('raw resource records never certify owned collection completion',()=>{
  const result=analyzeResourceWindow(fixture(),expected);
  assert.equal(result.recordDataQualified,true);assert.equal(result.windowDataQualified,true);
  assert.equal(result.observationQualified,false);assert.equal(result.collectionCompletionVerified,false);
  assert.match(result.qualification,/flush\/close.*held external receipts/);
  const old=fixture();delete old[0].clockCapability;
  const legacy=analyzeResourceWindow(old,expected);assert.equal(legacy.recordDataQualified,false);assert.equal(legacy.windowDataQualified,false);
  assert.equal(legacy.observationQualified,false);assert.equal(legacy.collectionCompletionVerified,false);
});
