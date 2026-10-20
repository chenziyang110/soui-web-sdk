#include "protocol.h"
#include "../runtime/platform/strict_utf8.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace soui::desktop::detail {
namespace {
std::string file_uri(const std::filesystem::path& path) {
  const auto input = path.generic_u8string();
  constexpr char hex[] = "0123456789ABCDEF";
  std::string output = "file://";
#ifdef _WIN32
  output += '/';
#endif
  for (const unsigned char value : input) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '-' || value == '_' ||
        value == '.' || value == '~' || value == '/' || value == ':') {
      output += static_cast<char>(value);
    } else {
      output += '%';
      output += hex[value >> 4U];
      output += hex[value & 15U];
    }
  }
  return output;
}

bool decode_file_uri(std::string_view input, std::string& output) {
  input = input.substr(0, input.find('#'));
  if (input.size() < 8 || input.substr(0, 8) != "file:///" ||
      input.find('?') != std::string_view::npos) return false;
  input.remove_prefix(7);
  const auto nibble = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < input.size(); ++i) {
    unsigned char value = static_cast<unsigned char>(input[i]);
    if (value == '%') {
      if (i + 2 >= input.size() || nibble(input[i + 1]) < 0 || nibble(input[i + 2]) < 0) return false;
      value = static_cast<unsigned char>((nibble(input[i + 1]) << 4) | nibble(input[i + 2]));
      i += 2;
    }
    if (!value || value < 32 || value == 127 || value == '\\') return false;
    output += static_cast<char>(value);
  }
  return soui::runtime::detail::valid_utf8(output);
}
}  // namespace

result resolve_document(const std::filesystem::path& root,
                        const std::filesystem::path& relative, document& selected) {
  try {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
      return result::failure("invalid_entry", "Entry must be a relative HTML path inside the resource root.");
    for (const auto& part : relative) {
      if (part == "..") return result::failure("invalid_entry", "Parent traversal is not allowed.");
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error))
      return result::failure("invalid_root", "Resource root must be an existing local directory.");
    const auto candidate = std::filesystem::canonical(canonical_root / relative, error);
    if (error || !std::filesystem::is_regular_file(candidate, error))
      return result::failure("invalid_entry", "Entry must be an existing regular file.");
    auto root_part = canonical_root.begin();
    auto path_part = candidate.begin();
    for (; root_part != canonical_root.end(); ++root_part, ++path_part) {
      if (path_part == candidate.end() || *root_part != *path_part)
        return result::failure("invalid_entry", "Entry resolves outside the resource root.");
    }
    auto extension = candidate.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".html" && extension != ".htm")
      return result::failure("invalid_entry", "Entry must have an HTML extension.");
    const auto bytes = candidate.generic_u8string();
    if (bytes.size() > 16384 || !soui::runtime::detail::valid_utf8(bytes))
      return result::failure("invalid_entry", "Entry path must be bounded UTF-8.");
    selected = {candidate, file_uri(candidate)};
    return result::success();
  } catch (...) {
    return result::failure("invalid_entry", "Unable to resolve the local application document.");
  }
}

bool same_document_uri(std::string_view actual, std::string_view selected) noexcept {
  try {
    if (actual.size() > 65536 || selected.size() > 65536) return false;
    std::string left, right;
    if (!decode_file_uri(actual, left) || !decode_file_uri(selected, right)) return false;
#ifdef _WIN32
    // Fold only the drive letter. Windows directories may be case-sensitive;
    // folding the remaining path could trust a distinct A.html / a.html file.
    for (auto* value : {&left, &right})
      if (value->size() >= 3 && (*value)[0] == '/' && (*value)[2] == ':' &&
          (*value)[1] >= 'A' && (*value)[1] <= 'Z') (*value)[1] += 'a' - 'A';
#endif
    return left == right;
  } catch (...) { return false; }
}

bool valid_name(std::string_view name) noexcept {
  if (name.empty() || name.size() > 64) return false;
  for (const unsigned char value : name)
    if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '.' || value == '-'))
      return false;
  return true;
}

nlohmann::json parse_bounded_json(std::string_view text, std::size_t maximum) {
  if (text.size() > maximum || !soui::runtime::detail::valid_utf8(text))
    throw std::invalid_argument("Invalid JSON encoding or size");
  return nlohmann::json::parse(text.begin(), text.end(),
    [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
      if (depth > 32) throw std::invalid_argument("JSON nesting limit exceeded");
      return true;
    });
}

std::string make_preload(std::string_view token) {
  // Token is closure-private, generated by the native CSPRNG for this navigation.
  return "(function(){'use strict';if(window!==window.top)return;const token=" +
    nlohmann::json(token).dump() + R"JS(;
const pending=new Map(),listeners=new Map();let nextId=0,live=true;
function nativePost(value){
  const text=JSON.stringify(value);
  if(new TextEncoder().encode(text).length>65536)throw new Error('Bridge request is too large');
  const stack=[[JSON.parse(text),0]];
  while(stack.length){const item=stack.pop();if(item[1]>32)throw new Error('Bridge JSON is too deeply nested');
    if(item[0]&&typeof item[0]==='object')for(const key of Object.keys(item[0]))stack.push([item[0][key],item[1]+1]);}
  if(window.chrome&&window.chrome.webview)window.chrome.webview.postMessage(text);
  else if(window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.soui)
    window.webkit.messageHandlers.soui.postMessage(text);
  else throw new Error('Native Bridge is unavailable');
}
function invoke(method,params){
  return new Promise(function(resolve,reject){
    if(!live){reject(new Error('Document session is closed'));return;}
    if(typeof method!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(method)){
      reject(new Error('Invalid method'));return;
    }
    if(pending.size>=256||nextId>=Number.MAX_SAFE_INTEGER){
      reject(new Error('Bridge request limit reached'));return;
    }
    const id=++nextId;
    const timer=setTimeout(function(){pending.delete(id);reject(new Error('Native request timed out'));},30000);
    pending.set(id,{resolve:resolve,reject:reject,timer:timer});
    try{nativePost({v:1,token:token,id:id,method:method,params:params===undefined?null:params});}
    catch(error){clearTimeout(timer);pending.delete(id);reject(error);}
  });
}
function on(event,listener){
  if(!live||typeof event!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(event)||typeof listener!=='function')
    throw new Error('Invalid event listener');
  let group=listeners.get(event);
  if(!group){if(listeners.size>=64)throw new Error('Event limit reached');group=new Set();listeners.set(event,group);}
  if(group.size>=128)throw new Error('Listener limit reached');
  group.add(listener);return function(){group.delete(listener);if(!group.size)listeners.delete(event);};
}
Object.defineProperty(window,'__souiDesktopDeliver',{configurable:false,writable:false,value:function(value){
  if(!live||!value||value.token!==token||value.v!==1)return;
  if(typeof value.event==='string'){
    const group=listeners.get(value.event);if(group)for(const callback of Array.from(group)){
      try{callback(value.value);}catch(error){setTimeout(function(){throw error;},0);}
    }return;
  }
  const item=pending.get(value.id);if(!item)return;
  pending.delete(value.id);clearTimeout(item.timer);
  if(value.ok)item.resolve(value.result);
  else{const error=new Error(value.error&&value.error.message||'Native request failed');
    error.code=value.error&&value.error.code||'native_failure';item.reject(error);}
}});
Object.defineProperty(window,'souiDesktop',{configurable:false,writable:false,value:Object.freeze({version:1,invoke:invoke,on:on})});
window.addEventListener('pagehide',function(){live=false;for(const item of pending.values()){
  clearTimeout(item.timer);item.reject(new Error('Document session was replaced'));}
  pending.clear();listeners.clear();},{once:true});
})();)JS";
}
}  // namespace soui::desktop::detail
