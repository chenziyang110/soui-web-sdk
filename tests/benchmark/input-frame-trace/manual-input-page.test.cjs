'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.resolve(__dirname,'../../../benchmarks/protocol/manual-input-page.js'),'utf8');
function harness(config={run:'a'.repeat(32),kind:'pointer_move'},nameStorage={value:''}) {
  const canvasEvents={},windowEvents={},documentEvents={},finishEvents={},queue=[],draws=[],timers=new Map(),timerDelays=new Map(),clearedTimers=[];let clock=0,nextTimer=0;
  const marker={allocations:0,uploads:0,image:null,x:null,y:null};
  const context={fillRect:(...args)=>draws.push(args),fillText:()=>{},
    createImageData:(width,height)=>{marker.allocations++;assert.equal(width,64);assert.equal(height,32);return {width,height,data:new Uint8ClampedArray(width*height*4)};},
    putImageData:(image,x,y)=>{if(marker.image)assert.equal(image,marker.image);marker.uploads++;marker.image=image;marker.x=x;marker.y=y;}};
  const canvas={getContext:()=>context,addEventListener:(name,callback)=>{canvasEvents[name]=callback;}};
  const finish={disabled:false,addEventListener:(name,callback)=>{finishEvents[name]=callback;}},status={textContent:''};
  const document={hidden:false,hasFocus:()=>false,getElementById:id=>({feedback:canvas,finish,'submission-status':status})[id],
    addEventListener:(name,callback)=>{documentEvents[name]=callback;}};
  const window={souiManualInputConfig:config,innerWidth:1100,innerHeight:720,devicePixelRatio:1,
    requestAnimationFrame:callback=>{queue.push(callback);},addEventListener:(name,callback)=>{windowEvents[name]=callback;},
    performance:{now:()=>clock},setTimeout:(callback,delay)=>{timers.set(++nextTimer,callback);timerDelays.set(nextTimer,delay);return nextTimer;},clearTimeout:id=>{clearedTimers.push(id);timers.delete(id);}};
  Object.defineProperty(window,'name',{get:()=>{if(nameStorage.readError)throw Error('unavailable');return nameStorage.value;},
    set:value=>{if(nameStorage.writeError)throw Error('unavailable');if(!nameStorage.ignoreWrite)nameStorage.value=value;}});
  const sandbox={window,document};vm.runInNewContext(source,sandbox,{timeout:1000});
  return {window,document,canvasEvents,windowEvents,documentEvents,queue,draws,finish,status,timers,timerDelays,clearedTimers,marker,click:()=>finishEvents.click(),advance:value=>{clock=value;},
    snapshot:()=>JSON.parse(JSON.stringify(window.souiManualInputResult())),
    render:()=>{assert.ok(queue.length>0);queue.shift()();}};
}
test('initial draw becomes idle and arbitrary pointer direction/position coalesces only rendering',()=>{
  const h=harness();assert.equal(h.queue.length,1);h.render();assert.equal(h.queue.length,0);
  for(const [clientX,clientY]of [[0,0],[1099,719],[100,100],[3,1]])h.canvasEvents.mousemove({clientX,clientY});
  assert.equal(h.queue.length,1);let state=h.snapshot();assert.equal(state.counts.pointer,4);
  assert.deepEqual(state.position,{x:3,y:1});assert.equal(state.counts.invalid,0);
  h.render();assert.equal(h.queue.length,0);state=h.snapshot();assert.equal(state.counts.renders,2);
  assert.deepEqual(h.draws.filter(args=>args[2]===1100&&args[3]===720).length,2);
  assert.equal(state.deterministic_replay,false);assert.equal(state.native_input_ids_available,false);
  assert.equal(state.workload_comparable,'unknown');assert.equal(state.physical_latency_measured,false);
});

function readMarker(h) {
  const {decodeMarkerHex}=require('../../../benchmarks/protocol/manual-frame-observation.cjs');
  assert.equal(h.marker.x,8);assert.equal(h.marker.y,336);
  const bytes=Buffer.alloc(16),pixels=h.marker.image.data;
  for(let bit=0;bit<128;bit++){
    const offset=(((bit>>>4)*4+2)*64+(bit%16)*4+2)*4,value=pixels[offset];
    assert.ok(value===0||value===255);assert.equal(pixels[offset+1],value);assert.equal(pixels[offset+2],value);assert.equal(pixels[offset+3],255);
    if(value)bytes[bit>>>3]|=1<<(bit&7);
  }
  return decodeMarkerHex(bytes.toString('hex'));
}
test('manual marker binds the run and actual local feedback without adding idle work',()=>{
  const h=harness({run:'1234567890abcdef1234567800000000',kind:'pointer_move'});
  assert.equal(h.marker.allocations,1);h.render();
  let marker=readMarker(h);assert.equal(marker.nonce,1);assert.equal(marker.revision,0);assert.equal(marker.frame,1);assert.equal(marker.flags,0);
  h.canvasEvents.mousemove({clientX:25,clientY:40});h.canvasEvents.wheel({deltaX:0,deltaY:-2,deltaMode:0});
  h.render();marker=readMarker(h);assert.equal(marker.revision,2);assert.equal(marker.frame,2);
  assert.equal(h.marker.allocations,1);assert.equal(h.marker.uploads,2);assert.equal(h.queue.length,0);assert.equal(h.timers.size,0);
  h.windowEvents.blur();h.render();marker=readMarker(h);assert.equal(marker.revision,2);assert.equal(marker.frame,3);
  h.window.innerWidth=1;h.windowEvents.resize();h.render();assert.equal(readMarker(h).flags,2);
});
test('marker frame exhaustion is explicit and normal feedback does not wrap or stop',()=>{
  const h=harness();h.render();h.draws.length=0;
  for(let i=1;i<65535;i++){h.windowEvents.blur();h.render();h.draws.length=0;}
  assert.equal(readMarker(h).frame,65535);assert.equal(h.snapshot().overflow,false);
  h.canvasEvents.mousemove({clientX:700,clientY:500});h.render();
  const marker=readMarker(h);assert.equal(marker.frame,65535);assert.equal(marker.flags&1,1);
  assert.equal(marker.revision,1);assert.equal(h.snapshot().counts.renders,65536);assert.equal(h.snapshot().overflow,true);
  assert.deepEqual(h.snapshot().position,{x:700,y:500});assert.equal(h.queue.length,0);
});
test('marker 20-bit feedback capacity saturates independently of normal input counters',()=>{
  const h=harness();h.render();
  for(let i=0;i<1048575;i++)h.canvasEvents.mousemove({clientX:i%1100,clientY:i%720});
  h.render();assert.equal(readMarker(h).revision,1048575);assert.equal(h.snapshot().overflow,false);
  h.canvasEvents.wheel({deltaX:0,deltaY:1,deltaMode:0});h.render();
  const marker=readMarker(h);assert.equal(marker.revision,1048575);assert.equal(marker.flags&1,1);
  assert.equal(h.snapshot().counts.pointer,1048575);assert.equal(h.snapshot().counts.wheel,1);assert.equal(h.snapshot().overflow,true);
});
test('same-context reload increments local page epoch while another run starts its own epoch',()=>{
  const storage={value:''},config={run:'c'.repeat(32),kind:'pointer_move'};
  const first=harness(config,storage);first.render();assert.equal(readMarker(first).pageEpoch,1);
  const second=harness(config,storage);second.render();assert.equal(readMarker(second).pageEpoch,2);
  const next=harness({run:'d'.repeat(32),kind:'wheel'},storage);next.render();assert.equal(readMarker(next).pageEpoch,1);
  assert.equal(next.snapshot().overflow,false);assert.equal(next.timers.size,0);assert.equal(next.queue.length,0);
});
test('unavailable, malformed or exhausted page epoch never silently reuses one',()=>{
  const run='a'.repeat(32);
  for(const storage of [{value:'foreign-window-name'},{value:undefined},{value:'soui-manual-observation:'+run+':0'},
    {value:'soui-manual-observation:'+run+':65535'},{value:'',readError:true},{value:'',writeError:true},{value:'',ignoreWrite:true}]) {
    const h=harness({run,kind:'pointer_move'},storage);h.render();h.canvasEvents.mousemove({clientX:25,clientY:30});h.render();
    assert.equal(readMarker(h).pageEpoch,0);assert.equal(readMarker(h).flags&1,1);assert.equal(h.snapshot().overflow,true);
    assert.equal(h.snapshot().counts.pointer,1);assert.equal(h.snapshot().counts.renders,2);
  }
});
test('both wheel directions and legacy deltas update real local event state without sequence restrictions',()=>{
  const h=harness({run:'b'.repeat(32),kind:'wheel'});h.render();let prevented=0;
  for(const deltaY of [-2.5,3.25])h.canvasEvents.wheel({deltaX:1,deltaY,deltaMode:0,preventDefault:()=>{prevented++;}});
  assert.equal(h.queue.length,1);let state=h.snapshot();assert.equal(state.counts.wheel,2);
  assert.equal(state.wheel.deltaY,3.25);assert.equal(state.counts.invalid,0);assert.equal(prevented,2);
  h.render();h.canvasEvents.wheel({wheelDelta:-120});h.canvasEvents.wheel({wheelDelta:120});
  state=h.snapshot();assert.equal(state.counts.wheel,4);assert.equal(state.wheel.legacyWheelDelta,120);
  assert.equal(state.wheel.deltaY,null);assert.equal(state.wheel.deltaMode,null);assert.equal(state.counts.invalid,0);
});
test('invalid values and lifecycle observations remain explicit without invented foreground qualification',()=>{
  const h=harness();h.render();h.canvasEvents.mousemove({clientX:NaN,clientY:1});h.canvasEvents.wheel({deltaX:Infinity,deltaY:1,deltaMode:0});
  h.windowEvents.blur();h.document.hidden=true;h.documentEvents.visibilitychange();h.window.innerWidth=800;h.windowEvents.resize();
  const state=h.snapshot();assert.equal(state.counts.invalid,2);assert.equal(state.lifecycle.blur,1);
  assert.equal(state.lifecycle.hidden,1);assert.equal(state.lifecycle.resize,1);assert.equal(state.geometry_invalid,true);
  assert.equal(state.lifecycle.focus_at_start,false);assert.equal(state.foreground_validity,'unknown');
  assert.equal(h.queue.length,1);h.windowEvents.unload();const prior=h.snapshot().counts.renders;h.render();
  assert.equal(h.snapshot().counts.renders,prior);assert.equal(h.queue.length,0);
});
test('manual bootstrap requires nonzero run and a supported guidance kind',()=>{
  for(const value of [{run:'0'.repeat(32),kind:'wheel'},{run:'a'.repeat(32),kind:'key'},null])assert.throws(()=>harness(value),/Manual run\/kind required/);
});
test('finish sends one frozen snapshot after input, and acceptance never claims persistence',()=>{
  const h=harness(),sent=[];assert.equal(h.timers.size,0);h.render();
  h.canvasEvents.mousemove({clientX:20,clientY:30});assert.equal(h.snapshot().render_pending,true);
  h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);
    return JSON.stringify({requestId:packet.requestId,ok:true,result:packet.method==='bridge.bootstrap'?
      {sessionId:'session',origin:'app://input-ui-benchmark',token:'secret'}:{accepted:true,persisted:false,submission:'accepted'}});};
  h.click();h.click();assert.equal(sent.length,2);assert.equal(h.timers.size,0);assert.equal(h.finish.disabled,true);
  assert.deepEqual(Object.keys(sent[0]).sort(),['requestId','sessionId','origin','token','method','params'].sort());
  const frozen=JSON.parse(sent[1].params.snapshot_json);assert.equal(frozen.counts.pointer,1);assert.equal(frozen.render_pending,true);
  h.canvasEvents.mousemove({clientX:40,clientY:50});h.render();assert.equal(frozen.counts.pointer,1);assert.equal(frozen.counts.renders,1);
  assert.match(h.status.textContent,/已接纳，尚未落盘/);assert.ok(!sent[1].params.snapshot_json.includes('secret'));
});
test('late native acceptance and timeout keep an uncertain submission without retry',()=>{
  const h=harness(),sent=[];let reply;
  h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);
    if(packet.method==='bridge.bootstrap')return JSON.stringify({requestId:packet.requestId,ok:true,result:{sessionId:'s',origin:'app://test',token:'t'}});
    return {then:callback=>{reply=callback;}};};
  h.click();h.advance(5001);reply(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:true,persisted:false,submission:'accepted'}}));
  assert.match(h.status.textContent,/未确认接纳/);assert.equal(h.timers.size,0);h.click();assert.equal(sent.length,2);
  reply(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:true,persisted:false,submission:'accepted'}}));
  assert.ok(!h.status.textContent.includes('已接纳'));
});
test('missing or rejected bridge never fabricates acceptance or starts background retry',()=>{
  const h=harness();h.click();assert.match(h.status.textContent,/未确认接纳/);assert.equal(h.timers.size,0);
  const rejected=harness();let calls=0;rejected.window.__souiNativePostMessage=wire=>{calls++;return JSON.stringify({requestId:JSON.parse(wire).requestId,ok:false,error:{code:'E_UNAUTHORIZED'}});};
  rejected.click();rejected.click();assert.equal(calls,1);assert.equal(rejected.timers.size,0);assert.match(rejected.status.textContent,/未确认接纳/);
});
test('bootstrap time reduces the report timeout and an expired bootstrap never submits',()=>{
  const h=harness(),packets=[];
  h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);packets.push(packet);
    if(packet.method==='bridge.bootstrap'){h.advance(1200.25);return JSON.stringify({requestId:packet.requestId,ok:true,result:{sessionId:'s',origin:'app://test',token:'t'}});}
    return JSON.stringify({requestId:packet.requestId,ok:true,result:{accepted:true,persisted:false,submission:'accepted'}});};
  h.click();assert.equal(packets.length,2);assert.equal(packets[1].timeoutMs,3799);assert.equal(h.timers.size,0);
  const expired=harness(),sent=[];
  expired.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);expired.advance(5000);
    return JSON.stringify({requestId:packet.requestId,ok:true,result:{sessionId:'s',origin:'app://test',token:'t'}});};
  expired.click();assert.equal(sent.length,1);assert.match(expired.status.textContent,/未确认接纳/);assert.equal(expired.timers.size,0);
});

test('explicit native pending permits only a fresh manual capture and isolates settled callbacks from the next attempt',()=>{
  const h=harness(),sent=[],replies=[];h.render();
  h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);
    if(packet.method==='bridge.bootstrap')return JSON.stringify({requestId:packet.requestId,ok:true,result:{sessionId:'s',origin:'app://test',token:'t'}});
    return {then:(resolve,reject)=>{replies.push({resolve,reject});}};};
  h.canvasEvents.mousemove({clientX:10,clientY:20});h.click();
  assert.deepEqual(sent.map(packet=>packet.requestId),['manual:bootstrap','manual:report']);
  assert.equal(h.finish.disabled,true);assert.equal(h.timers.size,1);
  const [firstTimer,firstTimeout]=[...h.timers][0],firstSnapshot=JSON.parse(sent[1].params.snapshot_json);
  assert.equal(h.timerDelays.get(firstTimer),5000);assert.equal(firstSnapshot.counts.pointer,1);assert.equal(firstSnapshot.render_pending,true);
  h.advance(1000);replies[0].resolve(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:false,persisted:false,submission:'pending',retryable:true}}));
  assert.equal(h.finish.disabled,false);assert.equal(h.timers.size,0);assert.ok(h.clearedTimers.includes(firstTimer));
  assert.match(h.status.textContent,/观察尚未结束/);assert.equal(sent.length,2);
  h.render();h.canvasEvents.mousemove({clientX:30,clientY:40});h.canvasEvents.wheel({deltaX:0,deltaY:1,deltaMode:0});
  assert.equal(sent.length,2);assert.equal(h.timers.size,0);h.click();
  assert.deepEqual(sent.map(packet=>packet.requestId),['manual:bootstrap','manual:report','manual:2:bootstrap','manual:2:report']);
  assert.equal(h.timers.size,1);assert.equal(h.finish.disabled,true);
  const secondTimer=[...h.timers.keys()][0],secondStatus=h.status.textContent,secondSnapshot=JSON.parse(sent[3].params.snapshot_json);
  assert.notEqual(secondTimer,firstTimer);assert.equal(h.timerDelays.get(secondTimer),5000);
  assert.equal(secondSnapshot.counts.pointer,2);assert.equal(secondSnapshot.counts.wheel,1);assert.equal(secondSnapshot.render_pending,true);
  assert.equal(firstSnapshot.counts.pointer,1);assert.equal(firstSnapshot.counts.wheel,0);
  // A response/rejection or already-queued timeout from the settled attempt
  // cannot clear the new attempt's shared timer or change its status.
  h.advance(5001);replies[0].resolve(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:true,persisted:false,submission:'accepted'}}));
  replies[0].reject(new Error('late failure from the first attempt'));firstTimeout();
  assert.equal(h.status.textContent,secondStatus);assert.equal(h.finish.disabled,true);
  assert.deepEqual([...h.timers.keys()],[secondTimer]);assert.ok(!h.clearedTimers.includes(secondTimer));assert.equal(sent.length,4);
  replies[1].resolve(JSON.stringify({requestId:'manual:2:report',ok:true,result:{accepted:true,persisted:false,submission:'accepted',observationEnded:true}}));
  assert.equal(h.timers.size,0);assert.ok(h.clearedTimers.includes(secondTimer));assert.equal(h.finish.disabled,true);
  assert.match(h.status.textContent,/观察程序已结束/);assert.match(h.status.textContent,/尚未落盘/);assert.match(h.status.textContent,/是否合格以报告为准/);
  h.click();assert.equal(sent.length,4);assert.equal(h.snapshot().foreground_validity,'unknown');
});

test('unavailable preserves the native code and never reopens submission for another click',()=>{
  const h=harness(),sent=[];let late;
  h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);
    if(packet.method==='bridge.bootstrap')return JSON.stringify({requestId:packet.requestId,ok:true,result:{sessionId:'s',origin:'app://test',token:'t'}});
    return {then:resolve=>{late=resolve;}};};
  h.click();assert.equal(h.timers.size,1);
  late(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:false,persisted:false,submission:'unavailable',retryable:true,code:'E_MANUAL_OBSERVATION_UNAVAILABLE'}}));
  assert.match(h.status.textContent,/E_MANUAL_OBSERVATION_UNAVAILABLE/);assert.match(h.status.textContent,/未确认接纳/);
  assert.equal(h.finish.disabled,true);assert.equal(h.timers.size,0);const message=h.status.textContent;
  h.advance(10000);h.click();assert.equal(sent.length,2);
  late(JSON.stringify({requestId:'manual:report',ok:true,result:{accepted:true,persisted:false,submission:'accepted',observationEnded:true}}));
  assert.equal(h.status.textContent,message);assert.equal(h.timers.size,0);assert.equal(sent.length,2);
});

test('partial or contradictory pending receipts never enable an automatic or manual retry',()=>{
  for(const result of [
    {accepted:false,persisted:false,submission:'pending'},
    {accepted:false,persisted:false,submission:'pending',retryable:false},
    {accepted:false,persisted:true,submission:'pending',retryable:true},
    {accepted:false,persisted:false,submission:'unknown',retryable:true}
  ]){
    const h=harness(),sent=[];h.window.__souiNativePostMessage=wire=>{const packet=JSON.parse(wire);sent.push(packet);
      return JSON.stringify({requestId:packet.requestId,ok:true,result:packet.method==='bridge.bootstrap'?{sessionId:'s',origin:'app://test',token:'t'}:result});};
    h.click();assert.equal(sent.length,2);assert.equal(h.finish.disabled,true);assert.equal(h.timers.size,0);
    assert.match(h.status.textContent,/未确认接纳/);h.click();assert.equal(sent.length,2);
  }
});
