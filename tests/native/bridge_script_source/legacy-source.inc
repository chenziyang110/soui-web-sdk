// Extracted legacy production bytes; see legacy-reference.json and generate.cjs.
namespace legacy_bridge_source {
namespace detail {
inline constexpr const char* create_bridge_pending_script = R"JS((function(id, timeoutMs, renewal) {
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
  renewal = renewal === true;
  var count = Object.keys(pending).filter(function(key) { return pending[key].renewal === renewal; }).length;
  if (count >= (renewal ? 1 : 384))
    return error('E_BUSY', 'The native resolver capacity is exhausted.');
  var resolve;
  var promise = new Promise(function(done) { resolve = done; });
  var entry = {renewal:renewal,finish:function(response) {
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
}
std::string json_string_literal(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\r': result += "\\r"; break;
      case '\n': result += "\\n"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20U) {
          static constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(hex[(character >> 4U) & 0x0FU]);
          result.push_back(hex[character & 0x0FU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  result.push_back('"');
  return result;
}

std::string bridge_resolution_script(const std::string& id, const std::string& response) {
  return "(function(){var p=window.__souiBridgePending;var id=" + json_string_literal(id)
      + ";if(!p||!p[id])return;return p[id].finish(" + json_string_literal(response) + ");})()";
}


std::string pending_source(const std::string& request_id, std::uint32_t timeout, bool renewal) {
  const std::string_view method = renewal ? "bridge.session.renew" : "benchmark.echo";
  const auto create = std::string{"return "} + detail::create_bridge_pending_script
        + "(" + json_string_literal(request_id) + "," + std::to_string(timeout) + ","
        + (method == "bridge.session.renew" ? "true" : "false") + ");";
  return create;
}
std::string immediate_source(const std::string& request_id, const std::string& response) {
  const auto resolve = "return " + bridge_resolution_script(request_id, response) + ";";
  return resolve;
}
}
