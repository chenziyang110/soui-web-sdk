'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');

// Execute the actual injected DRIVER, not a second implementation of its
// state machine. The ordinary mount/Bridge boundaries are controlled below.
const source=fs.readFileSync(path.resolve(__dirname,'../../benchmarks/render-phase-cost/prepare-page.py'),'utf8');
const opening="DRIVER = r'''\n",start=source.indexOf(opening),end=source.indexOf("\n'''",start+opening.length);
assert(start>=0&&end>start&&source.indexOf(opening,start+1)<0,'Unique actual DRIVER literal required');
const driver=source.slice(start+opening.length,end);
const clone=value=>JSON.parse(JSON.stringify(value));

function harness(options={}) {
  let clock=0,nextTimer=0,nativeActive=false,pending=0,reportAt=null;
  const timers=new Map(),calls=[],begins=[],saved=[];
  function later(fn,delay){const id=++nextTimer;timers.set(id,{id,at:clock+delay,fn});return id;}
  const win={setTimeout:later,clearTimeout:id=>timers.delete(id),addEventListener:()=>{}};
  const sandbox={win,now:()=>clock,DURATION:30000,connected:options.connected!==false,
    identity:{backend:'miniblink',inputTrace:'scalar',engineBudgetUs:0,requestedFrameRate:60,
      privateRenderPhase:{initialized:true}},credentials:{sessionId:'fixture-session'},
    running:false,scene:'input',recording:false,starting:false,lastReport:null,
    nativeBusy:()=>nativeActive,utf8Bytes:text=>Buffer.byteLength(text,'utf8')};
  sandbox.ui={scene:{value:'input',onchange(){sandbox.running=false;sandbox.scene=this.value;}}};
  sandbox.begin=withRecording=>{
    begins.push({at:clock,withRecording});sandbox.starting=false;sandbox.running=true;
    sandbox.scene=sandbox.ui.scene.value;sandbox.recording=withRecording;
    if(!withRecording)return;
    nativeActive=true;
    later(()=>{
      sandbox.recording=false;sandbox.running=false;
      sandbox.lastReport={scenario:'canvas',window:{complete:true,requestedMs:30000,
        observedMs:options.observedMs===undefined?30000:options.observedMs,valid:false,domValid:false,
        invalidReasons:['unfocused_at_start']},canvas:{drawCalls:100,steps:1800},
        nativeMeasurement:{phase:'persisting',measurementUsable:null,
          begin:{persisted:null},end:{persisted:null}}};
      later(()=>{
        sandbox.lastReport.nativeMeasurement={phase:'complete',measurementUsable:true,
          begin:{persisted:true},end:{persisted:true}};
        if(options.mutateMeasurement)options.mutateMeasurement(sandbox.lastReport.nativeMeasurement);
        nativeActive=false;
      },options.nativeDoneDelayMs===undefined?0:options.nativeDoneDelayMs);
    },30000);
  };
  sandbox.bridge={pending:()=>pending,nativeOutstandingKnown:()=>true,
    request(method,params,credentials,done){
      calls.push({method,params:clone(params),at:clock});pending++;
      function reply(error,result){pending--;done(error,result);}
      if(method==='benchmark.report'){
        reportAt=clock;saved.push(clone(params.report));
        reply(null,{accepted:true,id:7,path:'fixture/report-7.json'});
      }else if(method==='benchmark.reportStatus'){
        const emit=()=>reply(null,{state:options.neverSaved||clock<reportAt+(options.savedDelayMs||0)?'pending':'saved',
          id:7,path:'fixture/report-7.json'});
        if(options.statusReplyDelayMs)later(emit,options.statusReplyDelayMs);else emit();
      }else if(method==='benchmark.action')reply(null,{accepted:true,action:params.action});
      else throw Error('Unexpected Bridge method: '+method);
    }};
  vm.runInNewContext(driver,sandbox,{timeout:1000,filename:'actual-prepare-page-DRIVER.js'});
  function advance(until){
    assert(until>=clock);let deliveries=0;
    for(;;){const task=[...timers.values()].filter(item=>item.at<=until).sort((a,b)=>a.at-b.at||a.id-b.id)[0];
      if(!task)break;assert(++deliveries<=3000,'Fixture timer delivery bound');
      timers.delete(task.id);clock=task.at;task.fn();}
    clock=until;
  }
  return{advance,calls,begins,saved,sandbox,state:()=>clone(win.__souiRenderPhaseDiagnostic),
    reports:()=>calls.filter(v=>v.method==='benchmark.report'),closes:()=>calls.filter(v=>v.method==='benchmark.action')};
}

test('actual driver preserves warmup and full record, waits for native and saved status before owner close',()=>{
  const h=harness({nativeDoneDelayMs:300,savedDelayMs:300});
  h.advance(10199);assert.equal(h.begins.length,1);assert.equal(h.begins[0].withRecording,false);
  h.advance(10200);assert.equal(h.begins.length,2);assert.equal(h.begins[1].withRecording,true);
  assert.equal(h.state().warmupObservedMs,10000);
  h.advance(40199);assert.equal(h.reports().length,0);assert.equal(h.closes().length,0);
  h.advance(40499);assert.equal(h.reports().length,0,'DOM end alone must not pass native persistence');
  h.advance(40500);assert.equal(h.reports().length,1);assert.equal(h.closes().length,0);
  h.advance(40799);assert.equal(h.closes().length,0,'Accepted/pending write is not saved');
  h.advance(40800);assert.equal(h.closes().length,1);assert.equal(h.closes()[0].params.action,'diagnostic-close');
  assert.equal(h.state().phase,'close-accepted');
  assert.equal(h.saved[0].window.valid,false,'Private-desktop invalidity must remain in the report');
  assert.equal(h.saved[0].nativeMeasurement.phase,'complete');
  assert.equal(h.saved[0].privateRenderPhaseDiagnostic.physicalPresentationMeasured,false);
});

test('failed native measurement never reaches report or diagnostic-close despite busy=false',()=>{
  const h=harness({mutateMeasurement:value=>{value.phase='failed';value.error='end_write_failed';}});
  h.advance(60000);assert.equal(h.state().phase,'failed');assert.equal(h.reports().length,0);assert.equal(h.closes().length,0);
});

test('complete label cannot replace the actual begin and end persistence flags',()=>{
  for(const which of ['begin','end']){
    const h=harness({mutateMeasurement:value=>{value[which].persisted=false;}});
    h.advance(60000);assert.equal(h.state().phase,'failed',which);assert.equal(h.reports().length,0,which);assert.equal(h.closes().length,0,which);
  }
});

test('unusable native endpoint stays failed even after both files were saved',()=>{
  const h=harness({mutateMeasurement:value=>{value.measurementUsable=false;value.end.usable=false;}});
  h.advance(60000);assert.equal(h.state().phase,'failed');assert.equal(h.reports().length,0);assert.equal(h.closes().length,0);
});

test('bootstrap keeps the original absolute deadline without resetting on polls',()=>{
  const h=harness({connected:false});h.advance(19999);assert.equal(h.state().phase,'bootstrap');
  h.advance(20000);assert.equal(h.state().phase,'failed');assert.match(h.state().error,/bootstrap deadline/);
  h.advance(75000);assert.equal(h.begins.length,0);assert.equal(h.calls.length,0);
});

test('saved reply arriving exactly at its deadline cannot extend acceptance or close',()=>{
  const h=harness({statusReplyDelayMs:15000});h.advance(55300);
  assert.equal(h.reports().length,1);assert.equal(h.state().phase,'failed');assert.match(h.state().error,/identity\/deadline/);
  assert.equal(h.closes().length,0);h.advance(90000);assert.equal(h.closes().length,0);
});

test('pending writer polling never renews the original save deadline',()=>{
  const h=harness({neverSaved:true});h.advance(55300);
  assert.equal(h.reports().length,1);assert.equal(h.state().phase,'failed');assert.match(h.state().error,/saved-status deadline/);
  assert.equal(h.closes().length,0);assert(h.calls.filter(v=>v.method==='benchmark.reportStatus').length<=150);
});

test('only numerical rounding of the original complete window is tolerated',()=>{
  const start=10000+2**-38,observed=(start+30000)-start;
  assert.notEqual(observed,30000,'The regression case must expose floating-point subtraction rounding');
  const valid=harness({observedMs:observed});valid.advance(50000);assert.equal(valid.state().phase,'close-accepted');
  for(const wrong of [29999,'30000',NaN,Infinity]){
    const invalid=harness({observedMs:wrong});invalid.advance(50000);
    assert.equal(invalid.state().phase,'failed',String(wrong));assert.equal(invalid.closes().length,0);
  }
});
