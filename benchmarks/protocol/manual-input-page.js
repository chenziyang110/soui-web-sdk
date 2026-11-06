(function () {
  'use strict';
  // ES5/Chrome49 manual feedback only. Local counts are NOT native input IDs,
  // injection acknowledgements or a deterministic replay. The separate manual
  // pixel marker checks observed content; it never supplies a native input ID.
  var config = window.souiManualInputConfig;
  if (!config || !/^[a-f0-9]{32}$/.test(config.run) || /^0+$/.test(config.run) ||
      (config.kind !== 'pointer_move' && config.kind !== 'wheel')) throw new Error('Manual run/kind required');
  var canvas = document.getElementById('feedback'), context = canvas && canvas.getContext('2d');
  if (!context) throw new Error('Manual feedback Canvas unavailable');
  var countLimit = 2147483647, pending = false, disposed = false;
  var state = {pointerEvents:0, wheelEvents:0, renders:0, invalidEvents:0,
    x:null, y:null, deltaX:null, deltaY:null, deltaMode:null, legacyWheelDelta:null,
    blurred:0, hidden:0, resized:0, overflow:false, geometryInvalid:false, focusAtStart:null};
  var markerBytes = new Uint8Array(16), markerImage = context.createImageData(64, 32);
  var markerNonce = parseInt(config.run.slice(24), 16) || 1, markerPixel;
  // Local reload observation only, never a native document ID or authorization.
  var pageEpoch = 0;
  try {
    var previousName = window.name, previousEpoch, nextEpoch = 1;
    if (typeof previousName !== 'string') throw new Error('Page epoch storage unavailable');
    if (previousName !== '') {
      previousEpoch = /^soui-manual-observation:([a-f0-9]{32}):([1-9][0-9]{0,4})$/.exec(previousName);
      if (!previousEpoch || /^0+$/.test(previousEpoch[1]) || Number(previousEpoch[2]) > 65535) throw new Error('Page epoch storage invalid');
      if (previousEpoch[1] === config.run) nextEpoch = Number(previousEpoch[2]) + 1;
    }
    if (nextEpoch > 65535) throw new Error('Page epoch capacity exhausted');
    var nextName = 'soui-manual-observation:' + config.run + ':' + nextEpoch;
    window.name = nextName;
    if (window.name !== nextName) throw new Error('Page epoch storage write unavailable');
    pageEpoch = nextEpoch;
  } catch (epochError) { state.overflow = true; }
  for (markerPixel = 3; markerPixel < markerImage.data.length; markerPixel += 4) markerImage.data[markerPixel] = 255;
  function drawManualMarker() {
    var revision = state.pointerEvents + state.wheelEvents, frame = state.renders;
    if (revision > 1048575 || frame > 65535) state.overflow = true;
    // Saturating marker fields never wrap into another apparently valid frame.
    // Normal event accounting, background color and feedback keep running.
    revision = Math.min(revision, 1048575); frame = Math.min(frame, 65535);
    var i, bit, crc = 65535, value, x, y, offset;
    markerBytes[0] = 167; markerBytes[1] = 77;
    for (i = 0; i < 4; ++i) markerBytes[2 + i] = (markerNonce >>> (i * 8)) & 255;
    for (i = 0; i < 3; ++i) markerBytes[6 + i] = (revision >>> (i * 8)) & 255;
    markerBytes[9] = frame & 255; markerBytes[10] = (frame >>> 8) & 255;
    markerBytes[11] = (state.overflow ? 1 : 0) | (state.geometryInvalid ? 2 : 0);
    markerBytes[14] = pageEpoch & 255; markerBytes[15] = (pageEpoch >>> 8) & 255;
    for (i = 0; i < 16; ++i) {
      if (i === 12 || i === 13) continue;
      crc ^= markerBytes[i] << 8;
      for (bit = 0; bit < 8; ++bit) crc = ((crc << 1) ^ ((crc & 32768) ? 4129 : 0)) & 65535;
    }
    markerBytes[12] = crc & 255; markerBytes[13] = (crc >>> 8) & 255;
    for (i = 0; i < 128; ++i) {
      value = (markerBytes[i >>> 3] & (1 << (i & 7))) ? 255 : 0;
      for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
        offset = (((i >>> 4) * 4 + y) * 64 + (i % 16) * 4 + x) * 4;
        markerImage.data[offset] = value; markerImage.data[offset + 1] = value; markerImage.data[offset + 2] = value;
      }
    }
    context.putImageData(markerImage, 8, 336);
  }
  var finite = function (value) { return typeof value === 'number' && isFinite(value); };
  function increment(key) {
    if (state[key] >= countLimit) { state.overflow = true; return; }
    state[key] += 1;
  }
  function geometry() {
    if (window.innerWidth !== 1100 || window.innerHeight !== 720 || window.devicePixelRatio !== 1) state.geometryInvalid = true;
  }
  function render() {
    pending = false;
    if (disposed) return;
    increment('renders');
    var color = (state.pointerEvents + state.wheelEvents) % 256;
    context.fillStyle = 'rgb(' + ((color * 73) % 256) + ',' + ((color * 151) % 256) + ',' + ((color * 29) % 256) + ')';
    context.fillRect(0, 0, 1100, 720);
    context.fillStyle = '#ffffff'; context.font = '24px sans-serif';
    context.fillText('Manual input / requested: ' + config.kind, 30, 48);
    context.fillText('Mouse events: ' + state.pointerEvents + ' / wheel events: ' + state.wheelEvents, 30, 90);
    context.fillText('Position: ' + state.x + ', ' + state.y, 30, 132);
    context.fillText('Wheel delta: ' + state.deltaX + ', ' + state.deltaY + ' / mode: ' + state.deltaMode +
      ' / legacy: ' + state.legacyWheelDelta, 30, 174);
    context.fillText('Canvas renders: ' + state.renders + ' / event counts are local JavaScript observations', 30, 216);
    context.fillText('按本轮说明完成采样；如有外部观察器，先等控制台观察结束，再保存、关窗。', 30, 258);
    context.fillText('Blur/hidden/resize: ' + state.blurred + '/' + state.hidden + '/' + state.resized +
      ' / overflow: ' + state.overflow + ' / geometry invalid: ' + state.geometryInvalid, 30, 300);
    if (state.x !== null && state.y !== null) {
      context.fillStyle = '#000000'; context.fillRect(state.x - 5, state.y - 5, 11, 11);
    }
    drawManualMarker();
  }
  function requestRender() {
    if (!disposed && !pending) { pending = true; window.requestAnimationFrame(render); }
  }
  function pointer(event) {
    increment('pointerEvents');
    if (finite(event.clientX) && finite(event.clientY)) { state.x = event.clientX; state.y = event.clientY; }
    else increment('invalidEvents');
    requestRender();
  }
  function wheel(event) {
    increment('wheelEvents');
    if (event.preventDefault) event.preventDefault();
    if (finite(event.deltaX) && finite(event.deltaY) && (event.deltaMode === 0 || event.deltaMode === 1 || event.deltaMode === 2)) {
      state.deltaX = event.deltaX; state.deltaY = event.deltaY; state.deltaMode = event.deltaMode;
      state.legacyWheelDelta = finite(event.wheelDelta) ? event.wheelDelta : null;
    } else if (finite(event.wheelDelta)) {
      state.deltaX = null; state.deltaY = null; state.deltaMode = null; state.legacyWheelDelta = event.wheelDelta;
    } else { increment('invalidEvents'); state.deltaX = state.deltaY = state.deltaMode = state.legacyWheelDelta = null; }
    requestRender();
  }
  canvas.addEventListener('mousemove', pointer, false);
  canvas.addEventListener('wheel', wheel, false);
  window.addEventListener('blur', function () { increment('blurred'); requestRender(); }, false);
  window.addEventListener('resize', function () { increment('resized'); geometry(); requestRender(); }, false);
  document.addEventListener('visibilitychange', function () { if (document.hidden) increment('hidden'); requestRender(); }, false);
  window.addEventListener('unload', function () { disposed = true; if (timer != null) window.clearTimeout(timer); }, false);
  state.focusAtStart = typeof document.hasFocus === 'function' ? !!document.hasFocus() : null;
  if (document.hidden) increment('hidden');
  geometry(); requestRender();
  window.souiManualInputResult = function () {
    return {schema:'soui.manual-input-page/v1', run_id:config.run, requested_kind:config.kind,
      counts:{pointer:state.pointerEvents, wheel:state.wheelEvents, renders:state.renders, invalid:state.invalidEvents},
      position:{x:state.x, y:state.y}, wheel:{deltaX:state.deltaX, deltaY:state.deltaY,
        deltaMode:state.deltaMode, legacyWheelDelta:state.legacyWheelDelta},
      lifecycle:{blur:state.blurred, hidden:state.hidden, resize:state.resized, focus_at_start:state.focusAtStart},
      overflow:state.overflow, geometry_invalid:state.geometryInvalid, render_pending:pending, device_pixel_ratio:window.devicePixelRatio,
      viewport:{width:window.innerWidth, height:window.innerHeight},
      deterministic_replay:false, native_input_ids_available:false, physical_latency_measured:false,
      foreground_validity:'unknown', workload_comparable:'unknown'};
  };
  var finish = document.getElementById('finish'), submissionStatus = document.getElementById('submission-status');
  if (!finish || !submissionStatus) throw new Error('Manual finish controls unavailable');
  var submitted = false, timer = null, submissionAttempt = 0;
  function status(text) { if (!disposed) submissionStatus.textContent = text; }
  finish.addEventListener('click', function () {
    if (submitted || disposed) return;
    submissionAttempt += 1;
    if (submissionAttempt > 4294967295) { finish.disabled = true; status('提交次数已超限，请正常关窗。'); return; }
    submitted = true; finish.disabled = true;
    // Capture once before bootstrap/transport work. No timer or native request
    // exists during input; acceptance is distinct from close-time persistence.
    var snapshot = JSON.stringify(window.souiManualInputResult()), credentials = null, settled = false;
    var requestPrefix = submissionAttempt === 1 ? 'manual' : 'manual:' + submissionAttempt;
    var now = function () { return window.performance.now(); }, deadline;
    function fail(error) {
      if (settled) return; settled = true;
      if (timer !== null) window.clearTimeout(timer);
      status('未确认接纳：' + String(error && error.message || error).slice(0, 160) + '；请正常关窗并检查原件。');
    }
    function request(method, params, id, done) {
      var packet = {requestId:id, sessionId:credentials ? credentials.sessionId : '',
        origin:credentials ? credentials.origin : '', token:credentials ? credentials.token : '', method:method, params:params};
      var remaining = Math.floor(deadline - now());
      if (remaining <= 0) { fail(new Error('提交超过共同 5 秒期限')); return; }
      if (credentials) packet.timeoutMs = remaining; // Public timeout minimum is 1 ms; never clamp past the original deadline.
      function received(raw) {
        if (settled || disposed) return;
        try {
          if (now() >= deadline) throw new Error('提交超过共同 5 秒期限');
          if (typeof raw !== 'string' || raw.length > 8192) throw new Error('原生回执无效');
          var response = JSON.parse(raw);
          if (!response || response.requestId !== id || response.ok !== true) throw new Error(response && response.error && response.error.code || '原生拒绝提交');
          done(response.result);
        } catch (error) { fail(error); }
      }
      try {
        var returned = window.__souiNativePostMessage(JSON.stringify(packet));
        if (returned && typeof returned.then === 'function') returned.then(received, fail);
        else received(returned);
      } catch (error) { fail(error); }
    }
    try {
      if (!window.performance || typeof window.performance.now !== 'function' || typeof window.__souiNativePostMessage !== 'function') throw new Error('原生 Bridge 不可用');
      deadline = now() + 5000;
      timer = window.setTimeout(function () { fail(new Error('提交超过共同 5 秒期限')); }, 5000);
      status('正在接纳快照；尚未落盘。');
      request('bridge.bootstrap', {protocol:'soui.bridge/1'}, requestPrefix + ':bootstrap', function (value) {
        if (!value || typeof value.sessionId !== 'string' || !value.sessionId || value.sessionId.length > 256 ||
            typeof value.origin !== 'string' || !value.origin || typeof value.token !== 'string' || !value.token) throw new Error('Bootstrap 回执无效');
        credentials = value;
        request('benchmark.manualInputReport', {snapshot_json:snapshot}, requestPrefix + ':report', function (result) {
          if (result && result.accepted === false && result.persisted === false && result.submission === 'pending' && result.retryable === true) {
            settled = true; window.clearTimeout(timer); timer = null;
            // This attempt was not admitted. Discard it; only a new user click
            // captures and submits current state with a fresh request identity.
            snapshot = null; submitted = false; finish.disabled = false;
            status('观察尚未结束，请继续本轮操作，稍后再点完成。'); return;
          }
          if (result && result.accepted === false && result.submission === 'unavailable' && typeof result.code === 'string')
            throw new Error(result.code);
          if (!result || result.accepted !== true || result.persisted !== false || result.submission !== 'accepted') throw new Error('接纳回执无效');
          settled = true; window.clearTimeout(timer);
          status(result.observationEnded === true ? '观察程序已结束，快照已接纳、尚未落盘；可正常关窗，是否合格以报告为准。' :
            '已接纳，尚未落盘；这不表示观察结束。如有外部观察器，须先等控制台观察结束，再正常关窗。');
        });
      });
    } catch (error) { fail(error); }
  }, false);
}());
