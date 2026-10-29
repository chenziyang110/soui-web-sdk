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
  assert.equal(report.schema,'soui.scratch-performance/v1');assert.equal(report.completed,true);assert.equal(report.validForComparison,true);
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
