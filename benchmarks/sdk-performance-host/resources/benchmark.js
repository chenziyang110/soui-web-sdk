(function (root, factory) {
  'use strict';
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else api.mount(root, root.document);
}(typeof window !== 'undefined' ? window : this, function () {
  'use strict';
  var LIMIT = 65536, DURATION = 30000, SEED = 1374772973, STEP = 1000 / 60;
  function utf8Bytes(text) {
    var total = 0, i, c, next;
    for (i = 0; i < text.length; i += 1) {
      c = text.charCodeAt(i);
      if (c < 128) total += 1;
      else if (c < 2048) total += 2;
      else if (c >= 55296 && c <= 56319 && i + 1 < text.length &&
          (next = text.charCodeAt(i + 1)) >= 56320 && next <= 57343) { total += 4; i += 1; }
      else total += 3;
    }
    return total;
  }
  function failure(code, message) { var error = new Error(message); error.code = code; return error; }
  function createBridge(send, later, cancel, now) {
    var next = 1, active = 0, nativeUnknown = false;
    return {
      pending: function () { return active; },
      // Sticky across page scenarios: releasing a waiter cannot prove that its
      // native work ended. Preflight rejection never entered the transport.
      nativeOutstandingKnown: function () { return !nativeUnknown; },
      request: function (method, params, credentials, done, onAttempt) {
        var id = 'perf:' + next, packet, wire, requestBytes, started = now(), timer, settled = false, attempted = false;
        next += 1;
        if (active >= 8) { done(failure('E_PAGE_CAPACITY', '页面已有 8 个未结请求。')); return {attempted:false,requestBytes:0}; }
        try {
          packet = { requestId: id, sessionId: credentials.sessionId || '', origin: credentials.origin || '',
            token: credentials.token || '', method: method, params: params };
          // Bootstrap has an exact six-field protocol; its timeout is local.
          if (method !== 'bridge.bootstrap') packet.timeoutMs = 5000;
          wire = JSON.stringify(packet); requestBytes = utf8Bytes(wire);
          if (requestBytes > LIMIT) throw failure('E_PAGE_SIZE', '完整 UTF-8 请求超过 65536 字节。');
        } catch (error) { done(error); return {attempted:false,requestBytes:0}; }
        active += 1;
        function finish(error, raw) {
          var response, responseBytes = 0;
          if (settled) return;
          settled = true; active -= 1; cancel(timer);
          if (!error) try {
            if (typeof raw !== 'string') throw failure('E_PAGE_RESPONSE', '原生响应必须是 JSON 字符串。');
            responseBytes = utf8Bytes(raw);
            if (responseBytes > LIMIT) throw failure('E_PAGE_RESPONSE', '原生响应超过字节限制。');
            response = JSON.parse(raw);
            if (!response || response.requestId !== id) throw failure('E_PAGE_RESPONSE', '响应 requestId 不匹配。');
            if (response.ok !== true) throw failure(response.error && response.error.code || 'E_NATIVE',
              response.error && response.error.message || '原生请求失败。');
          } catch (parseError) { error = parseError; }
          var elapsed=Math.max(0,now()-started);
          if(!error && elapsed>=5000) error=failure('E_PAGE_TIMEOUT','响应到达时已超过页面期限；同步 native 调用无法被页面抢占。');
          // This exact bootstrap reply means context creation has not admitted
          // work yet. Other attempted failures stay conservatively unknown.
          var contextPending = method==='bridge.bootstrap' && response && response.requestId===id && response.ok===false &&
            response.error && response.error.code==='E_CONTEXT_PENDING';
          if(error && attempted && !contextPending)nativeUnknown=true;
          done(error, response && response.result, {requestBytes: requestBytes, responseBytes: responseBytes, elapsedMs:elapsed});
        }
        timer = later(function () { finish(failure('E_PAGE_TIMEOUT', '页面等待超时；未宣称取消原生工作。')); }, 5000);
        try {
          if(onAttempt)onAttempt(requestBytes);
          attempted=true;
          var returned = send(wire);
          if (returned && typeof returned.then === 'function') returned.then(function (raw) { finish(null, raw); }, function (error) { finish(error || failure('E_NATIVE', '原生请求失败。')); });
          else finish(null, returned);
        } catch (error) { finish(error); }
        return {attempted:true,requestBytes:requestBytes};
      }
    };
  }
  function createFixedWorkScheduler(win, now) {
    var channel = null, pending = null, ticket = 0, timer = 0, closed = false, scheduledAt = 0;
    var info = {primitive:'message-channel',fallback:false,fallbackReason:null,scheduled:0,delivered:0,totalYieldWaitMs:0};
    function deliver(token) {
      if (closed || token !== ticket || !pending) return;
      var callback = pending, elapsed = now() - scheduledAt;
      pending = null; timer = 0; info.delivered += 1;
      if (elapsed >= 0 && isFinite(elapsed)) info.totalYieldWaitMs += elapsed;
      callback();
    }
    try {
      if (typeof win.MessageChannel !== 'function') throw failure('E_CHANNEL_UNAVAILABLE','MessageChannel unavailable');
      channel = new win.MessageChannel();
      if (!channel.port1 || !channel.port2 || typeof channel.port2.postMessage !== 'function' ||
          typeof channel.port1.close !== 'function' || typeof channel.port2.close !== 'function')
        throw failure('E_CHANNEL_UNAVAILABLE','MessageChannel ports unavailable');
      channel.port1.onmessage = function (event) { deliver(event.data); };
      if (typeof channel.port1.start === 'function') channel.port1.start();
    } catch (error) {
      if (channel) {
        if (channel.port1 && typeof channel.port1.close === 'function') channel.port1.close();
        if (channel.port2 && typeof channel.port2.close === 'function') channel.port2.close();
      }
      channel = null; info.primitive = 'timer-4ms'; info.fallback = true;
      info.fallbackReason = String(error && error.code || 'E_CHANNEL_UNAVAILABLE');
    }
    return {
      schedule:function(callback) {
        if (closed || pending) return false;
        pending = callback; ticket += 1; scheduledAt = now(); info.scheduled += 1;
        if (channel) channel.port2.postMessage(ticket);
        else { var own = ticket; timer = win.setTimeout(function(){deliver(own);},4); }
        return true;
      },
      close:function() {
        if (closed) return; closed = true; ticket += 1; pending = null;
        if (timer) win.clearTimeout(timer); timer = 0;
        if (channel) { channel.port1.onmessage = null; channel.port1.close(); channel.port2.close(); }
      },
      snapshot:function() { return JSON.parse(JSON.stringify(info)); }
    };
  }
  function createFixedBridgeWork(bridge, credentials, now, later, cancel, scheduler, options, completed) {
    var size = options.payloadBytes, payload, phase = 'created', current, lastTime = null, clockFault = null;
    var totalTimer = 0, drainTimer = 0, sequence = 0, pumping = false, terminal = null, latencies = [];
    var data = {measurementKind:'fixed-work-bridge',targetSuccess:1024,warmupTarget:32,payloadBytes:size,maxInFlight:8,
      wholeRunDeadlineMs:60000,drainDeadlineMs:5000,createdDomMs:null,measurementStartDomMs:null,measurementEndDomMs:null,
      stopReason:null,invalidReasons:[],nativeOutstandingKnown:true,nativeWorkCancelled:false,transportDuplicateCount:null};
    function counters() { return {issued:0,transportAttempts:0,preflightRejected:0,success:0,failed:0,timeouts:0,mismatches:0,
      pending:0,peakPending:0,requestBytes:0,responseBytes:0}; }
    data.warmup = counters(); data.measured = counters(); current = data.warmup;
    function stamp() {
      var value = now();
      if (typeof value !== 'number' || !isFinite(value) || value < 0 || (lastTime !== null && value < lastTime)) {
        clockFault = 'monotonic_clock_invalid'; return lastTime === null ? 0 : lastTime;
      }
      lastTime = value; return value;
    }
    function invalid(reason) { if (data.invalidReasons.indexOf(reason) < 0) data.invalidReasons.push(reason); }
    function pending() { return data.warmup.pending + data.measured.pending; }
    function finish(at) {
      if (terminal) return;
      phase = 'terminal'; scheduler.close(); cancel(totalTimer); cancel(drainTimer);
      if (pending()) data.nativeOutstandingKnown = false;
      if (data.measurementStartDomMs !== null && data.measurementEndDomMs === null) data.measurementEndDomMs = at;
      var elapsed = data.measurementStartDomMs === null ? null : data.measurementEndDomMs - data.measurementStartDomMs;
      if (data.measured.success === data.targetSuccess && !(elapsed > 0)) invalid('elapsed_below_clock_resolution');
      var sorted = latencies.slice().sort(function(a,b){return a-b;});
      function percentile(fraction) { return sorted.length ? sorted[Math.ceil(sorted.length*fraction)-1] : null; }
      data.elapsedMs = elapsed; data.waitersPendingAtEnd = pending(); data.scheduler = scheduler.snapshot();
      data.rtt = {count:latencies.length,p50Us:percentile(0.5),p95Us:percentile(0.95),p99Us:percentile(0.99),
        maximumUs:sorted.length?sorted[sorted.length-1]:null,samplesUs:latencies.slice(),
        clock:'DOM performance.now; rounded microsecond units do not assert microsecond clock accuracy',
        scope:'Request construction through complete returned payload validation; includes scheduling, SDK and transport'};
      data.valid = data.invalidReasons.length === 0 && data.warmup.success === 32 && data.measured.success === 1024 &&
        data.measured.issued === 1024 && pending() === 0 && data.nativeOutstandingKnown && elapsed > 0;
      data.successOperationsPerSecond = data.valid ? 1024*1000/elapsed : null;
      data.payloadBytesPerSecond = data.valid ? 1024*size*1000/elapsed : null;
      data.physicalPresentationMeasured = false; data.pureNativeCostMeasured = false;
      terminal = JSON.parse(JSON.stringify(data)); completed(terminal);
    }
    function stop(reason, at) {
      if (terminal) return;
      invalid(reason); if (!data.stopReason) data.stopReason = reason;
      if (phase !== 'draining') {
        phase = 'draining'; data.pendingAtStop = pending(); scheduler.close();
        drainTimer = later(function(){invalid('drain_deadline');data.nativeOutstandingKnown=false;finish(stamp());},5000);
      }
      if (!pending()) finish(at === undefined ? stamp() : at);
    }
    function check(at) {
      if (clockFault) { stop(clockFault,at); return false; }
      var reason = options.checkState ? options.checkState() : null;
      if (reason) { stop(reason,at); return false; }
      if (at >= data.createdDomMs + 60000) { stop('whole_run_deadline',at); return false; }
      return !terminal && phase !== 'draining';
    }
    function schedule() {
      if (terminal || phase === 'draining') return;
      try { scheduler.schedule(pump); } catch (error) { stop('yield_scheduler_failed',stamp()); }
    }
    function progress(at) {
      if (terminal) return;
      if (phase === 'draining') { if (!pending()) finish(at); return; }
      if (current.issued === (phase === 'warming' ? 32 : 1024) && current.pending === 0) {
        if (phase === 'warming') { phase = 'running'; current = data.measured; data.measurementStartDomMs = at; schedule(); }
        else { data.measurementEndDomMs = at; finish(at); }
      } else if (!pumping && current.pending < 8 && current.issued < (phase === 'warming' ? 32 : 1024)) schedule();
    }
    function issue() {
      var own = current, id = sequence++, issuedAt = stamp(), attempted = false, settled = false;
      if (!check(issuedAt)) return;
      own.issued += 1; own.pending += 1; own.peakPending = Math.max(own.peakPending,own.pending);
      function done(error, result, timing) {
        if (settled || terminal) return; settled = true; own.pending -= 1;
        var mismatch = !error && (!result || result.sequence !== id || result.payload !== payload), at = stamp();
        if (timing) own.responseBytes += timing.responseBytes;
        if (mismatch) own.mismatches += 1;
        else if (error) {
          if (error.code === 'E_PAGE_TIMEOUT') own.timeouts += 1;
          else own.failed += 1;
          if (!attempted) own.preflightRejected += 1;
          if (attempted) data.nativeOutstandingKnown = false;
        } else {
          own.success += 1;
          if (own === data.measured && !clockFault) latencies.push(Math.round((at-issuedAt)*1000));
        }
        if (mismatch || error) stop(mismatch?'echo_payload_mismatch':error.code==='E_PAGE_TIMEOUT'?'request_timeout':'request_failed',at);
        else if (phase !== 'draining') check(at);
        progress(at);
      }
      try { bridge.request('benchmark.echo',{payload:payload,sequence:id},credentials,done,function(bytes) {
        attempted = true; own.transportAttempts += 1; own.requestBytes += bytes;
      }); } catch (error) { done(error); }
    }
    function pump() {
      if (pumping || terminal || phase === 'draining') return;
      var at = stamp(); if (!check(at)) return;
      pumping = true;
      try {
        var sent = 0, batchPhase = phase;
        while (sent < 8 && phase === batchPhase && !terminal && current.pending < 8 &&
            current.issued < (phase === 'warming' ? 32 : 1024)) { sent += 1; issue(); }
      } finally { pumping = false; }
      progress(stamp());
    }
    return {
      start:function() {
        if (phase !== 'created') return false;
        data.createdDomMs = stamp();
        if ([64,4096,60000].indexOf(size) < 0) { stop('unsupported_payload',data.createdDomMs); return false; }
        payload = new Array(size+1).join('a'); phase = 'warming';
        totalTimer = later(function(){stop('whole_run_deadline',stamp());},60000);
        if (check(data.createdDomMs)) schedule(); return true;
      },
      stop:function(reason) { stop(reason || 'manual_stop',stamp()); },
      busy:function() { return phase !== 'created' && phase !== 'terminal'; },
      view:function() { return terminal || JSON.parse(JSON.stringify(data)); }
    };
  }
  function createWorld(seed, count) {
    var state = seed >>> 0, sprites = [], i;
    function random() { state = (state * 1664525 + 1013904223) >>> 0; return state; }
    for (i = 0; i < count; i += 1) sprites.push({x: random() % (464 * 256), y: random() % (344 * 256),
      vx: (128 + random() % 641) * ((random() >>> 16) & 1 ? 1 : -1), vy: (128 + random() % 641) * ((random() >>> 16) & 1 ? 1 : -1)});
    return {sprites: sprites, steps: 0};
  }
  function stepWorld(world) {
    var i, item;
    for (i = 0; i < world.sprites.length; i += 1) {
      item = world.sprites[i]; item.x += item.vx; item.y += item.vy;
      if (item.x < 0) { item.x = -item.x; item.vx = -item.vx; }
      if (item.x > 464 * 256) { item.x = 2 * 464 * 256 - item.x; item.vx = -item.vx; }
      if (item.y < 0) { item.y = -item.y; item.vy = -item.vy; }
      if (item.y > 344 * 256) { item.y = 2 * 344 * 256 - item.y; item.vy = -item.vy; }
    }
    world.steps += 1;
  }
  function createTicker(world) {
    var accumulated = 0;
    return {advance: function (elapsed) {
      var steps = 0;
      if (!(elapsed >= 0) || !isFinite(elapsed)) throw failure('E_CLOCK', '非法时间增量。');
      accumulated += elapsed;
      while (accumulated + 0.000000001 >= STEP && steps < 8) { stepWorld(world); accumulated = Math.max(0, accumulated - STEP); steps += 1; }
      return steps;
    }, backlogMs: function () { return accumulated; }};
  }
  function stateHash(world) {
    var hash = 2166136261, i;
    function word(value) { var shift; for (shift = 0; shift < 32; shift += 8) { hash ^= (value >>> shift) & 255;
      hash = (hash + (hash << 1) + (hash << 4) + (hash << 7) + (hash << 8) + (hash << 24)) >>> 0; } }
    word(world.steps); word(world.sprites.length);
    for (i = 0; i < world.sprites.length; i += 1) { word(world.sprites[i].x); word(world.sprites[i].y); word(world.sprites[i].vx); word(world.sprites[i].vy); }
    return ('00000000' + hash.toString(16)).slice(-8);
  }
  function frameStats() {
    var buckets = [], count = 0, sum = 0, longest = 0, longFrames = 0, i;
    for (i = 0; i <= 4096; i += 1) buckets[i] = 0;
    function quantile(fraction) {
      var target = Math.ceil(count * fraction), found = 0, index;
      if (!count) return null;
      for (index = 0; index < buckets.length; index += 1) { found += buckets[index]; if (found >= target) return index === 4096 ? null : (index + 1) * 0.25; }
      return null;
    }
    return {add: function (elapsed) {
      if (!(elapsed > 0) || !isFinite(elapsed)) return false;
      count += 1; sum += elapsed; longest = Math.max(longest, elapsed); if (elapsed > 50) longFrames += 1;
      buckets[Math.min(4096, Math.floor(elapsed / 0.25))] += 1; return true;
    }, summary: function () { return {count: count, elapsedMs: sum, intervalHz: sum ? count * 1000 / sum : null,
      p50MsUpperBound: quantile(0.5), p95MsUpperBound: quantile(0.95), p99MsUpperBound: quantile(0.99),
      histogramResolutionMs: 0.25, overflowAtOrAboveMs: 1024, overflowCount: buckets[4096],
      maximumMs: count ? longest : null, above50Ms: longFrames}; }};
  }
  function createNativeMeasurement(bridge, credentials, now, later, changed) {
    var data={mode:'scalar',id:null,phase:'created',scope:'process-cumulative-live',error:null,missing:[],measurementUsable:null,
      domWindowStartMs:null,domWindowEndMs:null,
      begin:{sentDomMs:null,receivedDomMs:null,receipt:null,persisted:null},
      end:{requestedDomMs:null,sentDomMs:null,receivedDomMs:null,dispatchDelayMs:null,delayReason:null,receipt:null,persisted:null}};
    var endWanted=false, endStarted=false, waiters=[], capacityDeadline=0, persistDeadline=0;
    function uint(value) { return typeof value==='number' && value>=1 && value<=4294967295 && Math.floor(value)===value; }
    function qpc(value) { return typeof value==='string' && /^[0-9]{1,32}$/.test(value); }
    function receiptValid(value,phase) { return value && uint(value.id) && uint(value.writeId) && value.phase===phase &&
      value.status==='snapshot_queued' && value.scope==='process-cumulative-live' && typeof value.path==='string' &&
      qpc(value[phase==='begin'?'beginNativeQpc':'endNativeQpc']) && qpc(value.captureEndQpc) && qpc(value.qpcFrequency) &&
      typeof value.snapshotUsable==='boolean'; }
    function captureUsability(which,result) {
      data[which].usable=result.snapshotUsable;
      if(!result.snapshotUsable)data.missing.push(which+'_snapshot_unusable');
    }
    function notify() { changed(); }
    function settled(ok) { var pending=waiters; waiters=[]; notify(); for(var i=0;i<pending.length;i+=1)pending[i](ok); }
    function fail(code,error) {
      if(data.phase==='complete' || data.phase==='failed')return;
      data.missing.push(code); data.error=String(error && error.code || code)+': '+String(error && error.message || error || code).slice(0,180);
      data.phase='failed'; settled(false);
    }
    function persist(which) {
      if(data.phase!=='persisting')return;
      if(now()>=persistDeadline) { fail(which+'_write_unconfirmed','原生快照未在期限内确认写入。'); return; }
      if(bridge.pending()>=8) { later(function(){persist(which);},25); return; }
      bridge.request('benchmark.reportStatus',{id:data[which].receipt.writeId},credentials,function(error,result) {
        if(data.phase!=='persisting')return;
        if(now()>=persistDeadline) { fail(which+'_write_unconfirmed','写盘回执到达时已超过共同确认期限。'); return; }
        if(error || !result || result.state==='failed') {
          data[which].persisted=false; fail(which+'_write_failed',error || result && result.error || '缺少 writer 状态。'); return;
        }
        if(result.state==='saved') {
          if(typeof result.path!=='string' || result.path!==data[which].receipt.path) {fail(which+'_write_unconfirmed','快照写入路径不匹配。');return;}
          data[which].persisted=true; data[which].savedPath=result.path;
          if(which==='begin')persist('end');else {
            data.measurementUsable=data.begin.usable===true && data.end.usable===true;
            data.phase='complete';settled(data.measurementUsable);
          }
        } else if(result.state==='pending')later(function(){persist(which);},200);
        else fail(which+'_write_unconfirmed','未知 writer 状态。');
      });
    }
    function dispatchEnd() {
      if(data.phase==='failed' || data.phase==='complete')return;
      if(bridge.pending()>=8) {
        data.end.delayReason='page_capacity';
        if(now()>=capacityDeadline) {
          data.end.dispatchDelayMs=Math.max(0,now()-data.end.requestedDomMs); data.end.delayReason='page_capacity_timeout';
          fail('end_not_dispatched','8 个未结请求未在 6000ms 内释放 End 槽位。'); return;
        }
        later(dispatchEnd,25); return;
      }
      data.phase='end_pending';
      bridge.request('benchmark.measurementEnd',{id:data.id},credentials,function(error,result) {
        data.end.receivedDomMs=now();
        if(error || !receiptValid(result,'end') || result.id!==data.id || result.qpcFrequency!==data.begin.receipt.qpcFrequency)
          { fail('end_receipt_missing',error || '无效的 End 回执。'); return; }
        data.end.receipt=result; captureUsability('end',result); data.phase='persisting'; persistDeadline=now()+10000; notify(); persist('begin');
      },function() {
        // This runs after encoding and immediately before the native send. Do
        // no DOM/JSON notification between this timestamp and the transport.
        data.end.sentDomMs=now(); data.end.dispatchDelayMs=Math.max(0,data.end.sentDomMs-data.end.requestedDomMs);
        if(!data.end.delayReason)data.end.delayReason=data.end.dispatchDelayMs?'event_loop_or_begin_receipt':'none';
      });
      notify();
    }
    function startEnd() {
      if(endStarted || data.id===null || data.phase==='failed' || data.phase==='complete')return;
      endStarted=true; data.phase='waiting_capacity'; capacityDeadline=now()+6000; dispatchEnd();
    }
    return {
      view:function(){return JSON.parse(JSON.stringify(data));},
      busy:function(){return data.phase!=='created' && data.phase!=='complete' && data.phase!=='failed';},
      markDomStart:function(value){data.domWindowStartMs=value;data.phase='recording';},
      markDomEnd:function(value){data.domWindowEndMs=value;},
      begin:function(done) {
        if(data.phase!=='created') { done(failure('E_MEASUREMENT_STATE','不能重复 Begin。')); return; }
        data.phase='begin_pending';
        bridge.request('benchmark.measurementBegin',{},credentials,function(error,result) {
          data.begin.receivedDomMs=now();
          if(error || !receiptValid(result,'begin')) { fail('begin_receipt_missing',error || '无效的 Begin 回执。'); done(error || failure('E_MEASUREMENT_BEGIN','Begin 回执无效。')); return; }
          data.id=result.id; data.begin.receipt=result; captureUsability('begin',result); data.phase='ready'; notify();
          if(endWanted) { done(failure('E_RECORD_CANCELLED','DOM 记录开始前已取消。')); startEnd(); }
          else done(null);
        },function(){data.begin.sentDomMs=now();});
        notify();
      },
      end:function(reason,done) {
        if(data.phase==='complete' || data.phase==='failed') { if(done)done(data.phase==='complete'); return; }
        if(done) { if(waiters.length>=8) {done(false);return;} waiters.push(done); }
        if(!endWanted) { endWanted=true; data.end.requestedDomMs=now(); data.end.reason=reason; }
        startEnd();
      }
    };
  }
  function mount(win, doc) {
    var ids = ['identity','scene','payload','start','stop','record','fixed','fixedSummary','save','status','fps','p95','longFrames','steps','echo',
      'pending','canvas','input','feedback','scroll','scrollContent','inputCounts','second','reload','hide','reportPreview'];
    var ui = {}, index, context, credentials = {}, identity = null, connected = false, generation = 0, running = false;
    var scene = 'input', world, ticker, sceneLast = 0, draws = 0, frame = 0, stats, startedAt = 0, firstFrame = null, lastFrame = null;
    var animationScheduled=false, animationId=0, animationTicket=0;
    var recording = false, recordTimer = 0, floodTimer = 0, lastReport = null, invalid = [], input, echo, actions, payload = '', sequence = 0;
    var saving = false, starting = false, saveTimer = 0, lastRefresh = 0, liveStart = 0, liveFrames = 0, liveHz = 0;
    var measurement=null, measurementReport=null, actionPending=false;
    var fixedRun=null, fixedPreparing=false, fixedCancelled=null, fixedNeedsRestart=false, fixedTicket=0;
    var hasPerformance = !!(win.performance && typeof win.performance.now === 'function');
    function now() { return hasPerformance ? win.performance.now() : Date.now(); }
    function status(text) { ui.status.textContent = text; }
    function fault(error) { return String(error && error.code || 'E_PAGE') + ': ' + String(error && error.message || error).slice(0, 180); }
    for (index = 0; index < ids.length; index += 1) ui[ids[index]] = doc.getElementById(ids[index]);
    context = ui.canvas.getContext('2d');
    var bridge = createBridge(function (wire) {
      if (typeof win.__souiNativePostMessage !== 'function') throw failure('E_BRIDGE', '原生 Bridge 尚未就绪。');
      return win.__souiNativePostMessage(wire);
    }, function (fn, delay) { return win.setTimeout(fn, delay); }, function (timer) { win.clearTimeout(timer); }, now);
    function nativeBusy() { return !!measurement && measurement.busy(); }
    function fixedBusy() { return fixedPreparing || !!fixedRun && fixedRun.busy(); }
    function nativeView() { return measurement?measurement.view():{mode:'off',phase:'disabled',id:null}; }
    function nativeChanged() {
      var value=nativeView();
      if(measurementReport) {
        measurementReport.nativeMeasurement=value; measurementReport.nativeMeasurementId=value.id;
        measurementReport.window.valid=measurementReport.window.domValid && value.phase==='complete' && value.measurementUsable===true;
        if(value.phase==='failed' && measurementReport.window.invalidReasons.indexOf('native_measurement_incomplete')<0)
          measurementReport.window.invalidReasons.push('native_measurement_incomplete');
        if(value.phase==='complete' && value.measurementUsable!==true && measurementReport.window.invalidReasons.indexOf('native_measurement_unusable')<0)
          measurementReport.window.invalidReasons.push('native_measurement_unusable');
        if(measurementReport===lastReport)ui.reportPreview.textContent=JSON.stringify(lastReport,null,2);
      }
      if(value.phase==='begin_pending')status('等待原生 scalar Begin 回执；DOM 计时尚未开始。');
      else if(value.phase==='waiting_capacity' || value.phase==='end_pending')status('DOM 记录已停止，正在结束原生 scalar 采样。');
      else if(value.phase==='persisting')status('End 已返回，正在确认 Begin / End 快照写盘。');
      else if(value.phase==='failed')status('原生采样缺失：'+value.error+'；未宣称完整采样。');
      else if(value.phase==='complete')status(value.measurementUsable!==true?
        '原生快照已保存，但来源缺失或统计无效；本轮不用于性能对照。':measurementReport && measurementReport.window.valid?
        'DOM 记录及原生快照已完成，可以保存报告。':'原生快照已保存；已取消或失效的 DOM 记录仍然无效。');
      refresh(true);
    }
    function echoLimit() { return recording && measurement?7:8; }
    function resetCounters() {
      world = createWorld(SEED, 240); ticker = createTicker(world); draws = 0; frame = 0; stats = frameStats(); firstFrame = lastFrame = null;
      input = {keydown:0,keyup:0,compositionstart:0,compositionupdate:0,compositionend:0,input:0,pointer:0,wheel:0,scroll:0,
        coalescedSupported:false,coalescedSamples:0,handlerTotalMs:0};
      echo = {attempted:0,transportCalls:0,success:0,failed:0,timeouts:0,mismatches:0,requestBytes:0,responseBytes:0,totalRoundTripMs:0,maximumRoundTripMs:0,
        pendingAtEnd:0,transportOutstandingAtFinalization:0};
      actions = {requested:0,accepted:0,failed:0}; sequence = 0; invalid = [];
    }
    function invalidate(reason) {
      if (recording && invalid.indexOf(reason) < 0) invalid.push(reason);
      if (fixedPreparing) fixedCancelled = reason;
      if (fixedRun && fixedRun.busy()) fixedRun.stop(reason);
    }
    function report(actualEnd, complete) {
      var end = complete ? startedAt + DURATION : actualEnd, elapsed = Math.max(0, end - startedAt);
      return {schema:'soui.sdk-performance-page/v1',identity:identity,scenario:scene,
        nativeMeasurementId:nativeView().id,nativeMeasurement:nativeView(),
        parameters:{seed:SEED,sprites:240,logicalViewport:{width:480,height:360},stepHz:60,maxStepsPerCallback:8,
          payloadBytes:Number(ui.payload.value),maxOutstandingRequests:8,maxEchoOutstanding:measurement?7:8,inputTrace:measurement?'scalar':'off'},
        window:{requestedMs:DURATION,observedMs:elapsed,finalizationDelayMs:Math.max(0,actualEnd-end),complete:complete,
          domValid:complete && invalid.length===0,valid:complete && invalid.length===0 && !measurement,
          invalidReasons:invalid.slice(),clock:hasPerformance?'performance.now':'Date.now',
          documentVisibility:doc.visibilityState || 'unknown',hasFocus:typeof doc.hasFocus==='function'?doc.hasFocus():null,
          viewport:{width:win.innerWidth,height:win.innerHeight,devicePixelRatio:win.devicePixelRatio || 1},
          canvasCssViewport:{width:ui.canvas.clientWidth,height:ui.canvas.clientHeight}},
        raf:{measurementEnabled:scene!=='idle',callbacks:frame,callbackHz:scene!=='idle' && elapsed ? frame*1000/elapsed : null,intervals:stats.summary(),
          unobservedHeadMs:firstFrame===null?null:Math.max(0,firstFrame-startedAt),
          unobservedTailMs:lastFrame===null?null:Math.max(0,end-lastFrame),
          unobservedWholeWindowMs:scene!=='idle' && !frame?elapsed:null,physicalPresentation:false},
        canvas:{steps:world.steps,drawCalls:draws,spriteDrawCalls:draws*240,accumulatorBacklogMs:ticker.backlogMs(),
          visualChangeFrames:null,drawCallsMayRepeatState:true,workloadComparable:null,
          backlogWholeSteps:Math.floor(ticker.backlogMs()/STEP),
          stateHash:stateHash(world),hashAlgorithm:'fnv1a32-little-endian-integer-state-v1',pixelCRC:false},
        input:JSON.parse(JSON.stringify(input)),bridge:JSON.parse(JSON.stringify(echo)),actions:JSON.parse(JSON.stringify(actions)),
        limitations:['rAF is callback scheduling, not physical presentation.','DOM feedback and handler duration are not input-to-photon latency.',
          'Scene state hashes compare only equal seed and step counts.','Timeout stops waiting; native work is not claimed cancelled.',
          'transportCalls/bytes count attempted sends, not native admission receipts.',
          'Page timers cannot preempt synchronous native calls; late completions count as timeouts.',
          'Frame quantiles cover observed complete intervals; head and tail are separate. With no callbacks the whole window is unobserved.',
          'Draw calls may repeat the same 60 Hz simulation state. They are not unique visual frames.',
          'Equal-work speedup requires equal parameters, simulation steps and state hash; backlog prevents assuming equal work.',
          'Idle disables the page rAF and periodic DOM observer; input mode includes that observer.',
          'Native QPC and DOM monotonic time are separate clocks, never subtracted here. Native snapshot window is not asserted to be exactly the DOM 30 seconds.',
          'Scalar uses process-cumulative snapshots and reserves one transport slot for End; compare only equal inputTrace and echo limits.']};
    }
    function finish(reason, complete) {
      if (!recording) return;
      var actualEnd=now();
      if (reason && invalid.indexOf(reason)<0) invalid.push(reason);
      if(measurement)measurement.markDomEnd(complete?startedAt+DURATION:actualEnd);
      echo.pendingAtEnd = Math.max(0,echo.attempted-echo.success-echo.failed);
      echo.transportOutstandingAtFinalization=bridge.pending(); recording = false;
      win.clearTimeout(recordTimer); running = false; generation += 1; win.clearTimeout(floodTimer); floodTimer = 0;
      // End precedes report construction, preview serialization and UI work.
      // A synchronous transport may already finish; nativeChanged below binds
      // its final state to the newly created immutable DOM counters.
      if(measurement) { measurementReport=null; measurement.end(reason || (complete?'window_complete':'manual_stop')); }
      lastReport = report(actualEnd, complete);
      if(measurement) { measurementReport=lastReport; nativeChanged(); }
      else status(lastReport.window.valid ? '30 秒记录完成，可以保存报告。' : '记录已保留，但本轮无效：' + invalid.join('、'));
      ui.reportPreview.textContent = JSON.stringify(lastReport, null, 2);
      refresh(true);
    }
    function stop(reason) {
      if (fixedPreparing) fixedCancelled = reason || 'manual_stop';
      if (fixedRun && fixedRun.busy()) fixedRun.stop(reason || 'manual_stop');
      if (recording) finish(reason || 'manual_stop', false);
      else if(starting && nativeBusy())measurement.end(reason || 'cancel_before_dom_start');
      running = false; starting = false; generation += 1; win.clearTimeout(floodTimer); floodTimer = 0; refresh(true);
    }
    function flood() {
      var own = generation, sent = 0;
      floodTimer = 0;
      if(recording && now()>=startedAt+DURATION)finish(null,true);
      if (!running || scene !== 'bridge') return;
      while (sent < echoLimit() && bridge.pending() < echoLimit() && running && own === generation) {
        sent += 1;
        (function (current) {
          echo.attempted += 1;
          bridge.request('benchmark.echo', {payload:payload,sequence:current}, credentials, function (error, result, timing) {
            if(recording && now()>=startedAt+DURATION)finish(null,true);
            if (own !== generation || !running) return;
            if (timing) { echo.responseBytes += timing.responseBytes;
              echo.totalRoundTripMs += timing.elapsedMs; echo.maximumRoundTripMs = Math.max(echo.maximumRoundTripMs,timing.elapsedMs); }
            if (!error && (!result || result.sequence !== current || result.payload !== payload)) {
              error = failure('E_ECHO_MISMATCH','Echo 内容不一致。'); echo.mismatches += 1; invalidate('echo_mismatch');
            }
            if (error) { echo.failed += 1; if (error.code === 'E_PAGE_TIMEOUT') echo.timeouts += 1; }
            else echo.success += 1;
            scheduleFlood();
          },function(bytes){echo.transportCalls+=1;echo.requestBytes+=bytes;});
        }(sequence));
        sequence += 1;
      }
      // A synchronous native port must also yield instead of recursively filling forever.
      scheduleFlood();
    }
    function scheduleFlood() { var own=generation;
      if (!floodTimer && running && scene === 'bridge' && bridge.pending()<echoLimit())
        floodTimer = win.setTimeout(function(){if(own===generation)flood();}, 4); }
    function begin(withRecording) {
      if (!connected) { status('Bridge 尚未连接。'); return; }
      if (fixedBusy() || fixedNeedsRestart) { status('先等待固定工作结束；原生终态未知时请关闭并重新启动 Host。'); return; }
      if(nativeBusy()) { status('先等待上一轮原生采样结束和快照确认。'); return; }
      if (bridge.pending()) { stop('restart_pending'); status('已暂停旧场景；等待未结请求结束后，再点击运行或记录。'); return; }
      stop('restart'); starting=true; var own=generation;
      status('正在读取本轮 Host 身份、窗口尺寸和运行配置。');
      bridge.request('benchmark.identity',{},credentials,function(error,value) {
        if(own!==generation)return;
        if(error || !value) { starting=false;status(error?fault(error):'缺少本轮 Host 身份。'); refresh(true); return; }
        identity=value; measurementReport=null; measurement=null;
        function launch() {
        starting=false; resetCounters(); scene = ui.scene.value; payload = new Array(Number(ui.payload.value)+1).join('a');
        running = true; generation += 1; sceneLast = now(); startedAt = sceneLast; recording = withRecording;
        if(measurement)measurement.markDomStart(startedAt);
        if(scene==='idle')stopFrames();else scheduleFrame();
        if (withRecording) {
          if (doc.hidden) invalidate('hidden_at_start');
          if (typeof doc.hasFocus==='function' && !doc.hasFocus()) invalidate('unfocused_at_start');
          if (!hasPerformance) invalidate('monotonic_clock_unavailable');
          var recordingGeneration=generation;
          var completeWhenDue=function() { var remaining=startedAt+DURATION-now(); if(!recording || recordingGeneration!==generation)return;
            if(remaining>0)recordTimer=win.setTimeout(completeWhenDue,Math.ceil(remaining));else finish(null,true); };
          recordTimer = win.setTimeout(completeWhenDue, DURATION);
          status(scene==='idle'?'正在记录 30 秒真空闲；期间不刷新页面仪表，请保持窗口前台且不输入。':
            '正在记录 30 秒；保持窗口在前台、尺寸与负载不变。');
        } else status('场景运行中。记录按钮会重新从固定种子开始。');
        scheduleFlood(); refresh(true);
        }
        if(withRecording && identity.inputTrace==='scalar') {
          measurement=createNativeMeasurement(bridge,credentials,now,function(fn,delay){return win.setTimeout(fn,delay);},nativeChanged);
          var requested=measurement;
          requested.begin(function(beginError) {
            if(own!==generation) { requested.end('cancel_before_dom_start'); return; }
            if(beginError) { starting=false;status(fault(beginError));refresh(true);return; }
            launch();
          });
        } else launch();
      });
      refresh(true);
    }
    function fixedConfiguration(value) {
      var native = value && value.native, environment = value && value.environment;
      function integer(number) { return typeof number==='number' && isFinite(number) && Math.floor(number)===number; }
      if (!value || value.inputTrace !== 'off' || value.observation !== 'off' || value.detailedTraceConfiguredByConsumer !== false ||
          !environment || !(environment.SOUI_WEBVIEW2_OUTPUT_TIMING === null || environment.SOUI_WEBVIEW2_OUTPUT_TIMING === '0') ||
          !native || native.windowVisible !== true || native.windowIconic !== false || native.pacerFailed !== false ||
          native.documentReady !== true || native.applicationReady !== true ||
          !integer(native.pacerHz) || native.pacerHz < 30 || native.pacerHz > 1000 ||
          !integer(value.hostIndex) || value.hostIndex < 0 || value.hostIndex > 1 ||
          !integer(value.sessionGeneration) || value.sessionGeneration < 1 || value.sessionGeneration>9007199254740991 ||
          native.sessionGeneration !== value.sessionGeneration) return null;
      var result = {}, keys = ['backend','architecture','buildIdentity','sdkVersion','sdkPackageVersion','optionalFeedback','pageHash','pageFiles',
        'hostIndex','sessionGeneration','engineBudgetUs','requestedFrameRate','environment','inputTrace','observation',
        'executableSha256','requestedNodePath','nodeSha256','fixedWebView2'], i;
      for (i=0;i<keys.length;i+=1) result[keys[i]]=value[keys[i]];
      result.native={}; keys=['pid','creationFileTime','clientWidth','clientHeight','windowDpiX','windowDpiY','sessionGeneration','pacerHz',
        'pacerBackend','runtimeIdentity','browserPid','loadedNodePath','loadedIdentityBound','loadedModuleMatches'];
      for (i=0;i<keys.length;i+=1) result.native[keys[i]]=native[keys[i]];
      return JSON.stringify(result);
    }
    function startFixedWork() {
      if(!bridge.nativeOutstandingKnown())fixedNeedsRestart=true;
      if (!connected || fixedBusy() || nativeBusy() || saving || starting || actionPending || bridge.pending() || fixedNeedsRestart) {
        status('先结束当前工作并等待未结请求；终态未知时请重新启动 Host。'); return;
      }
      stop('fixed_work_start'); stopFrames(); fixedPreparing=true; fixedCancelled=null;
      var own=++fixedTicket, size=Number(ui.payload.value), viewport={width:win.innerWidth,height:win.innerHeight,devicePixelRatio:win.devicePixelRatio || 1};
      function stateReason() {
        if (fixedCancelled) return fixedCancelled;
        if (!hasPerformance) return 'monotonic_clock_unavailable';
        if (doc.hidden || doc.visibilityState !== 'visible') return 'document_hidden';
        if (typeof doc.hasFocus !== 'function' || !doc.hasFocus()) return 'window_unfocused';
        if (win.innerWidth !== viewport.width || win.innerHeight !== viewport.height || (win.devicePixelRatio || 1) !== viewport.devicePixelRatio) return 'viewport_changed';
        return null;
      }
      function resume() { fixedPreparing=false; refresh(true); scheduleFrame(); }
      status('读取固定工作身份；随后暖机 32 次、验证 1024 次 echo。'); refresh(true);
      bridge.request('benchmark.identity',{},credentials,function(error,value) {
        if (own !== fixedTicket) return;
        var frozen=fixedConfiguration(value), reason=stateReason();
        if (error || !frozen || reason) {
          if(!bridge.nativeOutstandingKnown())fixedNeedsRestart=true;
          status(error?fault(error):reason || '固定工作要求已就绪的可见 Host、observation/input trace/output timing 全部关闭。'); resume(); return;
        }
        var fixedIdentity=JSON.parse(JSON.stringify(value)); identity=value;
        measurement=null; measurementReport=null; lastReport=null; fixedPreparing=false;
        var scheduler=createFixedWorkScheduler(win,now);
        fixedRun=createFixedBridgeWork(bridge,credentials,now,function(fn,delay){return win.setTimeout(fn,delay);},
          function(timer){win.clearTimeout(timer);},scheduler,{payloadBytes:size,checkState:stateReason},function(result) {
            if (own !== fixedTicket) return;
            fixedPreparing=true; fixedNeedsRestart=!result.nativeOutstandingKnown;
            var capturedReason=stateReason(), ownReport={schema:'soui.sdk-performance-fixed-work/v1',measurementKind:'fixed-work-bridge',
              identity:fixedIdentity,scenario:'fixed-bridge',fixedWork:result,window:{valid:result.valid,invalidReasons:result.invalidReasons.slice(),
                viewport:viewport,documentVisibility:doc.visibilityState || 'unknown',hasFocus:typeof doc.hasFocus==='function'?doc.hasFocus():null},
              configuration:{start:frozen,end:null,endpointsConsistent:false,wholeWindowStable:null},
              limitations:['End-to-end Bridge round trips, not FPS or pure native processing cost.',
                'Timer fallback and macrotask scheduling cost remain in elapsed time; this is not an SDK maximum-throughput proof.',
                'Timeout stops waiting and does not cancel native work; unknown native work requires a new Host process.',
                'Matching cached native endpoints do not prove continuous display/Host configuration stability.']};
            function publish(endError,endIdentity) {
              var endConfiguration=fixedConfiguration(endIdentity), lateReason=stateReason();
              ownReport.configuration.end=endConfiguration;
              ownReport.configuration.endpointsConsistent=!!endConfiguration && endConfiguration===frozen;
              if(endError || !ownReport.configuration.endpointsConsistent)ownReport.window.invalidReasons.push('native_identity_endpoint_missing_or_changed');
              if(capturedReason || lateReason)ownReport.window.invalidReasons.push(capturedReason || lateReason);
              ownReport.window.valid=ownReport.window.valid && ownReport.window.invalidReasons.length===0;
              if(!bridge.nativeOutstandingKnown()) {fixedNeedsRestart=true;ownReport.fixedWork.nativeOutstandingKnown=false;}
              if(!ownReport.window.valid) {
                ownReport.fixedWork.valid=false;ownReport.fixedWork.successOperationsPerSecond=null;ownReport.fixedWork.payloadBytesPerSecond=null;
              }
              lastReport=ownReport; ui.reportPreview.textContent=JSON.stringify(ownReport,null,2);
              ui.fixedSummary.textContent=ownReport.window.valid?
                '固定 1024 次成功：'+result.elapsedMs.toFixed(2)+' ms · '+result.successOperationsPerSecond.toFixed(1)+' 次/秒 · RTT P95 '+(result.rtt.p95Us/1000).toFixed(3)+' ms':
                '固定工作结果已保留，无效原因：'+ownReport.window.invalidReasons.join('、');
              status(fixedNeedsRestart?'原生工作终态未知；可保存诊断报告，请关闭并重新启动 Host 后再测。':'固定工作已结束，可保存报告。'); resume();
            }
            if(!result.nativeOutstandingKnown || bridge.pending())publish(failure('E_NATIVE_OUTSTANDING','原生终态未知。'),null);
            else bridge.request('benchmark.identity',{},credentials,publish);
          });
        status('固定工作进行中；本轮不运行 rAF 或周期仪表。'); fixedRun.start(); refresh(true);
      });
    }
    function draw() {
      var i, item, colors = ['#4cd4be','#ffc568','#78aaff','#f590c2'];
      context.fillStyle='#101b31'; context.fillRect(0,0,480,360);
      for(i=0;i<world.sprites.length;i+=1) { item=world.sprites[i]; context.save(); context.translate(item.x/256+8,item.y/256+8);
        context.rotate(((world.steps+i)%360)*Math.PI/180); context.fillStyle=colors[i%4]; context.fillRect(-6,-6,12,12);
        context.fillStyle='#ffffff'; context.fillRect(0,-4,3,3); context.restore(); }
      draws += 1;
    }
    function refresh(force) {
      var time=now(), summary, elapsed=recording?time-startedAt:0;
      if (fixedBusy() && !force) return;
      if (!force && time-lastRefresh<250) return; lastRefresh=time;
      summary=stats.summary();
      ui.fps.textContent=scene==='idle'?'未测量':recording && elapsed>0 ? (frame*1000/elapsed).toFixed(1) :
        !running && lastReport && lastReport.raf && lastReport.raf.callbackHz!==null ? lastReport.raf.callbackHz.toFixed(1) : liveHz.toFixed(1);
      if(fixedBusy() || lastReport && lastReport.measurementKind==='fixed-work-bridge')ui.fps.textContent='本轮未测量';
      ui.p95.textContent=summary.p95MsUpperBound===null?'—':summary.p95MsUpperBound.toFixed(2)+' ms';
      ui.longFrames.textContent=String(summary.above50Ms); ui.steps.textContent=world.steps+' / '+draws;
      ui.echo.textContent=echo.success+' / '+echo.failed; ui.pending.textContent=bridge.pending()+' / 8';
      ui.inputCounts.textContent='键盘 '+(input.keydown+input.keyup)+' · IME '+(input.compositionstart+input.compositionupdate+input.compositionend)+
        ' · 指针 '+input.pointer+' · 滚动 '+input.scroll;
      ui.start.disabled=!connected || recording || saving || starting || nativeBusy() || fixedBusy() || fixedNeedsRestart; ui.record.disabled=ui.start.disabled;
      ui.fixed.disabled=ui.start.disabled || running || actionPending || bridge.pending()>0 || !bridge.nativeOutstandingKnown();
      ui.save.disabled=!lastReport || saving || recording || starting || nativeBusy() || fixedBusy(); ui.scene.disabled=recording || fixedBusy(); ui.payload.disabled=recording || fixedBusy();
      ui.second.disabled=ui.reload.disabled=ui.hide.disabled=!connected || saving || actionPending || fixedBusy() || fixedNeedsRestart || !bridge.nativeOutstandingKnown();
    }
    function stopFrames() {
      animationTicket+=1;
      if(animationScheduled && typeof win.cancelAnimationFrame==='function')win.cancelAnimationFrame(animationId);
      animationScheduled=false;
    }
    function scheduleFrame() {
      if(animationScheduled || scene==='idle' || fixedBusy())return;
      animationScheduled=true; var own=++animationTicket;
      animationId=win.requestAnimationFrame(function(){if(own!==animationTicket)return;animationScheduled=false;animation();});
    }
    function animation() {
      var time=now(), delta=time-sceneLast;
      liveFrames+=1; if(time-liveStart>=1000) { liveHz=liveFrames*1000/(time-liveStart); liveStart=time; liveFrames=0; }
      if (recording && time>=startedAt+DURATION) finish(null,true);
      if (running) {
        if(delta<0) { invalidate('clock_reversed'); delta=0; }
        sceneLast=time;
        if(recording) { frame+=1; if(firstFrame===null)firstFrame=time; if(lastFrame!==null) stats.add(time-lastFrame); lastFrame=time; }
        if(scene==='canvas') { ticker.advance(delta); draw(); }
      }
      refresh(false); scheduleFrame();
    }
    function inputEvent(event) {
      var began=now(), type=event.type, list, total;
      if(fixedBusy())invalidate('activity_during_fixed_work');
      if (recording && began>=startedAt+DURATION) finish(null,true);
      if(recording && scene==='idle')invalidate('activity_during_idle');
      if (Object.prototype.hasOwnProperty.call(input,type)) input[type]+=1;
      if(type==='pointermove'||type==='pointerdown'||type==='pointerup'||type==='mousemove'||type==='mousedown'||type==='mouseup') {
        input.pointer+=1;
        if(typeof event.getCoalescedEvents==='function') try { input.coalescedSupported=true; list=event.getCoalescedEvents(); input.coalescedSamples+=list.length; } catch (ignored) {}
      }
      total=input.keydown+input.keyup+input.input+input.compositionstart+input.compositionupdate+input.compositionend+input.pointer+input.wheel+input.scroll;
      ui.feedback.style.backgroundColor=total%2?'#ceeedd':'#d8e8ff'; ui.feedback.textContent=type+' · '+total+' 次事件（未保存输入内容）';
      input.handlerTotalMs+=Math.max(0,now()-began);
    }
    function saveReport() {
      var ownReport=lastReport, polls=0;
      if(!ownReport || saving || nativeBusy() || fixedBusy()) return;
      if(utf8Bytes(JSON.stringify(ownReport))>60000) { status('报告超过保存预算，未截断或伪造身份字段。'); return; }
      saving=true; refresh(true);
      bridge.request('benchmark.report',{report:ownReport},credentials,function(error,result) {
        if(error || !result || !result.accepted || typeof result.id!=='number' || result.id<1 || result.id>4294967295 || Math.floor(result.id)!==result.id)
          { saving=false; status(error?fault(error):'保存接纳回执无效。'); refresh(true); return; }
        status('已接纳保存，正在等待写盘完成。');
        function poll() {
          polls+=1;
          bridge.request('benchmark.reportStatus',{id:result.id},credentials,function(checkError,receipt) {
            if(checkError || !receipt || receipt.state==='failed') { saving=false; status(checkError?fault(checkError):'保存失败：'+String(receipt&&receipt.error||'未知错误')); refresh(true); return; }
            if(receipt.state==='saved') { saving=false; status('已保存：'+receipt.path); refresh(true); return; }
            if(receipt.state!=='pending' || polls>=50) { saving=false; status('尚未确认写盘完成；请按原生报告路径检查。'); refresh(true); return; }
            saveTimer=win.setTimeout(poll,200);
          });
        }
        poll();
      });
    }
    function action(name) {
      if(!bridge.nativeOutstandingKnown())fixedNeedsRestart=true;
      if(fixedNeedsRestart) { status('原生工作终态未知；请保存诊断并关闭当前 Host 进程，页面导航或新增 Host 不能隔离旧工作。'); return; }
      if(fixedBusy()) { stop('lifecycle_'+name);status('固定工作已暂停；等待未结请求后再执行生命周期操作。');return; }
      if(actionPending)return;
      stop('lifecycle_'+name); actions.requested+=1; var own=generation; actionPending=true;refresh(true);
      function submit() {
      if(own!==generation) {actionPending=false;refresh(true);return;}
      bridge.request('benchmark.action',{action:name},credentials,function(error,result) {
        actionPending=false;refresh(true);if(own!==generation)return;
        if(error || !result || !result.accepted) { actions.failed+=1; status(error?fault(error):'操作未接纳。'); }
        else { actions.accepted+=1; status('操作已接纳：'+name+'；完成状态由实际窗口行为确认。'); }
      });
      }
      if(nativeBusy())measurement.end('lifecycle_'+name,submit);else submit();
    }
    function connect(attempt) {
      if(typeof win.__souiNativePostMessage!=='function') {
        if(attempt<100) win.setTimeout(function(){connect(attempt+1);},100);
        else status('10 秒内未发现本地 Bridge；请使用性能 Host 打开此页。');
        return;
      }
      bridge.request('bridge.bootstrap',{protocol:'soui.bridge/1'},{},function(error,result) {
        if(error) {
          if(error.code==='E_CONTEXT_PENDING' && attempt<100) win.setTimeout(function(){connect(attempt+1);},100);
          else status(fault(error));
          return;
        }
        if(!result || typeof result.sessionId!=='string' || !result.sessionId || result.sessionId.length>256 ||
          typeof result.origin!=='string' || !result.origin || typeof result.token!=='string' || !result.token) { status('Bootstrap 身份字段无效。'); return; }
        credentials={sessionId:result.sessionId,origin:result.origin,token:result.token};
        bridge.request('benchmark.identity',{},credentials,function(identityError,value) {
          if(identityError || !value) { status(identityError?fault(identityError):'缺少 Host 身份。'); return; }
          identity=value; ui.identity.textContent=String(value.backend)+' · SDK '+String(value.sdkVersion)+' · '+String(value.buildIdentity)+
            ' · input trace '+String(value.inputTrace || 'off')+' · page '+String(value.pageHash);
          bridge.request('bridge.probe.report',{ready:true},credentials,function(probeError) {
            if(probeError) { status(fault(probeError)); return; }
            connected=true; status('已就绪。选择场景后手动运行或记录 30 秒。'); refresh(true);
          });
        });
      });
    }
    if(typeof win.requestAnimationFrame!=='function' || !context) { status('缺少 rAF 或 Canvas 2D，本页不能提供此测试。'); return; }
    resetCounters(); draw(); draws=0; liveStart=now(); refresh(true);
    for(index=0;index<60;index+=1) { var row=doc.createElement('p'); row.textContent='滚动行 '+(index+1)+' · 固定内容 / pointer 与 wheel 事件'; ui.scrollContent.appendChild(row); }
    ['keydown','keyup','compositionstart','compositionupdate','compositionend','input'].forEach(function(name){ui.input.addEventListener(name,inputEvent,false);});
    (win.PointerEvent?['pointermove','pointerdown','pointerup']:['mousemove','mousedown','mouseup']).forEach(function(name){ui.feedback.addEventListener(name,inputEvent,false);});
    ui.scroll.addEventListener('wheel',inputEvent,false); ui.scroll.addEventListener('scroll',inputEvent,false);
    win.addEventListener('blur',function(){invalidate('window_blur');},false);
    doc.addEventListener('visibilitychange',function(){if(doc.hidden)invalidate('document_hidden');},false);
    win.addEventListener('resize',function(){invalidate('viewport_changed');},false);
    win.addEventListener('beforeunload',function(){stop('navigation');win.clearTimeout(saveTimer);},false);
    ui.scene.onchange=function(){stop('scene_changed');scene=ui.scene.value;if(scene==='idle')stopFrames();else scheduleFrame();
      refresh(true);status('已暂停，点击运行或记录开始新场景。');};
    ui.payload.onchange=function(){stop('payload_changed');};
    ui.start.onclick=function(){begin(false);}; ui.stop.onclick=function(){stop('manual_stop');status('已暂停；旧 native 工作并未声称取消。');};
    ui.record.onclick=function(){begin(true);}; ui.fixed.onclick=startFixedWork; ui.save.onclick=saveReport;
    ui.second.onclick=function(){action('second-host');};ui.reload.onclick=function(){action('reload');};ui.hide.onclick=function(){action('hide-restore');};
    scheduleFrame(); connect(0);
  }
  return {utf8Bytes:utf8Bytes,createBridge:createBridge,createFixedWorkScheduler:createFixedWorkScheduler,createFixedBridgeWork:createFixedBridgeWork,
    createWorld:createWorld,stepWorld:stepWorld,createTicker:createTicker,
    stateHash:stateHash,frameStats:frameStats,createNativeMeasurement:createNativeMeasurement,mount:mount,
    constants:{messageLimit:LIMIT,durationMs:DURATION,seed:SEED,stepMs:STEP}};
}));
