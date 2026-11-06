'use strict';
// Logic contracts only. Synthetic clocks below are never FPS/performance evidence.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const page = require('../resources/benchmark.js');

function clock() {
  let now = 0, serial = 0;
  const timers = new Map();
  return {
    now: () => now,
    later(fn, delay) { const id = ++serial; timers.set(id, { at: now + delay, fn }); return id; },
    cancel(id) { timers.delete(id); },
    elapseBlocked(delta) { now += delta; },
    advance(delta) {
      const target = now + delta;
      for (let guard = 0; ; ++guard) {
        assert.ok(guard < 10000, 'test scheduler did not yield');
        const due = [...timers].filter(([, timer]) => timer.at <= target).sort((a, b) => a[1].at - b[1].at)[0];
        if (!due) break;
        now = due[1].at; timers.delete(due[0]); due[1].fn();
      }
      now = target;
    }
  };
}
function reply(wire, result) { const request = JSON.parse(wire); return JSON.stringify({ requestId: request.requestId, ok: true, result }); }
function messageChannels(time) {
  const instances=[];
  function Constructor() {
    const item={closed:0,messages:0,events:[]}; instances.push(item);
    this.port1={onmessage:null,start(){},close(){++item.closed;}};
    this.port2={close(){++item.closed;},postMessage:value=>{
      ++item.messages;const handler=this.port1.onmessage;
      const deliver=()=>{if(handler)handler({data:value});};item.events.push(deliver);time.later(deliver,0);
    }};
  }
  return {Constructor,instances};
}
function fixedHarness(options={}) {
  const time=clock(),channels=messageChannels(time),requests=[],pending=[];let result;
  const win={setTimeout:time.later,clearTimeout:time.cancel};if(!options.fallback)win.MessageChannel=channels.Constructor;
  const scheduler=page.createFixedWorkScheduler(win,time.now);
  const bridge=page.createBridge(wire=>{
    const request=JSON.parse(wire);requests.push(request);
    if(options.send)return options.send(wire,request,time,pending);
    time.elapseBlocked(.02);return reply(wire,request.params);
  },time.later,time.cancel,time.now);
  const runner=page.createFixedBridgeWork(bridge,{sessionId:'s',origin:'o',token:'t'},time.now,time.later,time.cancel,scheduler,
    {payloadBytes:options.payloadBytes===undefined?64:options.payloadBytes,checkState:options.checkState},value=>{result=value;});
  return {time,channels,requests,pending,bridge,runner,scheduler,result:()=>result};
}

test('fixed-work scheduler has one macrotask, rejects stale closed-run messages, and closes both ports',()=>{
  const time=clock(),channels=messageChannels(time),win={MessageChannel:channels.Constructor,setTimeout:time.later,clearTimeout:time.cancel};
  const first=page.createFixedWorkScheduler(win,time.now);let delivered=0;
  assert.equal(first.schedule(()=>++delivered),true);assert.equal(first.schedule(()=>++delivered),false);
  first.close();const next=page.createFixedWorkScheduler(win,time.now);next.schedule(()=>++delivered);
  channels.instances[0].events[0]();time.advance(0);assert.equal(delivered,1);next.close();next.close();
  assert.deepEqual(channels.instances.map(item=>item.closed),[2,2]);
  const fallback=page.createFixedWorkScheduler({setTimeout:time.later,clearTimeout:time.cancel},time.now);
  fallback.schedule(()=>++delivered);time.advance(3);assert.equal(delivered,1);time.advance(1);assert.equal(delivered,2);
  assert.equal(fallback.snapshot().primitive,'timer-4ms');assert.equal(fallback.snapshot().fallback,true);fallback.close();
});

test('fixed-work sends exactly 32 warm-up and 1024 measured successes for all three payloads without rAF',()=>{
  for(const bytes of [64,4096,60000]){
    const h=fixedHarness({payloadBytes:bytes});h.runner.start();assert.equal(h.requests.length,0,'start must yield');h.time.advance(1000);
    const value=h.result();assert.equal(value.valid,true);assert.equal(h.requests.length,1056);assert.equal(value.warmup.success,32);
    assert.equal(value.measured.issued,1024);assert.equal(value.measured.success,1024);assert.equal(value.waitersPendingAtEnd,0);
    assert.equal(value.rtt.count,1024);assert.ok(value.measured.peakPending<=8);assert.ok(value.successOperationsPerSecond>0);
    assert.equal(value.scheduler.primitive,'message-channel');assert.equal(value.scheduler.fallback,false);
    assert.equal(value.physicalPresentationMeasured,false);assert.equal(value.pureNativeCostMeasured,false);
    assert.deepEqual(h.requests.map(request=>request.params.sequence),Array.from({length:1056},(_,i)=>i));
    for(const request of h.requests){assert.equal(request.params.payload.length,bytes);assert.equal(request.timeoutMs,5000);}
    assert.equal(h.channels.instances[0].closed,2);
  }
});

test('fixed-work bounds eight out-of-order native round trips and retains zero-resolution RTT samples',()=>{
  const h=fixedHarness({send(wire,request,time){return {then(resolve){time.later(()=>{
    resolve(reply(wire,request.params));resolve(reply(wire,request.params));
  },request.params.sequence%2?0:2);}};}});
  h.runner.start();h.time.advance(1000);const value=h.result();assert.equal(value.valid,true);
  assert.equal(value.measured.peakPending,8);assert.equal(value.measured.success,1024);assert.equal(value.rtt.count,1024);
  assert.ok(value.rtt.samplesUs.includes(0));assert.equal(value.transportDuplicateCount,null,'suppressed transport duplicates are not observable');
});

test('fixed-work errors never retry to fill the goal; timeouts retain unknown native work and late replies cannot rewrite terminal data',()=>{
  const mismatch=fixedHarness({send(wire,request,time){time.elapseBlocked(.01);return reply(wire,
    request.params.sequence===32?{payload:'wrong',sequence:32}:request.params);}});
  mismatch.runner.start();mismatch.time.advance(1000);assert.equal(mismatch.requests.length,33);
  assert.equal(mismatch.result().valid,false);assert.equal(mismatch.result().measured.mismatches,1);assert.equal(mismatch.result().successOperationsPerSecond,null);
  const timeout=fixedHarness({send(wire,request,time,pending){if(request.params.sequence<32){time.elapseBlocked(.01);return reply(wire,request.params);}
    return {then(resolve){pending.push(()=>resolve(reply(wire,request.params)));}};}});
  timeout.runner.start();timeout.time.advance(5100);const value=timeout.result(),frozen=JSON.stringify(value);
  assert.equal(timeout.requests.length,40);assert.equal(value.measured.timeouts,8);assert.equal(value.measured.pending,0);
  assert.equal(value.measured.issued,value.measured.success+value.measured.failed+value.measured.timeouts+value.measured.mismatches+value.measured.pending);
  assert.equal(value.nativeOutstandingKnown,false);assert.equal(value.nativeWorkCancelled,false);
  timeout.pending.forEach(done=>done());assert.equal(JSON.stringify(timeout.result()),frozen);assert.equal(timeout.bridge.pending(),0);
  const oversized=fixedHarness({payloadBytes:60001});oversized.runner.start();assert.equal(oversized.requests.length,0);
  assert.ok(oversized.result().invalidReasons.includes('unsupported_payload'));
});

test('fixed-work uses one whole-run deadline across warm-up and batches, and stops/drains without issuing more work',()=>{
  const deadline=fixedHarness({send(wire,request,time){return {then(resolve){time.later(()=>resolve(reply(wire,request.params)),4000);}};}});
  deadline.runner.start();deadline.time.advance(65000);const value=deadline.result();
  assert.equal(value.valid,false);assert.ok(value.invalidReasons.includes('whole_run_deadline'));
  assert.equal(value.createdDomMs,0);assert.equal(value.measurementStartDomMs,16000);assert.ok(value.measurementEndDomMs<=65000);
  assert.ok(value.measured.success<1024);assert.equal(value.waitersPendingAtEnd,0);
  const stop=fixedHarness({send(wire,request,time,pending){return {then(resolve){pending.push(()=>resolve(reply(wire,request.params)));}};}});
  stop.runner.start();stop.time.advance(0);assert.equal(stop.requests.length,8);stop.runner.stop('manual_stop');
  stop.pending.forEach(done=>done());assert.equal(stop.result().valid,false);assert.equal(stop.result().waitersPendingAtEnd,0);
  stop.time.advance(60000);assert.equal(stop.requests.length,8);assert.equal(stop.channels.instances[0].closed,2);
  const reversed=fixedHarness({send(wire,request,time){time.elapseBlocked(-1);return reply(wire,request.params);}});
  reversed.runner.start();reversed.time.advance(0);assert.equal(reversed.result().valid,false);
  assert.ok(reversed.result().invalidReasons.includes('monotonic_clock_invalid'));
});
function nativeReceipt(phase) {
  return {id:7,phase,status:'snapshot_queued',scope:'process-cumulative-live',snapshotUsable:true,writeId:phase==='begin'?10:11,
    path:'native-'+phase+'.json',qpcFrequency:'10000000',captureEndQpc:phase==='begin'?'1001':'30000001001',
    ...(phase==='begin'?{beginNativeQpc:'1000'}:{endNativeQpc:'30000001000'})};
}

test('UTF-8 boundary counts actual wire bytes, including escaping and surrogate pairs', () => {
  for (const text of ['', 'a', '中', '\u00e9', '\ud83d\ude00', '\ud800', 'a\udfff中'])
    assert.equal(page.utf8Bytes(text), Buffer.byteLength(text, 'utf8'));
  const time = clock(); let calls = 0, error;
  const bridge = page.createBridge(wire => { ++calls; return reply(wire, {}); }, time.later, time.cancel, time.now);
  const accepted = bridge.request('benchmark.echo', {payload:'a'.repeat(60000), sequence:0},
    {sessionId:'s',origin:'o',token:'t'}, err => { error = err; });
  assert.equal(error, null); assert.equal(accepted.attempted, true); assert.ok(accepted.requestBytes <= 65536);
  bridge.request('benchmark.echo', {payload:'"'.repeat(40000)}, {}, err => { error = err; });
  assert.equal(error.code, 'E_PAGE_SIZE'); assert.equal(calls, 1); assert.equal(bridge.pending(), 0);
  assert.equal(bridge.nativeOutstandingKnown(),true,'preflight rejection cannot create unknown native work');
});

test('transport accepts synchronous string and thenable; checks correlation and settles once', () => {
  const time = clock(); let complete, callbacks = 0, result;
  const bridge = page.createBridge(wire => ({ then(resolve) { complete = () => resolve(reply(wire, {ok:1})); } }),
    time.later, time.cancel, time.now);
  bridge.request('x', {}, {}, (error, value) => { assert.equal(error, null); ++callbacks; result = value; });
  assert.equal(bridge.pending(), 1); complete(); complete(); time.advance(6000);
  assert.equal(bridge.pending(), 0); assert.equal(callbacks, 1); assert.deepEqual(result, {ok:1});
  const wrong = page.createBridge(() => '{"requestId":"other","ok":true,"result":{}}', time.later, time.cancel, time.now);
  wrong.request('x', {}, {}, error => assert.equal(error.code, 'E_PAGE_RESPONSE'));
});

test('eight outstanding slots stay occupied until completion or timeout; late replies cannot reopen them', () => {
  const time = clock(), completions = [], errors = [];
  const bridge = page.createBridge(wire => ({then(resolve) { completions.push(() => resolve(reply(wire, {}))); }}),
    time.later, time.cancel, time.now);
  for (let i = 0; i < 9; ++i) bridge.request('x', {}, {}, error => errors.push(error && error.code));
  assert.equal(completions.length, 8); assert.equal(bridge.pending(), 8); assert.deepEqual(errors, ['E_PAGE_CAPACITY']);
  completions[0](); assert.equal(bridge.pending(), 7);
  time.advance(5000); assert.equal(bridge.pending(), 0); assert.equal(errors.filter(x => x === 'E_PAGE_TIMEOUT').length, 7);
  assert.equal(bridge.nativeOutstandingKnown(),false);
  completions.forEach(fn => fn()); assert.equal(errors.length, 9); assert.equal(bridge.pending(), 0);
  assert.equal(bridge.nativeOutstandingKnown(),false,'late suppressed completions cannot rehabilitate the native history');
});

test('a blocked synchronous response arriving after the deadline is not counted as on-time success', () => {
  const time=clock(); let outcome;
  const bridge=page.createBridge(wire=>{time.elapseBlocked(5001);return reply(wire,{});},time.later,time.cancel,time.now);
  bridge.request('x',{}, {},error=>{outcome=error;});
  assert.equal(outcome.code,'E_PAGE_TIMEOUT'); assert.equal(bridge.pending(),0);
});

test('fixed simulation is independent of callback partition and retains bounded catch-up debt', () => {
  const a = page.createWorld(page.constants.seed, 240), b = page.createWorld(page.constants.seed, 240);
  const ta = page.createTicker(a), tb = page.createTicker(b);
  for (let i = 0; i < 1800; ++i) ta.advance(page.constants.stepMs);
  for (let i = 0; i < 600; ++i) tb.advance(50);
  assert.equal(a.steps, 1800); assert.deepEqual(a, b); assert.equal(page.stateHash(a), page.stateHash(b));
  assert.equal(page.stateHash(a).length, 8);
  for (const sprite of a.sprites) {
    assert.ok(sprite.x >= 0 && sprite.x <= 464 * 256 && sprite.y >= 0 && sprite.y <= 344 * 256);
  }
  const slow = page.createWorld(1, 1), ticker = page.createTicker(slow);
  assert.equal(ticker.advance(1000), 8); assert.ok(ticker.backlogMs() > 860);
  while (ticker.backlogMs() >= page.constants.stepMs - 0.000001) ticker.advance(0);
  assert.equal(slow.steps, 60); assert.ok(ticker.backlogMs() < 0.000001);
  assert.throws(() => ticker.advance(-1)); assert.throws(() => ticker.advance(Infinity));
});

test('histogram uses all intervals, strict long-frame threshold and explicit overflow rather than capped fake quantiles', () => {
  const stats = page.frameStats();
  assert.equal(stats.summary().p95MsUpperBound, null);
  for (let i = 0; i < 6000; ++i) stats.add(10);
  stats.add(50); stats.add(50.001); stats.add(2048);
  assert.equal(stats.add(NaN), false); assert.equal(stats.add(0), false);
  const data = stats.summary();
  assert.equal(data.count, 6003); assert.equal(data.above50Ms, 2); assert.equal(data.p95MsUpperBound, 10.25);
  assert.equal(data.overflowCount, 1); assert.equal(data.maximumMs, 2048);
  const overflow = page.frameStats(); overflow.add(2048); assert.equal(overflow.summary().p50MsUpperBound, null);
});

function mountedPage(options = {}) {
  const time = clock(), nodes = new Map(), listeners = {}, documentListeners = {}, reports = [], calls = [], actions=[], echoReplies=[], echoPayloadSizes=[], frameRequests=[];
  let bootstrapPending=options.bootstrapPending || 0, identityCalls=0, identityReply, writesPending=options.writesPending || 0;
  let beginReply, endReply, scalarWritesPending=options.scalarWritesPending || 0;
  let focused=options.focused!==false;
  const context = Object.fromEntries(['fillRect','save','restore','translate','rotate'].map(name => [name, () => {}]));
  function node(id) {
    if (!nodes.has(id)) nodes.set(id, {value:'',textContent:'',style:{},disabled:false,handlers:{},clientWidth:480,clientHeight:360,
      addEventListener(name, fn) { this.handlers[name] = fn; }, appendChild() {}, getContext() { return context; }});
    return nodes.get(id);
  }
  node('scene').value = 'input'; node('payload').value = '64';
  const doc = {getElementById:node,createElement:() => node('row'+nodes.size),hidden:false,visibilityState:'visible',hasFocus:() => focused,
    documentElement:{getAttribute(name){return name==='data-soui-autobench'&&options.autoMarker?'bridge4096-v1':null;}},
    addEventListener(name, fn) { documentListeners[name] = fn; }};
  const win = {performance:{now:time.now},setTimeout(fn,delay){return options.dropFlood&&delay===4?0:time.later(fn,delay);},clearTimeout:time.cancel,innerWidth:1000,innerHeight:800,devicePixelRatio:1,
    requestAnimationFrame(fn) { this.nextFrame = fn; frameRequests.push(fn); return frameRequests.length; },
    cancelAnimationFrame() {},addEventListener(name, fn) { listeners[name] = fn; },
    __souiNativePostMessage(wire) {
      const request = JSON.parse(wire); calls.push(request.method); let result;
      switch (request.method) {
        case 'bridge.bootstrap':
          assert.deepEqual(Object.keys(request).sort(),['method','origin','params','requestId','sessionId','token']);
          if(bootstrapPending-->0)return JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_CONTEXT_PENDING',message:'waiting'}});
          result = {sessionId:'session',origin:'local',token:'not-saved'}; break;
        case 'benchmark.identity':
          result = {backend:options.backend||'test-contract',sdkVersion:'test',buildIdentity:'test',pageHash:'test',identityCall:++identityCalls,
            hostIndex:0,sessionGeneration:1,inputTrace:options.inputTrace || 'off',observation:options.observation||'off',detailedTraceConfiguredByConsumer:false,
            environment:{SOUI_WEBVIEW2_PREPARE_OUTPUT:options.prepareOutput===undefined?null:options.prepareOutput,
              SOUI_WEBVIEW2_OUTPUT_TIMING:options.outputTiming===undefined?null:options.outputTiming},native:{windowVisible:true,windowIconic:false,pacerFailed:false,
              documentReady:true,applicationReady:true,pacerHz:60,pacerBackend:2,pid:123,creationFileTime:'123',clientWidth:1000,
              clientHeight:800,windowDpiX:96,windowDpiY:96,sessionGeneration:1,runtimeIdentity:{test:true}}};
          if(options.changeFixedIdentity && identityCalls>=3)result.native.pacerHz=75;
          if(options.identityErrorAt===identityCalls)return JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_TEST_IDENTITY',message:'identity failed'}});
          if(options.deferSecondIdentity && identityCalls===2)return {then(resolve){identityReply=()=>resolve(reply(wire,result));}};
          break;
        case 'bridge.probe.report': result = {ready:true}; break;
        case 'benchmark.echo':
          echoPayloadSizes.push(request.params.payload.length);
          if(options.echoCostMs)time.elapseBlocked(options.echoCostMs);
          if(options.deferEcho)return {then(resolve){echoReplies.push(()=>resolve(reply(wire,request.params)));}};
          result = request.params; break;
        case 'benchmark.report': reports.push(request.params.report); result = {accepted:true,path:'test.json',id:1}; break;
        case 'benchmark.measurementBegin':
          result=nativeReceipt('begin');
          if(options.snapshotUsable===false)result.snapshotUsable=false;
          if(options.deferMeasurementBegin)return {then(resolve){beginReply=()=>resolve(reply(wire,result));}};
          break;
        case 'benchmark.measurementEnd':
          assert.equal(request.params.id,7);result=nativeReceipt('end');
          if(options.snapshotUsable===false)result.snapshotUsable=false;
          if(options.deferMeasurementEnd)return {then(resolve){endReply=()=>resolve(reply(wire,result));}};
          break;
        case 'benchmark.reportStatus':
          if(request.params.id===options.scalarWriteFailure)result={state:'failed',error:'test writer failure'};
          else if(request.params.id===10 || request.params.id===11)result={state:scalarWritesPending-->0?'pending':'saved',id:request.params.id,path:request.params.id===10?'native-begin.json':'native-end.json'};
          else {assert.equal(request.params.id,1);result = {state:writesPending-->0?'pending':'saved',
            id:options.badSavedReceipt?2:1,path:options.badSavedReceipt?'different.json':'test.json'};}
          break;
        case 'benchmark.action': actions.push(request.params.action); result = {accepted:true,action:request.params.action}; break;
        default: throw Error(request.method);
      }
      return reply(wire,result);
    }};
  if(options.messageChannel)win.MessageChannel=messageChannels(time).Constructor;
  page.mount(win,doc);
  return {time,node,win,doc,listeners,documentListeners,reports,calls,actions,echoReplies,echoPayloadSizes,frameRequests,
    focus(){focused=true;if(listeners.focus)listeners.focus();},
    resolveIdentity(){identityReply();},resolveBegin(){beginReply();},resolveEnd(){endReply();}};
}

test('only the W02 marker enables the fixed bridge workload; ordinary page remains manual',()=>{
  const ordinary=mountedPage({inputTrace:'scalar'});
  ordinary.time.advance(30000);
  assert.equal(ordinary.node('scene').value,'input');assert.equal(ordinary.node('payload').value,'64');
  assert.equal(ordinary.calls.includes('benchmark.measurementBegin'),false);
  assert.equal(ordinary.reports.length,0);assert.deepEqual(ordinary.actions,[]);

  const auto=mountedPage({autoMarker:true,focused:false,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'1',writesPending:1});
  assert.equal(auto.node('scene').value,'bridge');assert.equal(auto.node('payload').value,'4096');
  assert.equal(auto.calls.includes('benchmark.measurementBegin'),false,'background windows must wait for real focus');
  auto.focus();assert.equal(auto.calls.filter(method=>method==='benchmark.measurementBegin').length,1);
  auto.time.advance(30000);
  assert.equal(auto.reports.length,1);assert.equal(auto.reports[0].scenario,'bridge');
  assert.equal(auto.reports[0].parameters.payloadBytes,4096);assert.equal(auto.reports[0].parameters.maxEchoOutstanding,7);
  assert.equal(auto.reports[0].window.requestedMs,30000);assert.ok(auto.reports[0].bridge.attempted>0);
  assert.ok(auto.echoPayloadSizes.length>0&&auto.echoPayloadSizes.every(bytes=>bytes===4096));
  assert.equal(auto.reports[0].window.valid,true);assert.equal(auto.reports[0].nativeMeasurement.measurementUsable,true);
  assert.deepEqual(auto.actions,[],'admission or pending writer status is not saved');
  auto.time.advance(200);
  assert.deepEqual(auto.actions,['close'],'the existing saved receipt triggers one private close action');
  assert.equal(auto.calls.filter(method=>method==='benchmark.report').length,1);
  assert.equal(auto.calls.filter(method=>method==='benchmark.action').length,1);
  const lastStatus=auto.calls.lastIndexOf('benchmark.reportStatus'),close=auto.calls.lastIndexOf('benchmark.action');
  assert.ok(lastStatus>=0&&lastStatus<close,'saved status must precede close request');
  const host=fs.readFileSync(require.resolve('../main.cpp'),'utf8');
  const claim=host.indexOf('item.bridge.dispatch().claim(next.receipt)'),closeDispatch=host.indexOf('if(next.name=="close")');
  assert.ok(claim>=0&&closeDispatch>claim,'close must pass the existing original-receipt claim');
  assert.match(host,/name=="hide-restore"\|\|name=="close"/);
  assert.match(host,/PostMessageW\(item\.window,WM_CLOSE,0,0\)/);
  assert.match(host,/case WM_CLOSE:item->request_close\(\);return 0;/,'queued close must use the normal WM_CLOSE lifecycle');
});

test('W02 errors and invalid focus/native results never save or close automatically',()=>{
  const inFlight=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'1',deferEcho:true});
  inFlight.time.advance(4);
  assert.equal(inFlight.echoReplies.length,7,'scalar Bridge workload reserves the eighth slot for End');
  assert.ok(inFlight.echoPayloadSizes.every(bytes=>bytes===4096));
  inFlight.node('stop').onclick();
  inFlight.echoReplies.forEach(done=>done());
  assert.equal(inFlight.reports.length,0);assert.deepEqual(inFlight.actions,[]);

  const blur=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'1',outputTiming:'1',dropFlood:true});
  assert.equal(blur.calls.filter(method=>method==='benchmark.measurementBegin').length,1);
  blur.listeners.blur();blur.time.advance(30000);
  assert.equal(blur.reports.length,0);assert.deepEqual(blur.actions,[]);
  assert.equal(blur.calls.filter(method=>method==='benchmark.measurementEnd').length,1);

  const unusable=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'1',snapshotUsable:false,dropFlood:true});
  unusable.time.advance(30000);
  assert.equal(unusable.reports.length,0,'saved native files with unusable source do not qualify for auto-save');
  assert.deepEqual(unusable.actions,[]);

  const wrongProfile=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'off',
    prepareOutput:'0',outputTiming:'1'});
  assert.equal(wrongProfile.calls.includes('benchmark.measurementBegin'),false);
  assert.equal(wrongProfile.reports.length,0);assert.deepEqual(wrongProfile.actions,[]);

  const wrongOutput=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'0'});
  assert.equal(wrongOutput.calls.includes('benchmark.measurementBegin'),false);
  assert.equal(wrongOutput.reports.length,0);assert.deepEqual(wrongOutput.actions,[]);

  const changedControls=mountedPage({autoMarker:true,focused:false,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'1'});
  changedControls.node('payload').value='64';changedControls.focus();
  assert.equal(changedControls.calls.includes('benchmark.measurementBegin'),false);
  assert.equal(changedControls.reports.length,0);assert.deepEqual(changedControls.actions,[]);

  const falseSaved=mountedPage({autoMarker:true,backend:'webview2',inputTrace:'scalar',observation:'scalar',
    prepareOutput:'0',outputTiming:'1',dropFlood:true,badSavedReceipt:true});
  falseSaved.time.advance(30000);
  assert.equal(falseSaved.reports.length,1);
  assert.deepEqual(falseSaved.actions,[],'a saved label with mismatched write id/path cannot trigger close');
  assert.match(falseSaved.node('status').textContent,/身份或路径不匹配/);
});

test('fixed-work UI saves its distinct schema through the existing writer and restores only one rAF chain',()=>{
  const h=mountedPage({messageChannel:true,echoCostMs:.02}),staleFrame=h.win.nextFrame;
  h.node('payload').value='60000';h.node('fixed').onclick();
  assert.equal(h.node('record').disabled,true);assert.equal(h.node('fixed').disabled,true);assert.equal(h.node('save').disabled,true);
  assert.equal(h.calls.filter(method=>method==='benchmark.echo').length,0);staleFrame();assert.equal(h.frameRequests.length,1);
  const status=h.node('status').textContent;assert.match(status,/本轮不运行 rAF/);
  h.time.advance(1000);assert.equal(h.calls.filter(method=>method==='benchmark.echo').length,1056);
  assert.equal(h.frameRequests.length,2);staleFrame();assert.equal(h.frameRequests.length,2);
  assert.equal(h.node('save').disabled,false);h.node('save').onclick();
  const report=h.reports[0];assert.equal(report.schema,'soui.sdk-performance-fixed-work/v1');
  assert.equal(report.measurementKind,'fixed-work-bridge');assert.equal(report.window.valid,true);
  assert.equal(report.fixedWork.valid,true);assert.equal(report.fixedWork.measured.success,1024);
  assert.equal(report.fixedWork.payloadBytes,60000);assert.equal(report.configuration.endpointsConsistent,true);
  assert.equal(report.configuration.wholeWindowStable,null);assert.equal(report.raf,undefined);
  assert.equal(h.node('fps').textContent,'本轮未测量');assert.equal(h.calls.some(method=>/^benchmark\.measurement/.test(method)),false);
  assert.ok(Buffer.byteLength(JSON.stringify(report))<60000);assert.match(h.node('status').textContent,/已保存/);
  h.node('record').onclick();h.time.advance(30000);h.node('save').onclick();
  assert.equal(h.reports[1].schema,'soui.sdk-performance-page/v1');assert.equal(h.reports[1].window.requestedMs,30000);
  assert.equal(h.reports[1].window.complete,true);assert.equal(h.frameRequests.length,2,'30s start reuses the restored chain');
});

test('fixed-work rejects active tracing and changed identity, and preserves blur/timeout failures without restarting native work',()=>{
  const traced=mountedPage({inputTrace:'scalar',messageChannel:true});traced.node('fixed').onclick();traced.time.advance(1000);
  assert.equal(traced.calls.includes('benchmark.echo'),false);assert.equal(traced.node('save').disabled,true);
  const changed=mountedPage({changeFixedIdentity:true,messageChannel:true,echoCostMs:.02});
  changed.node('fixed').onclick();changed.time.advance(1000);changed.node('save').onclick();
  const altered=changed.reports[0];assert.equal(altered.fixedWork.measured.success,1024);assert.equal(altered.window.valid,false);
  assert.equal(altered.configuration.endpointsConsistent,false);assert.equal(altered.fixedWork.successOperationsPerSecond,null);
  assert.ok(altered.window.invalidReasons.includes('native_identity_endpoint_missing_or_changed'));
  const blur=mountedPage({deferEcho:true,messageChannel:true});blur.node('fixed').onclick();blur.time.advance(0);
  assert.equal(blur.echoReplies.length,8);blur.listeners.blur();blur.time.advance(5100);blur.node('save').onclick();
  const failed=blur.reports[0],frozen=JSON.stringify(failed);assert.equal(failed.window.valid,false);
  assert.ok(failed.window.invalidReasons.includes('window_blur'));assert.equal(failed.fixedWork.warmup.timeouts,8);
  assert.equal(failed.fixedWork.nativeOutstandingKnown,false);assert.equal(blur.node('fixed').disabled,true);
  assert.equal(blur.node('record').disabled,true);
  for(const id of ['reload','second','hide']) { assert.equal(blur.node(id).disabled,true);blur.node(id).onclick(); }
  assert.equal(blur.calls.includes('benchmark.action'),false,'same-process navigation/new Host must not bypass unknown native work');
  blur.echoReplies.forEach(done=>done());
  assert.equal(JSON.stringify(blur.reports[0]),frozen);assert.equal(blur.calls.filter(method=>method==='benchmark.echo').length,8);
  const beginFailure=mountedPage({identityErrorAt:2,messageChannel:true});beginFailure.node('fixed').onclick();
  assert.equal(beginFailure.calls.includes('benchmark.echo'),false);assert.equal(beginFailure.node('fixed').disabled,true);
  beginFailure.node('reload').onclick();assert.equal(beginFailure.calls.includes('benchmark.action'),false);
  const endFailure=mountedPage({identityErrorAt:3,messageChannel:true,echoCostMs:.02});
  endFailure.node('fixed').onclick();endFailure.time.advance(1000);endFailure.node('save').onclick();
  assert.equal(endFailure.reports[0].window.valid,false);assert.equal(endFailure.reports[0].fixedWork.nativeOutstandingKnown,false);
  assert.equal(endFailure.reports[0].fixedWork.successOperationsPerSecond,null);assert.equal(endFailure.node('fixed').disabled,true);
});

test('fixed-work cannot reuse unknown native history from the original duration-based Bridge scene',()=>{
  const h=mountedPage({deferEcho:true,messageChannel:true});h.node('scene').value='bridge';h.node('record').onclick();
  h.time.advance(5005);h.node('stop').onclick();h.time.advance(5000);
  const attempts=h.calls.filter(method=>method==='benchmark.echo').length;
  assert.equal(h.node('pending').textContent,'0 / 8');assert.equal(h.node('fixed').disabled,true);
  for(const id of ['reload','second','hide']) { assert.equal(h.node(id).disabled,true);h.node(id).onclick(); }
  assert.equal(h.calls.includes('benchmark.action'),false);h.node('fixed').onclick();h.time.advance(1000);
  assert.equal(h.calls.filter(method=>method==='benchmark.echo').length,attempts);
  h.node('save').onclick();assert.equal(h.reports[0].schema,'soui.sdk-performance-page/v1');
  assert.ok(h.reports[0].bridge.timeouts>0,'old schema and diagnostics remain intact');
});

test('no workload starts before a click; fixed 30s recording freezes counters and uses saved receipt', () => {
  const h = mountedPage();
  assert.deepEqual(h.calls, ['bridge.bootstrap','benchmark.identity','bridge.probe.report']);
  assert.equal(h.node('record').disabled, false); h.node('record').onclick();
  h.node('input').handlers.keydown({type:'keydown'});
  h.time.advance(29999); assert.equal(h.node('save').disabled, true);
  h.time.advance(1); assert.equal(h.node('save').disabled, false);
  h.node('input').handlers.keydown({type:'keydown'}); h.node('save').onclick();
  assert.equal(h.reports.length, 1); assert.equal(h.reports[0].input.keydown, 1);
  assert.equal(h.calls.filter(x=>x==='benchmark.identity').length,2,'identity refreshed at manual measurement start');
  assert.equal(h.reports[0].window.requestedMs, 30000); assert.equal(h.reports[0].window.complete, true);
  assert.equal(h.reports[0].raf.physicalPresentation, false); assert.equal(h.reports[0].canvas.pixelCRC, false);
  assert.ok(Buffer.byteLength(JSON.stringify(h.reports[0])) < 60000);
  assert.ok(!JSON.stringify(h.reports[0]).includes('not-saved')); assert.match(h.node('status').textContent, /已保存/);
  assert.equal(h.calls.some(method=>/^benchmark\.measurement/.test(method)),false,'off must not add scalar requests');
});

test('blur/hidden mark comparison invalid; scenario pause stops synchronous flood instead of spinning', () => {
  const h = mountedPage(); h.node('scene').value='bridge'; h.node('scene').onchange(); h.node('record').onclick();
  h.time.advance(4); assert.equal(h.calls.filter(x => x==='benchmark.echo').length, 8);
  h.listeners.blur(); h.doc.hidden=true; h.documentListeners.visibilitychange();
  h.node('stop').onclick(); const atStop = h.calls.length; h.time.advance(10000); assert.equal(h.calls.length,atStop);
  h.node('save').onclick(); const report=h.reports[0];
  assert.equal(report.window.valid,false); assert.equal(report.window.complete,false);
  assert.deepEqual(report.window.invalidReasons,['window_blur','document_hidden','manual_stop']);
  assert.equal(report.bridge.success,8); assert.equal(report.bridge.transportCalls,8);
});

test('context-pending bootstrap retries are bounded, and ready waits for an accepted bootstrap', () => {
  const transient=mountedPage({bootstrapPending:2}); assert.equal(transient.node('record').disabled,true);
  transient.time.advance(200); assert.equal(transient.node('record').disabled,false);assert.equal(transient.node('fixed').disabled,false);
  assert.equal(transient.calls.filter(x=>x==='bridge.bootstrap').length,3);
  const permanent=mountedPage({bootstrapPending:500}); permanent.time.advance(20000);
  assert.equal(permanent.calls.length,101); assert.equal(permanent.node('record').disabled,true);
  assert.match(permanent.node('status').textContent,/E_CONTEXT_PENDING/);
});

test('changing scene cancels a pending start identity callback; old echo completions cannot contaminate a new record', () => {
  const pending=mountedPage({deferSecondIdentity:true}); pending.node('record').onclick();
  pending.node('scene').value='canvas'; pending.node('scene').onchange(); pending.resolveIdentity(); pending.time.advance(30000);
  assert.equal(pending.node('save').disabled,true); assert.equal(pending.reports.length,0);
  const h=mountedPage({deferEcho:true}); h.node('scene').value='bridge'; h.node('record').onclick(); h.time.advance(4);
  assert.equal(h.echoReplies.length,8); h.node('stop').onclick(); h.node('scene').value='input'; h.node('scene').onchange();
  h.node('record').onclick(); assert.match(h.node('status').textContent,/等待未结请求/);
  h.echoReplies.forEach(fn=>fn()); h.node('record').onclick(); h.time.advance(30000); h.node('save').onclick();
  assert.equal(h.reports[0].bridge.success,0); assert.equal(h.reports[0].bridge.attempted,0); assert.equal(h.reports[0].scenario,'input');
});

test('accepted save is not called saved until the writer reports completion', () => {
  const h=mountedPage({writesPending:2}); h.node('record').onclick(); h.time.advance(30000); h.node('save').onclick();
  assert.match(h.node('status').textContent,/已接纳保存/); assert.doesNotMatch(h.node('status').textContent,/已保存/);
  h.time.advance(400); assert.match(h.node('status').textContent,/已保存/);
});

test('true idle schedules no periodic page observer and resumes one bounded rAF chain on switching back', () => {
  const h=mountedPage(), staleFrame=h.win.nextFrame;
  assert.equal(h.frameRequests.length,1); h.node('scene').value='idle'; h.node('scene').onchange(); h.node('record').onclick();
  const message=h.node('status').textContent; h.time.advance(1000); staleFrame();
  assert.equal(h.frameRequests.length,1); assert.equal(h.node('status').textContent,message);
  h.time.advance(29000); h.node('save').onclick(); const report=h.reports[0];
  assert.equal(report.raf.measurementEnabled,false); assert.equal(report.raf.callbackHz,null); assert.equal(report.raf.callbacks,0);
  assert.equal(report.raf.unobservedHeadMs,null); assert.equal(report.raf.unobservedTailMs,null);
  assert.equal(report.raf.unobservedWholeWindowMs,null);
  h.node('scene').value='input'; h.node('scene').onchange(); assert.equal(h.frameRequests.length,2);
  h.node('start').onclick(); assert.equal(h.frameRequests.length,2,'starting must not add another live rAF chain');
  staleFrame(); assert.equal(h.frameRequests.length,2); h.win.nextFrame(); assert.equal(h.frameRequests.length,3);
});

test('Canvas may draw unchanged 60Hz state; report has no invented unique/effective frame count and includes the observation head', () => {
  const h=mountedPage(); h.node('scene').value='canvas'; h.node('record').onclick();
  h.time.advance(5); h.win.nextFrame(); h.time.advance(5); h.win.nextFrame(); h.node('stop').onclick(); h.node('save').onclick();
  const report=h.reports[0];
  assert.equal(report.canvas.drawCalls,2); assert.equal(report.canvas.steps,0); assert.equal(report.canvas.drawCallsMayRepeatState,true);
  assert.equal(report.canvas.visualChangeFrames,null); assert.equal(report.canvas.workloadComparable,null);
  assert.equal(report.canvas.stateHash,page.stateHash(page.createWorld(page.constants.seed,240)));
  assert.equal(report.raf.unobservedHeadMs,5); assert.equal(report.raf.unobservedTailMs,0);
  assert.ok(report.limitations.some(text=>text.includes('backlog prevents assuming equal work')));
});

test('scalar DOM window starts only after Begin receipt, immediately Ends at 30s, and waits for both persisted snapshots', () => {
  const h=mountedPage({inputTrace:'scalar',deferMeasurementBegin:true,scalarWritesPending:2});
  h.node('scene').value='idle'; h.node('scene').onchange(); h.node('record').onclick();
  h.time.advance(1000); assert.equal(h.calls.filter(x=>x==='benchmark.measurementEnd').length,0);
  assert.equal(h.node('save').disabled,true); h.resolveBegin(); h.time.advance(29999);
  assert.equal(h.calls.filter(x=>x==='benchmark.reportStatus').length,0,'no writer polling inside true idle DOM window');
  h.time.advance(1); assert.equal(h.calls.filter(x=>x==='benchmark.measurementEnd').length,1);
  assert.equal(h.node('save').disabled,true); assert.match(h.node('status').textContent,/确认 Begin/);
  h.time.advance(400); assert.equal(h.node('save').disabled,false); h.node('save').onclick();
  const report=h.reports[0], native=report.nativeMeasurement;
  assert.equal(report.nativeMeasurementId,7); assert.equal(report.window.valid,true); assert.equal(native.phase,'complete');
  assert.equal(native.begin.sentDomMs,0); assert.equal(native.begin.receivedDomMs,1000); assert.equal(native.domWindowStartMs,1000);
  assert.equal(native.domWindowEndMs,31000); assert.equal(native.end.requestedDomMs,31000); assert.equal(native.end.sentDomMs,31000);
  assert.equal(native.end.dispatchDelayMs,0); assert.equal(native.end.delayReason,'none');
  assert.equal(native.begin.receipt.beginNativeQpc,'1000'); assert.equal(native.end.receipt.endNativeQpc,'30000001000');
  assert.equal(native.begin.persisted,true); assert.equal(native.end.persisted,true); assert.equal(native.scope,'process-cumulative-live');
  assert.equal(report.parameters.inputTrace,'scalar'); assert.equal(report.raf.measurementEnabled,false);
});

test('scalar flood reserves the eighth slot for immediate End and records manual-stop invalidity', () => {
  const h=mountedPage({inputTrace:'scalar',deferEcho:true,deferMeasurementEnd:true});
  h.node('scene').value='bridge'; h.node('record').onclick(); h.time.advance(4);
  assert.equal(h.echoReplies.length,7); h.node('stop').onclick();
  assert.equal(h.calls.filter(x=>x==='benchmark.measurementEnd').length,1); assert.equal(h.node('pending').textContent,'8 / 8');
  assert.equal(h.node('save').disabled,true); h.resolveEnd(); h.node('save').onclick();
  const report=h.reports[0]; assert.equal(report.parameters.maxEchoOutstanding,7); assert.equal(report.parameters.maxOutstandingRequests,8);
  assert.equal(report.bridge.pendingAtEnd,7); assert.equal(report.window.valid,false); assert.ok(report.window.invalidReasons.includes('manual_stop'));
  assert.equal(report.nativeMeasurement.phase,'complete'); assert.equal(report.nativeMeasurement.end.dispatchDelayMs,0);
  h.echoReplies.forEach(fn=>fn()); assert.equal(report.bridge.success,0);
});

test('cancelled pending scalar Begin is closed when its receipt arrives; lifecycle navigation waits for End', () => {
  const cancelled=mountedPage({inputTrace:'scalar',deferMeasurementBegin:true}); cancelled.node('record').onclick();
  cancelled.node('scene').value='canvas'; cancelled.node('scene').onchange(); cancelled.time.advance(100); cancelled.resolveBegin();
  assert.equal(cancelled.calls.filter(x=>x==='benchmark.measurementEnd').length,1);
  cancelled.time.advance(30000); assert.equal(cancelled.node('save').disabled,true);
  const h=mountedPage({inputTrace:'scalar',deferMeasurementEnd:true}); h.node('record').onclick(); h.node('reload').onclick();
  assert.equal(h.calls.includes('benchmark.action'),false); h.resolveEnd();
  assert.equal(h.calls.filter(x=>x==='benchmark.action').length,1);
  assert.ok(h.calls.indexOf('benchmark.measurementEnd')<h.calls.indexOf('benchmark.action'));
});

test('unexpected eight-slot saturation has a bounded End wait and an explicit missing terminal on expiry', () => {
  function run(release) {
    const time=clock(), calls=[]; let occupied=0;
    const bridge={pending:()=>occupied,request(method,params,credentials,done,onAttempt) {
      if(onAttempt)onAttempt();
      calls.push(method);
      if(method==='benchmark.measurementBegin')done(null,nativeReceipt('begin'));
      else if(method==='benchmark.measurementEnd')done(null,nativeReceipt('end'));
      else done(null,{state:'saved',path:params.id===10?'native-begin.json':'native-end.json'});
    }};
    const measurement=page.createNativeMeasurement(bridge,{},time.now,time.later,()=>{});
    measurement.begin(error=>assert.equal(error,null)); occupied=8; measurement.end('window_complete');
    time.advance(100); assert.equal(calls.includes('benchmark.measurementEnd'),false);
    if(release){occupied=7;time.advance(25);}else time.advance(5900);
    return {result:measurement.view(),calls};
  }
  const released=run(true); assert.equal(released.result.phase,'complete');
  assert.equal(released.result.end.dispatchDelayMs,125); assert.equal(released.result.end.delayReason,'page_capacity');
  const expired=run(false); assert.equal(expired.result.phase,'failed'); assert.equal(expired.calls.includes('benchmark.measurementEnd'),false);
  assert.deepEqual(expired.result.missing,['end_not_dispatched']); assert.equal(expired.result.end.delayReason,'page_capacity_timeout');
  assert.equal(expired.result.end.sentDomMs,null); assert.equal(expired.result.end.persisted,null);
});

test('an End writer failure remains missing and invalid, even when the DOM 30s window is complete', () => {
  const h=mountedPage({inputTrace:'scalar',scalarWriteFailure:11}); h.node('record').onclick(); h.time.advance(30000);
  assert.match(h.node('status').textContent,/原生采样缺失/); h.node('save').onclick(); const report=h.reports[0];
  assert.equal(report.window.domValid,true); assert.equal(report.window.valid,false); assert.equal(report.nativeMeasurement.phase,'failed');
  assert.equal(report.nativeMeasurement.begin.persisted,true); assert.equal(report.nativeMeasurement.end.persisted,false);
  assert.ok(report.window.invalidReasons.includes('native_measurement_incomplete')); assert.deepEqual(report.nativeMeasurement.missing,['end_write_failed']);
});

test('Begin and End send stamps are taken at the actual transport attempt before observation DOM work', () => {
  const time=clock(), sends={};
  const bridge=page.createBridge(wire=>{
    const request=JSON.parse(wire);sends[request.method]=time.now();
    const result=request.method==='benchmark.measurementBegin'?nativeReceipt('begin'):
      request.method==='benchmark.measurementEnd'?nativeReceipt('end'):
      {state:'saved',path:request.params.id===10?'native-begin.json':'native-end.json'};
    return reply(wire,result);
  },time.later,time.cancel,time.now);
  const measurement=page.createNativeMeasurement(bridge,{},time.now,time.later,()=>time.elapseBlocked(40));
  measurement.begin(error=>assert.equal(error,null));measurement.end('window_complete');
  const result=measurement.view();
  assert.equal(result.phase,'complete');
  assert.equal(result.begin.sentDomMs,sends['benchmark.measurementBegin']);
  assert.equal(result.end.sentDomMs,sends['benchmark.measurementEnd']);
  assert.equal(result.end.sentDomMs,result.end.requestedDomMs,'observer notification cannot precede the send');
});

test('mounted finish sends End before constructing and serializing the report preview', () => {
  const h=mountedPage({inputTrace:'scalar'}), preview=h.node('reportPreview'), writes=[];
  let text=preview.textContent;
  Object.defineProperty(preview,'textContent',{get:()=>text,set(value){
    writes.push({endSent:h.calls.includes('benchmark.measurementEnd'),time:h.time.now()});
    h.time.elapseBlocked(50);text=value;
  }});
  h.node('record').onclick();h.time.advance(30000);h.node('save').onclick();
  assert.ok(writes.length>0);assert.ok(writes.every(write=>write.endSent));
  const measurement=h.reports[0].nativeMeasurement;
  assert.equal(measurement.end.sentDomMs,30000);assert.equal(measurement.end.dispatchDelayMs,0);
});

test('a saved status arriving beyond the shared 10s confirmation deadline cannot complete measurement', () => {
  const time=clock();
  const bridge={pending:()=>0,request(method,params,credentials,done,onAttempt){
    if(onAttempt)onAttempt();
    if(method==='benchmark.measurementBegin')done(null,nativeReceipt('begin'));
    else if(method==='benchmark.measurementEnd')done(null,nativeReceipt('end'));
    else if(params.id===10)done(null,{state:'saved',path:'native-begin.json'});
    else if(time.now()<9800)done(null,{state:'pending'});
    else time.later(()=>done(null,{state:'saved',path:'native-end.json'}),201);
  }};
  const measurement=page.createNativeMeasurement(bridge,{},time.now,time.later,()=>{});
  measurement.begin(error=>assert.equal(error,null));measurement.end('window_complete');
  time.advance(10000);assert.equal(measurement.view().phase,'persisting');
  time.advance(1);const result=measurement.view();
  assert.equal(result.phase,'failed');assert.equal(result.end.persisted,null);
  assert.deepEqual(result.missing,['end_write_unconfirmed']);
});

test('saved snapshots with an unavailable or faulty requested source remain invalid for comparison', () => {
  const h=mountedPage({inputTrace:'scalar',snapshotUsable:false});
  h.node('record').onclick();h.time.advance(30000);h.node('save').onclick();
  const report=h.reports[0], measurement=report.nativeMeasurement;
  assert.equal(measurement.phase,'complete');assert.equal(measurement.begin.persisted,true);assert.equal(measurement.end.persisted,true);
  assert.equal(measurement.measurementUsable,false);assert.equal(report.window.domValid,true);assert.equal(report.window.valid,false);
  assert.deepEqual(measurement.missing,['begin_snapshot_unusable','end_snapshot_unusable']);
  assert.ok(report.window.invalidReasons.includes('native_measurement_unusable'));
});

test('resource page has five manual workload entries plus true idle and no remote resource or automatic navigation', () => {
  const html = fs.readFileSync(require.resolve('../resources/index.html'),'utf8');
  for (const scene of ['input','canvas','bridge','pointer','lifecycle','idle']) assert.ok(html.includes('value="'+scene+'"'));
  assert.doesNotMatch(html, /(?:src|href)=["']https?:/);
  const js = fs.readFileSync(require.resolve('../resources/benchmark.js'),'utf8');
  assert.doesNotMatch(js, /\b(?:fetch|XMLHttpRequest|WebSocket)\s*\(/);
  assert.doesNotMatch(js, /location\.(?:href|assign|replace)\s*[=(]/);
});
