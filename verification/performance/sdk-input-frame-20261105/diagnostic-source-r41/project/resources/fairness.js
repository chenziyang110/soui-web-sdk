(function () {
  'use strict';
  // The same public preload envelope/bootstrap/probe as the real capacity page.
  // ES5 + Promise; no alternative transport, synthetic grant, rAF, or retries.
  var grant = null, requestId = 0, pending = 0, control = false, nativeUnknown = false;
  var payload = new Array(4097).join('x'), stopping = false, lanes = [], loadDue = 0, lastReply = 0;
  var firstReplyResolve, firstReplyReject, firstReply = new Promise(function(resolve,reject){firstReplyResolve=resolve;firstReplyReject=reject;});
  var result = {schema:'soui.r41-owner-fairness-page/v1', payloadBytes:4096, inflightLimit:7,
    attempted:0, success:0, failed:0, timeouts:0, mismatches:0, inflight:0, peakInflight:0,
    marks:[], times:{}, error:null, valid:false, nativeTerminalUnknown:false,
    hardwareInputMeasured:false, physicalPresentationMeasured:false, performanceVerified:false,
    finalDoneControlReplyConfirmed:false,inputArmed:false,inputFinishFenced:false,finishFence:null,maxLoadNoReplyMs:0};
  function now() { return performance.now(); }
  function failure(code) { var error = new Error(code); error.code = code; return error; }
  function require(ok, code) { if (!ok) throw failure(code); }
  function bytes(text) { return unescape(encodeURIComponent(text)).length; }
  function call(method, params) {
    var echo = method === 'fairness.echo', start = now(), due = start + 5000;
    if (pending >= 8 || (!echo && control)) return Promise.reject(failure('E_LOCAL_CAPACITY'));
    var id = 'owner:' + (++requestId), envelope = {requestId:id, sessionId:grant ? grant.sessionId : '',
      origin:grant ? grant.origin : '', token:grant ? grant.token : '', method:method, params:params};
    if (method !== 'bridge.bootstrap') envelope.timeoutMs = 5000;
    var wire = JSON.stringify(envelope);
    if (bytes(wire) > 65536) return Promise.reject(failure('E_LOCAL_BYTES'));
    pending += 1; if (!echo) control = true;
    return new Promise(function (resolve, reject) {
      var settled = false, attempted = false;
      var timer;
      function expire() {
        if (now() < due) timer = setTimeout(expire, Math.ceil(due-now()));
        else finish(failure('E_PAGE_TIMEOUT'));
      }
      timer = setTimeout(expire, Math.max(0, Math.ceil(due-now())));
      function finish(error, raw) {
        if (settled) return;
        settled = true; clearTimeout(timer); pending -= 1; if (!echo) control = false;
        var response;
        if (!error) try {
          require(now() < due, 'E_PAGE_TIMEOUT');
          require(typeof raw === 'string' && bytes(raw) <= 65536, 'E_RESPONSE_BYTES');
          response = JSON.parse(raw);
          require(response && response.requestId === id, 'E_RESPONSE_ID');
          if (response.ok !== true) throw failure(response.error && response.error.code || 'E_NATIVE');
        } catch (parseError) { error = parseError; }
        if (error) { if (attempted) nativeUnknown = true; reject(error); }
        else resolve(response.result);
      }
      try {
        require(typeof window.__souiNativePostMessage === 'function', 'E_PRELOAD_MISSING');
        attempted = true;
        Promise.resolve(window.__souiNativePostMessage(wire)).then(function (raw) { finish(null, raw); }, function (error) { finish(error || failure('E_TRANSPORT')); });
      } catch (error) { finish(error); }
    });
  }
  function waitUntil(due) {
    return new Promise(function (resolve) {
      function check() { if (now() >= due) resolve(); else setTimeout(check, Math.ceil(due - now())); }
      check();
    });
  }
  function within(promise, due) {
    return new Promise(function (resolve, reject) {
      var settled = false;
      var timer;
      function expire() {
        if (settled) return;
        if (now() < due) timer = setTimeout(expire, Math.ceil(due-now()));
        else { settled = true; reject(failure('E_DRAIN_DEADLINE')); }
      }
      timer = setTimeout(expire, Math.max(0, Math.ceil(due-now())));
      promise.then(function (value) {
        if (settled) return; settled = true; clearTimeout(timer);
        if (now() >= due) reject(failure('E_DRAIN_DEADLINE')); else resolve(value);
      }, function (error) { if (!settled) { settled = true; clearTimeout(timer); reject(error); } });
    });
  }
  function mark(stage) {
    var requested = now();
    return call('fairness.mark', {stage:stage}).then(function (answer) {
      require(answer && answer.stage === stage && typeof answer.nativeQpc === 'string' && /^[1-9][0-9]*$/.test(answer.nativeQpc), 'E_NATIVE_MARK');
      result.marks.push({stage:stage, requestedMs:requested, responseMs:now(), nativeQpc:answer.nativeQpc});
    });
  }
  function lane() {
    return new Promise(function (finished) {
      function next() {
        if (stopping || now() >= loadDue) { finished(); return; }
        if (result.attempted >= 1000000) { stopping = true; result.error = 'E_COUNT_BOUND'; finished(); return; }
        var sequence = result.attempted++;
        result.inflight += 1; result.peakInflight = Math.max(result.peakInflight, result.inflight);
        call('fairness.echo', {sequence:sequence, payload:payload}).then(function (answer) {
          if (!answer || answer.sequence !== sequence || answer.payload !== payload) {
            result.mismatches += 1; throw failure('E_ECHO_MISMATCH');
          }
          var received=now(),bounded=Math.min(received,loadDue);
          require(received>=lastReply,'E_CLOCK');
          result.success += 1; result.inflight -= 1;
          if(lastReply&&bounded>=lastReply)result.maxLoadNoReplyMs=Math.max(result.maxLoadNoReplyMs,bounded-lastReply);
          lastReply=bounded;
          if(firstReplyResolve){firstReplyResolve();firstReplyResolve=null;firstReplyReject=null;result.times.firstEchoReplyMs=received;}
          next(); // Do not retain an ever-growing chain of completed requests.
        }).catch(function (error) {
          result.failed += 1; result.inflight -= 1; stopping = true;
          if (error.code === 'E_PAGE_TIMEOUT' || error.code === 'E_TIMEOUT') result.timeouts += 1;
          result.error = error.code || 'E_ECHO_FAILURE'; finished();
          if(firstReplyReject){firstReplyReject(error);firstReplyResolve=null;firstReplyReject=null;}
        });
      }
      next();
    });
  }
  function save(valid, error) {
    stopping = true;
    result.error = result.error || (error && error.code) || (error ? 'E_PAGE_FAILURE' : null);
    result.valid = valid && result.inputArmed && result.inputFinishFenced && !result.error && !nativeUnknown && result.inflight === 0 && result.attempted === result.success && result.success > 0;
    result.nativeTerminalUnknown = nativeUnknown || result.inflight !== 0;
    result.times.reportRequestedMs = now();
    // Snapshot once. Later terminal callbacks never rewrite the saved evidence.
    var snapshot = JSON.parse(JSON.stringify(result)); window.ownerFairnessPageResult = snapshot;
    document.title = snapshot.valid ? 'Diagnostic work completed' : 'Diagnostic work failed';
    return call('fairness.report', snapshot).then(function (answer) {
      require(answer && answer.recorded === true, 'E_REPORT_RECEIPT');
      return call('fairness.done', {}); // Entry proves report response was received.
    });
  }
  result.times.bootstrapRequestedMs = now();
  function inputReady(){
    require(now()-result.times.bootstrapRequestedMs<25000,'E_COLD_INPUT_READY_DEADLINE');
    return call('fairness.inputReady',{}).then(function(answer){
      require(answer&&typeof answer.ready==='boolean','E_INPUT_READY_RESPONSE');
      if(answer.ready)return;
      return waitUntil(now()+100).then(inputReady); // Cold only, before pre-idle/load.
    });
  }
  call('bridge.bootstrap', {protocol:'soui.bridge/1'}).then(function (value) {
    grant = value;
    require(grant && typeof grant.sessionId === 'string' && typeof grant.origin === 'string' && typeof grant.token === 'string', 'E_GRANT');
    return call('bridge.probe.report', {ready:true});
  }).then(inputReady).then(function () { return mark(1); }).then(function () { return waitUntil(now()+2000); })
    .then(function () { return mark(2); }).then(function () {
      result.times.loadStartedMs = now();lastReply=result.times.loadStartedMs;loadDue = result.times.loadStartedMs + 30000;
      result.times.loadDeadlineMs = loadDue;
      for (var index=0; index<7; index+=1) lanes.push(lane());
      return firstReply.then(function(){result.times.loadActiveRequestedMs=now();return call('fairness.loadActive',{});})
        .then(function(answer){require(answer&&answer.armed===true&&typeof answer.nativeQpc==='string'&&/^[1-9][0-9]*$/.test(answer.nativeQpc),'E_LOAD_ACTIVE');
          result.inputArmed=true;result.loadActiveNativeQpc=answer.nativeQpc;result.times.loadActiveAnsweredMs=now();
          return waitUntil(result.times.loadStartedMs+29000).then(function(){
            var fence=result.finishFence={requestedMs:now(),responseMs:null,finished:null,cohortEndQpc:null,observedQpc:null,error:null};
            return call('fairness.inputFinished',{}).then(function(reply){
              fence.responseMs=now();
              require(reply&&typeof reply.finished==='boolean'&&typeof reply.cohortEndQpc==='string'&&/^(0|[1-9][0-9]*)$/.test(reply.cohortEndQpc)&&
                typeof reply.observedQpc==='string'&&/^[1-9][0-9]*$/.test(reply.observedQpc),'E_INPUT_FINISH_FENCE_SHAPE');
              fence.finished=reply.finished;fence.cohortEndQpc=reply.cohortEndQpc;fence.observedQpc=reply.observedQpc;
              require(fence.requestedMs<loadDue&&fence.responseMs<loadDue,'E_INPUT_FINISH_FENCE_LATE');
              require(reply.finished===true,'E_INPUT_FINISH_FENCE_FALSE');result.inputFinishFenced=true;
            }).catch(function(error){
              // Lock invalid, but never stop the offered flood early or retry.
              fence.error=error.code||'E_INPUT_FINISH_FENCE';result.error=result.error||fence.error;
            });
          }).then(function(){return waitUntil(loadDue);});});
    }).then(function () {
      stopping = true; result.times.stopSendingMs = now();
      result.maxLoadNoReplyMs=Math.max(result.maxLoadNoReplyMs,loadDue-lastReply);
      // A single control may overlap seven echoes: total <=8.
      // Its native QPC is the real boundary, not the JS stop instant.
      return mark(3);
    }).then(function () { return within(Promise.all(lanes), loadDue+5000); })
    .then(function () {
      result.times.drainedMs = now();
      require(result.inflight===0 && !result.error && !nativeUnknown, 'E_WORK_UNQUALIFIED');
      return mark(4);
    }).then(function () { return waitUntil(now()+2000); }).then(function () { return mark(5); })
    .then(function () { return save(true); }, function (error) { return save(false,error); })
    .catch(function () {
      // No retry or invented native cancellation. The native absolute watchdog
      // closes a failed/undelivered report; the owned supervisor retains it.
      document.title = 'Diagnostic transport/report failed';
    });
}());
