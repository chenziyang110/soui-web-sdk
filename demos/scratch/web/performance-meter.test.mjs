import test from 'node:test';
import assert from 'node:assert/strict';
import {createPerformanceMeter,summarizeFrameIntervals} from './performance-meter.js';

function harness({longTasks = false, heap = false} = {}) {
  let time=0,nextId=1;
  const frames=new Map(),timers=new Map(),listeners=new Map(),documentListeners=new Map();
  const document={visibilityState:'visible',addEventListener:(name,fn)=>documentListeners.set(name,fn),removeEventListener:name=>documentListeners.delete(name)};
  let observer;
  class Observer {
    static supportedEntryTypes=longTasks ? ['longtask'] : [];
    constructor(callback){this.callback=callback;this.records=[];observer=this;}
    observe(){}
    disconnect(){this.disconnected=true;}
    takeRecords(){const values=this.records;this.records=[];return values;}
  }
  const environment={document,performance:{now:()=>time,...(heap ? {memory:{usedJSHeapSize:12345}} : {})},PerformanceObserver:Observer,
    navigator:{userAgent:'meter-test'},innerWidth:1200,innerHeight:800,devicePixelRatio:1,
    requestAnimationFrame:fn=>{const id=nextId++;frames.set(id,fn);return id;},cancelAnimationFrame:id=>frames.delete(id),
    setTimeout:(fn,delay)=>{const id=nextId++;timers.set(id,{fn,at:time+delay});return id;},clearTimeout:id=>timers.delete(id),
    addEventListener:(name,fn)=>listeners.set(name,fn),removeEventListener:name=>listeners.delete(name)};
  return {environment,frames,timers,documentListeners,listeners,
    frame(at){time=at;const work=[...frames.values()];frames.clear();work.forEach(fn=>fn(at));},
    timer(at){time=at;for(const [id,item] of [...timers])if(item.at<=at){timers.delete(id);item.fn();}},
    hidden(){document.visibilityState='hidden';documentListeners.get('visibilitychange')?.();},
    tasks(entries){observer.callback({getEntries:()=>entries});},
    pendingTasks(entries){observer.records.push(...entries);}};
}

test('percentiles use nearest-rank intervals and leave no-data values unavailable',()=>{
  assert.deepEqual(summarizeFrameIntervals([]),{count:0,fps:null,p50Ms:null,p95Ms:null,p99Ms:null,maxMs:null});
  const stats=summarizeFrameIntervals([10,10,20,60,NaN,-1,0]);
  assert.equal(stats.count,4);assert.equal(stats.fps,40);assert.equal(stats.p50Ms,10);assert.equal(stats.p95Ms,60);assert.equal(stats.p99Ms,60);
});
test('deadline finalizes counters, true runtime inventory and supported observations',()=>{
  const h=harness({longTasks:true,heap:true});
  const runtime={targets:[{isStage:true,isOriginal:true},{isStage:false,isOriginal:true},{isStage:false,isOriginal:false}],threads:[{},{}]};
  const meter=createPerformanceMeter({environment:h.environment,getVM:()=>({runtime})});
  meter.start({sceneId:'sprites',level:'heavy',durationMs:1000,metadata:{seed:7}});
  h.frame(0);h.frame(16);h.frame(32);h.frame(92);
  h.tasks([{startTime:-100,duration:80},{startTime:40,duration:55}]);
  h.pendingTasks([{startTime:500,duration:60}]);h.timer(1000);
  const report=meter.collectReport();
  assert.equal(report.schema,'soui.scratch-performance/v1');assert.equal(report.completed,true);
  assert.equal(report.validForWindowRateComparison,true);assert.equal(report.validForComparison,false);
  assert.equal(report.censored,true);assert.equal(report.windowRafFps,4);
  assert.equal(report.intervalsObserved,3);assert.equal(report.longFrames,1);assert.equal(report.maxFrameMs,60);
  assert.deepEqual([report.targets,report.sprites,report.clones,report.threads],[3,1,1,2]);
  assert.equal(report.heapBytes,12345);assert.deepEqual(report.longTasks,{count:2,totalMs:115,maxMs:60});
  runtime.targets=[];runtime.threads=[];h.environment.performance.memory.usedJSHeapSize=99999;
  assert.equal(meter.collectReport().targets,3,'final inventory does not change when another project loads');
  assert.equal(meter.collectReport().heapBytes,12345,'final heap sample does not include later allocations');
  assert.equal(h.frames.size,0);assert.equal(h.timers.size,0);assert.equal(report.frameIntervalsMs,undefined);
});
test('visibility interruption invalidates, freezes elapsed time and cancels work',()=>{
  const h=harness();const meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.frame(0);h.frame(16);h.hidden();h.timer(2000);
  const report=meter.collectReport();assert.equal(report.status,'invalid');assert.equal(report.valid,false);assert.equal(report.validForComparison,false);
  assert.deepEqual(report.invalidReasons,['document-hidden']);assert.equal(report.elapsedMs,16);
  assert.equal(report.heapBytes,null);assert.equal(report.longTasks,null);assert.equal(report.targets,null);
  assert.equal(h.frames.size,0);assert.equal(h.timers.size,0);
});
test('bounded window is disclosed while lifetime counters retain all frames',()=>{
  const h=harness();let updates=0;
  const meter=createPerformanceMeter({environment:h.environment,sampleCapacity:600,onUpdate:()=>++updates});
  meter.start({durationMs:30000});for(let index=0;index<=700;index++)h.frame(index*16);
  const report=meter.collectReport({includeIntervals:true});assert.equal(report.sampledIntervals,600);assert.equal(report.intervalsObserved,700);
  assert.equal(report.sampleCoverage,'rolling-window');assert.equal(report.frameIntervalsMs.length,600);assert.equal(report.rafFps,62.5);
  assert.ok(updates<=13,'periodic update is limited to approximately once a second');
  assert.ok(JSON.stringify(meter.collectReport()).length<24576,'default report fits the native metrics action bound');
  meter.stop();assert.equal(meter.snapshot().recording,false);
});
test('reset and dispose stop scheduling and remove event subscriptions',()=>{
  const h=harness();const meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.frame(0);h.frame(20);meter.reset();
  assert.equal(meter.snapshot().status,'idle');assert.equal(meter.snapshot().rafFps,null);
  meter.dispose();assert.equal(h.documentListeners.size,0);assert.equal(h.listeners.size,0);
  assert.throws(()=>meter.start(),/disposed/);
});
test('30-second high refresh records retain the whole comparison window',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:30000});
  for(let frame=0;frame<=30000;frame++)h.frame(frame);
  const report=meter.collectReport({includeIntervals:true});
  assert.equal(report.completed,true);assert.equal(report.sampleCoverage,'complete');
  assert.equal(report.intervalsObserved,30000);assert.equal(report.frameIntervalsMs.length,30000);
  assert.equal(report.frameP95Ms,1);assert.equal(report.validForComparison,true);
});
test('polling during recording does not sort and final percentiles are reused',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.frame(0);h.frame(10);h.frame(30);
  const sort=Array.prototype.sort;
  Array.prototype.sort=()=>{throw Error('Unexpected recording-path sort');};
  try {
    for(let poll=0;poll<20;poll++)assert.equal(meter.snapshot().frameP95Ms,null);
  }finally{Array.prototype.sort=sort;}
  for(let frame=50;frame<=990;frame+=20)h.frame(frame);
  h.timer(1000);
  Array.prototype.sort=()=>{throw Error('Unexpected repeated final sort');};
  try {assert.equal(meter.snapshot().frameP95Ms,20);assert.equal(meter.collectReport().frameP99Ms,20);}
  finally{Array.prototype.sort=sort;}
});
test('truncated interval windows cannot pass a complete comparison gate',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment,sampleCapacity:600});
  meter.start({durationMs:1000});for(let frame=0;frame<=1000;frame++)h.frame(frame);
  const report=meter.collectReport();
  assert.equal(report.sampleCoverage,'rolling-window');assert.equal(report.valid,true);
  assert.equal(report.completed,true);assert.equal(report.validForComparison,false);
});
test('source counters are opt-in, measure deltas and release observers after recording',()=>{
  const h=harness(),listeners=new Map();
  const runtime={targets:[],threads:[],souiPerformanceCounters:null};
  const vm={runtime,on:(name,fn)=>listeners.set(name,fn),off:name=>listeners.delete(name)};
  const meter=createPerformanceMeter({environment:h.environment,getVM:()=>vm});
  meter.start({durationMs:1000});
  runtime.souiPerformanceCounters.steps=60;runtime.souiPerformanceCounters.draws=59;
  listeners.get('targetsUpdate')();listeners.get('targetsUpdate')();
  h.frame(0);h.frame(16);h.timer(1000);
  assert.deepEqual(meter.collectReport().workload,{vmSteps:60,drawCalls:59,targetsUpdates:2});
  assert.equal(runtime.souiPerformanceCounters,null);assert.equal(listeners.size,0);
  assert.deepEqual(meter.snapshot().workload,{vmSteps:60,drawCalls:59,targetsUpdates:2});
  const external={steps:5,draws:7};runtime.souiPerformanceCounters=external;
  meter.start({durationMs:1000});external.steps=20;external.draws=19;meter.stop();
  assert.deepEqual(meter.collectReport().workload,{vmSteps:15,drawCalls:12,targetsUpdates:0});
  assert.equal(runtime.souiPerformanceCounters,external,'Another observer retains its counter ownership');
});
test('a long unclosed tail stays a poor whole-window sample, never a good short cadence score',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.frame(0);h.frame(16);h.frame(32);h.timer(1000);
  const report=meter.collectReport();
  assert.equal(report.rafFps,62.5,'historical cadence remains explicitly diagnostic');
  assert.equal(report.windowRafFps,3);assert.equal(report.rafCallbacks,3);
  assert.equal(report.trailingGapMs,968);assert.equal(report.censored,true);
  assert.equal(report.valid,true);assert.equal(report.validForWindowRateComparison,true);
  assert.equal(report.validForComparison,false);assert.equal(report.frameP95Ms,null);
  assert.equal(report.observedFramePercentiles.p95Ms,16);
});
test('a visible completed zero-frame recording scores zero and is retained',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.timer(1000);
  const report=meter.collectReport();
  assert.equal(report.windowRafFps,0);assert.equal(report.rafFps,null);assert.equal(report.rafCallbacks,0);
  assert.equal(report.leadingGapMs,1000);assert.equal(report.trailingGapMs,1000);
  assert.equal(report.censored,true);assert.equal(report.validForComparison,false);
  assert.equal(report.validForWindowRateComparison,true);assert.equal(report.valid,true);
});
test('late first frame exposes the leading censor instead of hiding startup stall',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});for(let frame=500;frame<=996;frame+=16)h.frame(frame);h.timer(1000);
  const report=meter.collectReport();
  assert.equal(report.leadingGapMs,500);assert.equal(report.trailingGapMs,4);
  assert.equal(report.windowRafFps,32);assert.equal(report.censored,true);assert.equal(report.frameP95Ms,null);
  assert.equal(report.validForWindowRateComparison,true);
});
test('continuous coverage keeps final observed percentiles with explicit bounded head/tail gaps',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});for(let frame=16;frame<=992;frame+=16)h.frame(frame);h.timer(1000);
  const report=meter.collectReport();
  assert.equal(report.leadingGapMs,16);assert.equal(report.trailingGapMs,8);assert.equal(report.windowRafFps,62);
  assert.equal(report.censored,false);assert.equal(report.validForComparison,true);assert.equal(report.frameP95Ms,16);
});
test('finish observes hidden state even when its visibility event has not yet been dispatched',()=>{
  const h=harness(),meter=createPerformanceMeter({environment:h.environment});
  meter.start({durationMs:1000});h.frame(0);h.environment.document.visibilityState='hidden';h.timer(1000);
  assert.equal(meter.collectReport().validForWindowRateComparison,false);
  assert.deepEqual(meter.collectReport().invalidReasons,['document-hidden']);
});
test('overlapping observers retain shared source counters until the last owner stops',()=>{
  const h=harness(),runtime={targets:[],threads:[],souiPerformanceCounters:null};
  const first=createPerformanceMeter({environment:h.environment,getVM:()=>({runtime})});
  const second=createPerformanceMeter({environment:h.environment,getVM:()=>({runtime})});
  first.start({durationMs:1000});runtime.souiPerformanceCounters.steps=5;
  second.start({durationMs:1000});runtime.souiPerformanceCounters.steps=8;
  first.stop();assert.notEqual(runtime.souiPerformanceCounters,null);
  runtime.souiPerformanceCounters.steps=15;second.stop();
  assert.equal(first.collectReport().workload.vmSteps,8);assert.equal(second.collectReport().workload.vmSteps,10);
  assert.equal(runtime.souiPerformanceCounters,null);
});
