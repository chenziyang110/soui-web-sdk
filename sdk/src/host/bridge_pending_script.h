#pragma once

namespace soui::sdk::detail {
// Arguments are supplied as escaped JSON literals by the native binding.
// Keep this function compatible with Chrome 49; the same source is exercised
// by the resolver contract tests, including real timer ownership.
// No leading line break: native code prefixes this expression with `return `.
inline constexpr const char* create_bridge_pending_script = R"JS((function(id, timeoutMs) {
  function error(code, message) {
    return JSON.stringify({requestId:id,ok:false,error:{code:code,message:message}});
  }
  if (window.__souiBridgeDocumentClosed)
    return error('E_DISPOSED', 'The document has closed.');
  var pending = window.__souiBridgePending;
  if (!pending) {
    pending = window.__souiBridgePending = Object.create(null);
    var close = function() {
      window.__souiBridgeDocumentClosed = true;
      Object.keys(pending).forEach(function(key) {
        pending[key].finish(JSON.stringify({requestId:key,ok:false,error:{code:'E_DISPOSED',
          message:'The document closed; native work was not cancelled.'}}));
      });
    };
    window.addEventListener('pagehide', close);
    window.addEventListener('beforeunload', close);
  }
  if (Object.prototype.hasOwnProperty.call(pending, id))
    return error('E_DUPLICATE_REQUEST', 'This document already has a pending request with this identity.');
  if (Object.keys(pending).length >= 384)
    return error('E_BUSY', 'The native resolver capacity is exhausted.');
  var resolve;
  var promise = new Promise(function(done) { resolve = done; });
  var entry = {finish:function(response) {
    if (pending[id] === entry) {
      delete pending[id];
      clearTimeout(entry.timer);
      resolve(response);
    }
    return promise;
  }};
  pending[id] = entry;
  entry.timer = setTimeout(function() {
    entry.finish(error('E_TIMEOUT', 'The native response wait timed out; running work was not cancelled.'));
  }, timeoutMs);
  return promise;
})
)JS";
}  // namespace soui::sdk::detail
