'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {settings,validateRecords}=require('./runner.cjs');
const config=settings('reload',1,0,500,20000);
const outcome={exitCode:0,signal:null,timedOut:false,pid:125};
const sample=(phase,cycle)=>({event:'sample',phase,cycle,privateBytes:20000000,workingSetBytes:10000000,handles:100,threads:3,gdiObjects:2,userObjects:3});
const pair=(action,window,cycle)=>['step.begin','step.complete'].map(event=>({event,action,window,cycle}));
function transcript() {
  return [{event:'run.begin',...config,pid:125},sample('process',0),...pair('mount',1,0),...pair('runtime.reject-busy',0,0),sample('mounted',0),
    ...pair('reload',1,1),sample('cycle',1),...pair('close',1,2),sample('closed',2),
    ...pair('runtime.shutdown',0,2),sample('runtime-closed',2),...pair('runtime.reject-restart',0,2),
    ...pair('runtime.shutdown-idempotent',0,2),{event:'run.complete'}]
    .map((record,index)=>({...record,elapsedMs:index*100}));
}
test('complete native reload transcript proves steps but does not claim a resource/production gate',()=>{
  const result=validateRecords(transcript(),config,outcome);
  assert.equal(result.completedSteps,7);
  assert.equal(result.measuredCycles,1);
  assert.equal(result.resourceGate,'not-evaluated');
  assert.equal(result.releaseReady,false);
});
test('a green native log cannot hide a process crash, kill or supervision timeout',()=>{
  for(const change of [{exitCode:-1073741819},{timedOut:true},{signal:'SIGTERM'},{pid:126},{pid:null}])
    assert.throws(()=>validateRecords(transcript(),config,{...outcome,...change}));
});
test('early completion, duplicate success and routing to a different window fail',()=>{
  const missing=transcript().filter(record=>record.action!=='close');
  assert.throws(()=>validateRecords(missing,config,outcome),/lifecycle steps/);
  const duplicate=transcript(); duplicate.splice(4,0,{...duplicate[3]});
  assert.throws(()=>validateRecords(duplicate,config,outcome),/lifecycle steps/);
  const wrong=transcript(); wrong.find(record=>record.action==='reload').window=2;
  assert.throws(()=>validateRecords(wrong,config,outcome),/lifecycle steps/);
});
test('cycle counts, sample completeness and native failure records are mandatory',()=>{
  assert.throws(()=>validateRecords(transcript(),{...config,cycles:100},outcome),/parameters/);
  assert.throws(()=>validateRecords(transcript().filter(record=>record.phase!=='closed'),config,outcome),/sampling/);
  const invalid=transcript(); delete invalid.find(record=>record.event==='sample').threads;
  assert.throws(()=>validateRecords(invalid,config,outcome),/measurement/);
  const failed=transcript(); failed[4]={event:'step.failed',elapsedMs:400};
  assert.throws(()=>validateRecords(failed,config,outcome),/Failed or unknown/);
});
test('multiwindow requires surviving windows to reload and the closed slot to be recreated',()=>{
  const multi=settings('multiwindow',1,0,500,20000);
  const operations=[['mount',1],['runtime.reject-busy',0],['mount',2],['mount',3],['close',2],['reload',1],['reload',3],['mount',2],['close',3],['close',2],['close',1]];
  const records=[{event:'run.begin',...multi,pid:125},sample('process',0),
    ...operations.flatMap(([action,window])=>pair(action,window,1)),sample('cycle',1),sample('closed',2),
    ...pair('runtime.shutdown',0,2),sample('runtime-closed',2),...pair('runtime.reject-restart',0,2),
    ...pair('runtime.shutdown-idempotent',0,2),{event:'run.complete'}]
    .map((record,index)=>({...record,elapsedMs:index*100}));
  assert.equal(validateRecords(records,multi,outcome).completedSteps,14);
  assert.throws(()=>validateRecords(records.filter(record=>record.action!=='reload'),multi,outcome),/lifecycle steps/);
});
test('invalid scenario parameters and backwards clocks are rejected',()=>{
  assert.throws(()=>settings('unknown',1,0,500,20000));
  assert.throws(()=>settings('reload',0,0,500,20000));
  assert.throws(()=>settings('reload',1,-1,500,20000));
  assert.throws(()=>settings('reload',1,0,500,NaN));
  const backwards=transcript(); backwards[4].elapsedMs=1;
  assert.throws(()=>validateRecords(backwards,config,outcome),/timestamp/);
});

test('bridge contract mode requires its own real process, complete mount and teardown',()=>{
  const bridge=settings('bridge',1,0,0,10000);
  const records=[{event:'run.begin',...bridge,pid:125},sample('process',0),
    ...pair('mount',1,1),...pair('runtime.reject-busy',0,1),...pair('close',1,1),sample('cycle',1),sample('closed',2),
    ...pair('runtime.shutdown',0,2),sample('runtime-closed',2),...pair('runtime.reject-restart',0,2),
    ...pair('runtime.shutdown-idempotent',0,2),{event:'run.complete'}].map((x,i)=>({...x,elapsedMs:i*100}));
  assert.equal(validateRecords(records,bridge,outcome).completedSteps,6);
  assert.throws(()=>validateRecords(records,{...bridge,mode:'recreate'},outcome),/parameters/);
});

test('closed resources may only be sampled after the final window teardown completes',()=>{
  const early=transcript();
  const sampleIndex=early.findIndex(record=>record.phase==='closed');
  const closed=early.splice(sampleIndex,1)[0];
  early.splice(early.findIndex(record=>record.action==='close'),0,closed);
  const orderedClock=early.map((record,index)=>({...record,elapsedMs:index*100}));
  assert.throws(()=>validateRecords(orderedClock,config,outcome),/misplaced resource sampling/);

  const insideStep=transcript();
  const position=insideStep.findIndex(record=>record.phase==='closed');
  const between=insideStep.splice(position,1)[0];
  insideStep.splice(insideStep.findIndex(record=>record.action==='close')+1,0,between);
  assert.throws(()=>validateRecords(insideStep.map((record,index)=>({...record,elapsedMs:index*100})),config,outcome),/misplaced resource sampling/);
});
