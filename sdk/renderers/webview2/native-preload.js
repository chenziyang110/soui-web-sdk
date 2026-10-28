(function () {
  'use strict';
  if (window !== window.top || window.location.href.split('#')[0] !== 'https://soui-webview2.invalid/index.html') return;
  var documentId = __SOUI_DOCUMENT_ID__;
  var pending = new Map();
  var closed = false;
  // Correlation only, never authority. Do not reuse an ID after timeout or post failure.
  var nextInvocationId = 1;
  var limits = [128, 128, 1];
  var used = [0, 0, 0];
  var encoder = new TextEncoder();
  function boundedString(value, maximum) {
    return typeof value === 'string' && value.length > 0 && value.length <= maximum && encoder.encode(value).length <= maximum;
  }
  function field(packet, key, maximum) {
    return Object.prototype.hasOwnProperty.call(packet, key) && boundedString(packet[key], maximum);
  }
  function category(packet) {
    if (!field(packet, 'sessionId', 256) || !field(packet, 'origin', 1024) || !field(packet, 'token', 512)) return 0;
    var params = packet.params;
    if (!params || typeof params !== 'object' || Array.isArray(params) || Object.keys(params).length !== 1) return 0;
    // Scheduling is not permission: native code independently checks shape,
    // credentials and capability. Page-supplied priority/control is ignored.
    if (packet.method === 'bridge.session.renew' && field(params, 'operationId', 128) && /^[A-Za-z0-9_.:-]+$/.test(params.operationId)) return 2;
    if ((packet.method === 'task.cancel' && field(params, 'taskId', 256)) ||
        (packet.method === 'task.unsubscribe' && field(params, 'subscriptionId', 256))) return 1;
    return 0;
  }
  function release(id, expected) {
    var entry = pending.get(id);
    if (!entry || (expected && entry !== expected)) return;
    pending.delete(id);
    clearTimeout(entry.timer);
    --used[entry.category];
    return entry;
  }
  function failAll() {
    if (closed) return;
    closed = true;
    pending.forEach(function (entry, id) { release(id, entry); entry.reject(new Error('Document closed.')); });
    window.chrome.webview.removeEventListener('message', receive);
  }
  function receive(event) {
    var data = event.data;
    if (closed || !data || data.documentId !== documentId) return;
    if (data.kind === 'event') {
      if (typeof window.__souiNativeDispatchEvent === 'function') window.__souiNativeDispatchEvent(data.data);
      return;
    }
    if (data.kind !== 'response' || typeof data.raw !== 'string') return;
    var entry = pending.get(data.requestId);
    if (!entry || data.invocationId !== entry.invocationId) return;
    release(data.requestId, entry);
    entry.resolve(data.raw);
  }
  window.chrome.webview.addEventListener('message', receive);
  window.addEventListener('pagehide', failAll);
  window.addEventListener('beforeunload', failAll);
  window.__souiNativePostMessage = function (request) {
    return new Promise(function (resolve, reject) {
      if (closed) { reject(new Error('Document closed.')); return; }
      if (typeof request !== 'string' || request.length > 65536 || encoder.encode(request).length > 65536) { reject(new Error('Message exceeds limit.')); return; }
      var packet;
      try { packet = JSON.parse(request); } catch (error) { reject(error); return; }
      var id = packet && packet.requestId;
      if (!boundedString(id, 512) || pending.has(id)) { reject(new Error('Invalid or duplicate requestId.')); return; }
      var lane = category(packet);
      if (used[lane] >= limits[lane]) { reject(new Error('Transport capacity exceeded.')); return; }
      var timeout = packet.timeoutMs === undefined ? 10000 : packet.timeoutMs;
      if (!Number.isInteger(timeout) || timeout < 1 || timeout > 60000) { reject(new Error('Invalid timeout.')); return; }
      if (!Number.isSafeInteger(nextInvocationId)) { reject(new Error('Invocation identity exhausted.')); return; }
      var invocationId = String(nextInvocationId++);
      var entry = {resolve: resolve, reject: reject, category: lane, invocationId: invocationId};
      entry.timer = setTimeout(function () {
        if (release(id, entry)) reject(new Error('Native response timed out; native work was not cancelled.'));
      }, timeout);
      pending.set(id, entry);
      ++used[lane];
      try { window.chrome.webview.postMessage(JSON.stringify({documentId: documentId, invocationId: invocationId, request: request})); }
      catch (error) { if (release(id, entry)) reject(error); }
    });
  };
})();
