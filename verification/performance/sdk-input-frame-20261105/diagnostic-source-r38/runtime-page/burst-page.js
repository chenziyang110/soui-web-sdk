(function () {
  'use strict';
  var seed = window.souiBurstConfig;
  var query = seed ? '?run=' + seed.run + '&kind=' + seed.kind : (location.search || location.hash.replace(/^#/, '?'));
  var runMatch = /(?:^|[?&])run=([a-f0-9]{32})(?:&|$)/.exec(query);
  var kindMatch = /(?:^|[?&])kind=(pointer_move|wheel)(?:&|$)/.exec(query);
  if (!runMatch || !kindMatch || kindMatch[1] !== 'pointer_move' || window.devicePixelRatio !== 1) throw Error('Frozen pointer64/run/DPR1 required');
  var run = runMatch[1], nonce = parseInt(run.slice(24), 16) || 1;
  var coldDeadline = window.performance.now() + 5000;
  var canvas = document.getElementById('feedback'), context = canvas.getContext('2d');
  var mask = new Uint8Array(8), dirtyMask = new Uint8Array(8), firstIds = new Array(64), firstCalls = new Array(64), firstCallbacks = new Array(64);
  var count = 0, frame = 0, pending = false, initialized = false, binding = false, installedNotified = false, initializationStarted = false;
  var callbackCount = 0, callCount = 0, ledger = [], calls = [], limit = 128, overflow = false;
  var invalid = false, rejected = 0, repeated = 0, outside = 0, firstFault = null;
  var submitted = false, fallbackTimer = null, fallbackExpired = false, credentials = null;
  var listenerClosed = false, closeReason = null;
  var listenerOpened = false;
  var cold = null, deferred = null;
  for (var n = 0; n < 64; ++n) firstIds[n] = firstCalls[n] = firstCallbacks[n] = 0;
  function finite(v) { return typeof v === 'number' && isFinite(v); }
  function fail(code, row) { invalid = true; ++rejected; if (firstFault === null) firstFault = {code: code, row: row}; }
  function readNative(kind) {
    // This is the first operation in receive(): no Promise, Bridge, clock or
    // coordinate lookup precedes the synchronous native getter call.
    var ordinal = ++callCount, id = 0, completed = false;
    try {
      var observed = window.__souiSyncInputId();
      if (!finite(observed) || observed !== Math.floor(observed) || observed < 0 || observed > 2147483647) throw Error('Native input ID invalid');
      id = observed; completed = true;
    } catch (error) { fail('getter-result', [ordinal, kind]); }
    if (calls.length < limit) calls.push([ordinal, kind, id, completed ? 1 : 0]);
    else { overflow = true; fail('calls-overflow', [ordinal, kind]); }
    return {ordinal: ordinal, id: id, completed: completed};
  }
  function schedule() { if (initialized && !pending) { pending = true; requestAnimationFrame(render); } }
  function render() {
    pending = false;
    if (!initialized) return;
    if (frame >= 65535) throw Error('Marker frame capacity exhausted');
    ++frame;
    // The wire ACK is the number of actual distinct slots, never a prefix
    // inferred from the largest coordinate. The bitmap identifies each slot.
    var color = 'rgb(' + (count & 255) + ',' + ((count >>> 8) & 255) + ',' + ((count >>> 16) & 255) + ')';
    context.fillStyle = color;
    context.fillRect(0, 0, 1100, 8); context.fillRect(0, 8, 8, 52);
    context.fillRect(72, 8, 1028, 52); context.fillRect(0, 60, 1100, 660);
    context.fillRect(8, 40, 64, 4); // Original content probe [10,42].
    context.fillStyle = '#ffffff'; context.font = '24px sans-serif';
    context.fillText('Input slots ' + count + ' / callbacks ' + callbackCount, 100, 500);
    var bytes = new Uint8Array(24), i, bit, crc = 65535;
    bytes[0] = 167; bytes[1] = 83;
    for (i = 0; i < 4; ++i) bytes[2 + i] = ((invalid ? 0 : nonce) >>> (8 * i)) & 255;
    bytes[6] = count; bytes[9] = frame & 255; bytes[10] = (frame >>> 8) & 255; bytes[11] = 2; bytes[14] = count;
    for (i = 0; i < 8; ++i) bytes[16 + i] = mask[i];
    for (i = 0; i < 24; ++i) {
      if (i === 12 || i === 13) continue;
      crc ^= bytes[i] << 8;
      for (bit = 0; bit < 8; ++bit) crc = ((crc << 1) ^ ((crc & 32768) ? 4129 : 0)) & 65535;
    }
    bytes[12] = crc & 255; bytes[13] = (crc >>> 8) & 255;
    for (i = 0; i < 128; ++i) {
      context.fillStyle = (bytes[i >>> 3] & (1 << (i & 7))) ? '#ffffff' : '#000000';
      context.fillRect(8 + (i % 16) * 4, 8 + Math.floor(i / 16) * 4, 4, 4);
    }
    context.fillStyle = '#ffffff';
    for (i = 0; i < 64; ++i) if (dirtyMask[i >>> 3] & (1 << (i & 7))) context.fillRect(8 + (i % 16) * 4, 44 + Math.floor(i / 16) * 4, 4, 4);
    for (i = 0; i < 8; ++i) dirtyMask[i] = 0;
  }
  function report() {
    var hex = '';
    for (var i = 0; i < 8; ++i) hex += (mask[i] < 16 ? '0' : '') + mask[i].toString(16);
    return {schema: 'soui.burst-page/v4', protocol: 'sync-input-ack64/v4', mode: 'native-dispatch-scope-required', run_id: run, nonce: nonce, kind: 'pointer_move',
      expected_inputs: 64, latest: count, observed: count, rejected: rejected, invalid: invalid, repeated_coordinates: repeated, outside_coordinates: outside,
      device_pixel_ratio: 1, mask_hex: hex, marker_region: [8, 8, 64, 52], content_probe: [10, 42], mask_region: [8, 44, 64, 16],
      binding_ready: binding, initial_zero_drawn: initialized, callback_count: callbackCount, getter_call_count: callCount,
      listener_closed: listenerClosed, listener_close_reason: closeReason,
      listener_opened: listenerOpened, listener_opened_after_getter_call: listenerOpened ? 1 : null,
      listener_closed_after_callback: listenerClosed ? callbackCount : null, listener_closed_after_getter_call: listenerClosed ? callCount : null,
      ledger_limit: limit, overflow: overflow, dom_ledger_complete: !overflow && ledger.length === callbackCount,
      calls_complete: !overflow && calls.length === callCount, dom_ledger: ledger.slice(0), getter_calls: calls.slice(0),
      dom_columns: ['callback', 'getter_call', 'native_input_id', 'js_now_ms', 'client_x', 'client_y', 'coordinate_sequence', 'outcome', 'is_trusted', 'is_trusted_typeof', 'is_trusted_property_present'],
      absent_is_trusted_remains_unknown: true, arbitrary_page_native_origin_certified: false,
      call_columns: ['ordinal', 'kind', 'returned_id', 'completed'], call_kinds: {outside: 0, dom: 1, deferred_engine_task: 2},
      dom_outcomes: {first_slot: 1, repeated_coordinate: 2, outside_grid: 3, invalid: 4},
      first_slot_input_ids: firstIds.slice(0), first_slot_calls: firstCalls.slice(0), first_slot_callbacks: firstCallbacks.slice(0),
      cold_outside: cold, deferred_audit: deferred, first_fault: firstFault,
      clock_scope: 'Uncalibrated JS performance.now; never subtract from native QPC',
      native_tag_observed: false, native_dom_one_to_one_qualified: false, physical_feedback_qualified: false};
  }
  window.souiBurstResult = report;
  function request(method, params, id, auth, deadline, done) {
    var remaining = Math.floor(deadline - window.performance.now());
    if (remaining <= 0) { done(Error('Submission deadline elapsed')); return; }
    var packet = {requestId: id, sessionId: auth ? auth.sessionId : '', origin: auth ? auth.origin : '', token: auth ? auth.token : '', method: method, params: params};
    if (auth) packet.timeoutMs = remaining;
    var settled = false;
    function receive(raw) {
      if (settled) return; settled = true;
      try {
        if (window.performance.now() >= deadline || typeof raw !== 'string' || raw.length > 8192) throw Error('Reply size/deadline');
        var value = JSON.parse(raw);
        if (!value || value.requestId !== id || value.ok !== true) throw Error('Request refused');
        done(null, value.result);
      } catch (error) { done(error); }
    }
    try {
      var value = window.__souiNativePostMessage(JSON.stringify(packet));
      if (value && typeof value.then === 'function') value.then(receive, function (error) { if (!settled) { settled = true; done(error); } }); else receive(value);
    } catch (error) { if (!settled) { settled = true; done(error); } }
  }
  function maybeSubmit(reason) {
    if (submitted || !credentials || (!invalid && (!deferred || !deferred.done || (count !== 64 && !fallbackExpired)))) return;
    submitted = true;
    // Close admission before freezing the snapshot. Later UI/native inputs are
    // still recorded externally, but cannot silently extend this DOM ledger.
    canvas.removeEventListener('mousemove', receive, false);
    listenerClosed = true; closeReason = reason;
    if (fallbackTimer !== null) window.clearTimeout(fallbackTimer);
    var rows = [];
    for (var i = 0; i < ledger.length; ++i) {
      if (!finite(ledger[i][4]) || !finite(ledger[i][5])) return; // Never invent finite coordinates for the legacy transport.
      rows.push({x: ledger[i][4], y: ledger[i][5]});
    }
    var snapshot = JSON.stringify({schema: 'soui.input-coordinate-page-witness/v1', run_id: run, diagnostic_only: true, reason: reason,
      rows: rows, overflow: overflow, invalid: invalid, first_invalid: firstFault, final_sequence: count, rejected: rejected,
      sync_input_report: report(), performanceGate: false, releaseReady: false});
    // All strings are fixed ASCII; both original Host wire bounds are preserved.
    if (snapshot.length > 24576 || JSON.stringify({snapshot_json: snapshot}).length > 32768) return;
    request('benchmark.coordinateWitness', {snapshot_json: snapshot}, 'sync-input-report:' + run, credentials,
      window.performance.now() + 5000, function () {});
  }
  function receive(event) {
    if (listenerClosed) return;
    var observed = readNative(1);
    ++callbackCount;
    var trustRaw = -1, trustType = 'exception', trustPresent = -1, trustAllowed = false;
    try {
      var trustValue = event.isTrusted;
      trustType = typeof trustValue; trustPresent = ('isTrusted' in event) ? 1 : 0;
      trustRaw = trustValue === true ? 1 : trustValue === false ? 0 : -1;
      // This fixed old binding has no property. Absence remains unknown, never
      // true; the offline exact native-context/tag gates remain mandatory.
      trustAllowed = trustValue === true || (trustValue === undefined && trustPresent === 0);
    } catch (error) { trustAllowed = false; }
    var x = event.clientX, y = event.clientY, now = window.performance.now(), next = null, outcome = 4;
    if (!finite(x) || !finite(y) || Math.abs(x) > 65535 || Math.abs(y) > 65535 || !finite(now) || now < 0 || now > 1000000000) {
      fail('coordinate-or-clock', [callbackCount]); x = finite(x) ? x : null; y = finite(y) ? y : null; now = finite(now) ? now : null;
    } else if (!binding || !initialized || !observed.completed || !trustAllowed) fail('untrusted-or-not-ready', [callbackCount, observed.ordinal]);
    else {
      now = Math.round(now * 1000) / 1000;
      var gx = x - 100, gy = y - 100;
      if (gx < 0 || gx >= 1000 || gy < 0 || gy >= 100 || gx !== Math.floor(gx) || gy !== Math.floor(gy)) { outcome = 3; ++outside; }
      else {
        next = gy * 1000 + gx + 1;
        if (next > 64) { outcome = 3; ++outside; }
        else if (mask[(next - 1) >>> 3] & (1 << ((next - 1) & 7))) { outcome = 2; ++repeated; }
        else if (observed.id === 0) fail('first-slot-without-native-input-id', [callbackCount, observed.ordinal]);
        else {
          // Tag0 is not guessed away. A nonzero actual ID is necessary here;
          // offline first-setter T1/tag matching must still establish this slot.
          mask[(next - 1) >>> 3] |= 1 << ((next - 1) & 7); dirtyMask[(next - 1) >>> 3] |= 1 << ((next - 1) & 7);
          firstIds[next - 1] = observed.id; firstCalls[next - 1] = observed.ordinal; firstCallbacks[next - 1] = callbackCount;
          ++count; outcome = 1;
        }
      }
    }
    var row = [callbackCount, observed.ordinal, observed.id, now, x, y, next, outcome, trustRaw, trustType, trustPresent];
    if (ledger.length < limit) ledger.push(row); else { overflow = true; fail('dom-overflow', [callbackCount]); }
    if (outcome === 1 && fallbackTimer === null && !submitted) fallbackTimer = window.setTimeout(function () { fallbackExpired = true; maybeSubmit('first-slot-plus-1000ms'); }, 1000);
    schedule(); maybeSubmit(invalid ? 'first-invalid' : 'all-slots-and-native-audit');
  }
  // Only the consumer's once-posted Engine task calls this. Page code does not
  // assume that a timer/microtask necessarily lies outside an input scope.
  window.__souiSyncDeferredAudit = function () {
    if (deferred !== null) { fail('duplicate-deferred-audit', null); maybeSubmit('duplicate-audit'); return JSON.stringify({done: false}); }
    var observed = readNative(2);
    deferred = {done: observed.completed, getter_call: observed.ordinal, returned_id: observed.id, page_asserts_outside_scope: false};
    maybeSubmit('native-engine-task-audit');
    return JSON.stringify(deferred);
  };
  window.__souiSyncInputWitnessReady = false;
  function initializeIfReady() {
    // The sole real bootstrap response and the native installation notification
    // may arrive in either order. Neither alone admits DOM input or draws zero.
    if (initializationStarted || !credentials || !installedNotified) return;
    initializationStarted = true;
    if (window.performance.now() >= coldDeadline || typeof window.__souiSyncInputId !== 'function') { fail('cold-binding-not-installed', null); return; }
    binding = true;
    var observed = readNative(0); cold = {getter_call: observed.ordinal, returned_id: observed.id, completed: observed.completed};
    if (!observed.completed || observed.id !== 0 || invalid) { fail('cold-outside-not-zero', null); maybeSubmit('cold-outside-failed'); return; }
    context.fillStyle = '#000000'; context.fillRect(8, 8, 64, 52);
    initialized = true; canvas.addEventListener('mousemove', receive, false); listenerOpened = true;
    render(); window.__souiSyncInputWitnessReady = true;
  }
  window.__souiNativeDispatchEvent = function (event) {
    if (!event || event.type !== 'soui.sync-input-witness.installed' || event.run_id !== run || event.request_id !== 'sync-input-witness-ready:' + run) return;
    installedNotified = true;
    initializeIfReady();
  };
  // A document has exactly one authenticated Bridge session. The native owner
  // captures this same bootstrap's view/receipt, then installs on a later Engine
  // opportunity and notifies this page without requesting another bootstrap.
  request('bridge.bootstrap', {protocol: 'soui.bridge/1'}, 'sync-input-witness-ready:' + run, null, coldDeadline,
    function (error, value) {
      if (error || !value || typeof value.sessionId !== 'string' || !value.sessionId || typeof value.origin !== 'string' || typeof value.token !== 'string') { fail('listener-ready-bootstrap', null); return; }
      credentials = value;
      initializeIfReady();
    });
}());
