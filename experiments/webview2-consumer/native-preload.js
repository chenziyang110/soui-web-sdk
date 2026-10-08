(function () {
  'use strict';
  if (window !== window.top || window.location.href.split('#')[0] !== 'https://soui-webview2.invalid/index.html') return;
  var documentId = __SOUI_DOCUMENT_ID__;
  var pending = new Map();
  var closed = false;
  var maximum = 128;
  function failAll() {
    if (closed) return;
    closed = true;
    pending.forEach(function (entry) { clearTimeout(entry.timer); entry.reject(new Error('Document closed.')); });
    pending.clear();
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
    if (!entry) return;
    pending.delete(data.requestId);
    clearTimeout(entry.timer);
    entry.resolve(data.raw);
  }
  window.chrome.webview.addEventListener('message', receive);
  window.addEventListener('pagehide', failAll);
  window.addEventListener('beforeunload', failAll);
  window.__souiNativePostMessage = function (request) {
    return new Promise(function (resolve, reject) {
      if (closed) { reject(new Error('Document closed.')); return; }
      if (typeof request !== 'string' || request.length > 65536) { reject(new Error('Message exceeds limit.')); return; }
      var packet;
      try { packet = JSON.parse(request); } catch (error) { reject(error); return; }
      var id = packet && packet.requestId;
      if (typeof id !== 'string' || !id || id.length > 512 || pending.has(id)) { reject(new Error('Invalid or duplicate requestId.')); return; }
      if (pending.size >= maximum) { reject(new Error('Transport capacity exceeded.')); return; }
      var timeout = packet.timeoutMs === undefined ? 10000 : packet.timeoutMs;
      if (!Number.isInteger(timeout) || timeout < 1 || timeout > 60000) { reject(new Error('Invalid timeout.')); return; }
      var timer = setTimeout(function () {
        pending.delete(id);
        reject(new Error('Native response timed out.'));
      }, timeout);
      pending.set(id, {resolve: resolve, reject: reject, timer: timer});
      try { window.chrome.webview.postMessage(JSON.stringify({documentId: documentId, request: request})); }
      catch (error) { clearTimeout(timer); pending.delete(id); reject(error); }
    });
  };
})();
