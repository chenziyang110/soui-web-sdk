#include <soui/host/soui_miniblink_host.h>
#include <soui/host/window_chrome_script.h>
#include <soui/runtime/host/trusted_document.h>
#include <soui/runtime/services/desktop/window_commands.h>
#include "bridge_pending_script.h"
#include "bridge_request_budget.h"
#include "engine_admission.h"
#include "input_mailbox.h"
#include "cursor_broker.h"
#include "main_context_gate.h"
#include "runtime_shutdown_poll.h"
#include <soui/diagnostics/renderer_observation_binding.h>
#include <soui/diagnostics/renderer_model_binding.h>
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
#include "diagnostic_result.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_map>

#include <soui/bridge_adapter.h>
#include "../../../third_party/include/json/json.hpp"
#ifdef SOUI_TEST_KIT
#include "../../test-kit/native/dom-agent.generated.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif
#ifdef SOUI_BRIDGE_BENCHMARK
#include <mutex>
#include <vector>
#endif

#ifdef _WIN32
#ifndef ENABLE_WKE
#define ENABLE_WKE 0
#endif
#include "../../../third_party/include/miniblink/wke.h"
#include <imm.h>
#include <windows.h>
#include <windowsx.h>
#include "runtime_shutdown_api.h"
#ifdef SOUI_SOURCE_KERNEL_HOST
#include "source_kernel_api.h"
#endif
#endif

namespace soui::sdk {
namespace {

// Never reset: a later mount cannot make an old native lookup current again.
std::atomic<std::uint64_t> native_context_lookup_revision{0};

// Owner-thread reentrancy guard: native lifecycle calls may dispatch window
// messages while waiting. This does not make host calls thread-safe.
class lifecycle_operation_guard {
 public:
  explicit lifecycle_operation_guard(bool& active) noexcept : active_(active) { active_ = true; }
  lifecycle_operation_guard(const lifecycle_operation_guard&) = delete;
  lifecycle_operation_guard& operator=(const lifecycle_operation_guard&) = delete;
  ~lifecycle_operation_guard() { active_ = false; }

 private:
  bool& active_;
};

#ifdef _WIN32
constexpr wchar_t kInteractionWindowClass[] = L"SouiMiniblinkInteractionWindowV1";

LRESULT CALLBACK interaction_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* adapter = reinterpret_cast<dynamic_miniblink_adapter*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    adapter = static_cast<dynamic_miniblink_adapter*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(adapter));
  }
  if (adapter != nullptr) {
    std::intptr_t result = 0;
    if (adapter->handle_native_window_message(message,
                                              static_cast<std::uintptr_t>(wparam),
                                              static_cast<std::intptr_t>(lparam), result)) {
      return static_cast<LRESULT>(result);
    }
  }
  if (message == WM_NCDESTROY)
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  return DefWindowProcW(window, message, wparam, lparam);
}
#endif

#ifdef SOUI_BRIDGE_BENCHMARK
std::mutex paint_samples_mutex;
std::vector<double> paint_samples;
std::vector<double> paint_pixel_changes;
unsigned paint_pixel_failures{0};
class paint_band_sampler {
 public:
  ~paint_band_sampler() { reset(); }
  bool sample(HDC source, bool& changed) {
    BITMAP bitmap{};
    if (!GetObjectW(GetCurrentObject(source, OBJ_BITMAP), sizeof(bitmap), &bitmap) || bitmap.bmWidth <= 0 || (bitmap.bmHeight >= -220 && bitmap.bmHeight <= 220)) return false;
    if (width_ != bitmap.bmWidth) {
      reset();
      BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth=bitmap.bmWidth; info.bmiHeader.biHeight=-1;
      info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
      dc_=CreateCompatibleDC(source);
      bitmap_=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
      if (!dc_ || !bitmap_ || !pixels_) { reset(); return false; }
      old_=SelectObject(dc_,bitmap_);
      if (!old_ || old_==HGDI_ERROR) { reset(); return false; }
      width_=bitmap.bmWidth;
      std::memset(pixels_,0,static_cast<std::size_t>(width_)*4);
    }
    if (!BitBlt(dc_,0,0,width_,1,source,0,220,SRCCOPY)) return false;
    if (!GdiFlush()) return false;
    std::uint32_t hash=2166136261U;
    const auto* bytes=static_cast<const unsigned char*>(pixels_);
    for (int x=0;x<width_;++x) for(int channel=0;channel<3;++channel) hash=(hash^bytes[x*4+channel])*16777619U;
    changed=!has_hash_ || hash!=hash_; hash_=hash; has_hash_=true;
    return true;
  }
 private:
  void reset() {
    if(dc_ && old_ && old_!=HGDI_ERROR) SelectObject(dc_,old_);
    if(bitmap_) DeleteObject(bitmap_);
    if(dc_) DeleteDC(dc_);
    dc_=nullptr; bitmap_=nullptr; old_=nullptr; pixels_=nullptr; width_=0; has_hash_=false;
  }
  HDC dc_{nullptr}; HBITMAP bitmap_{nullptr}; HGDIOBJ old_{nullptr}; void* pixels_{nullptr};
  int width_{0}; std::uint32_t hash_{0}; bool has_hash_{false};
};
paint_band_sampler paint_band;
std::vector<std::string> paint_phases;
std::string paint_phase{"startup"};
std::filesystem::path gallery_checkpoint_path;
unsigned gallery_checkpoint_count{0};
jsValue WKE_CALL_TYPE gallery_checkpoint(jsExecState es, void*) {
  const char* raw=jsToString(es,jsArg(es,0));
  if (!raw || gallery_checkpoint_path.empty() || gallery_checkpoint_count>=5000) return jsBoolean(false);
  const std::string name(raw);
  if(name.empty() || name.size()>48 || !std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)!=0;}))return jsBoolean(false);
  const bool mounted=jsToBoolean(es,jsArg(es,1))!=FALSE;
  std::ofstream stream(gallery_checkpoint_path,std::ios::app);
  stream<<nlohmann::json{{"name",name},{"stage",mounted?"mounted":"before"}}.dump()<<'\n';
  ++gallery_checkpoint_count;
  return jsBoolean(stream.good());
}
double first_vue_ready_qpc_ms{0};
double first_content_paint_qpc_ms{0};
std::filesystem::path cycle_evidence_path;
jsValue WKE_CALL_TYPE write_benchmark_cycle(jsExecState es, void*) {
  const char* raw = jsToString(es, jsArg(es, 0));
  if (!raw || cycle_evidence_path.empty()) return jsBoolean(false);
  const std::string value(raw);
  if (value.size() > 1024U * 1024U) return jsBoolean(false);
  auto record = nlohmann::json::parse(value, nullptr, false);
  if (!record.is_object() || record.value("files", 0) != 10000) return jsBoolean(false);
  std::ofstream stream(cycle_evidence_path, std::ios::app | std::ios::binary);
  stream << value << '\n';
  stream.flush();
  return jsBoolean(stream.good());
}
jsValue WKE_CALL_TYPE mark_benchmark_phase(jsExecState es, void*) {
  const char* raw = jsToString(es, jsArg(es, 0));
  const std::string phase = raw ? raw : "";
  if (phase == "scroll" || phase == "idle" || phase == "paging" || phase == "zoom" || phase == "resize" || phase == "drag" || phase == "animation" || phase == "input") {
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    paint_phase = phase;
  }
  LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
  return jsDouble(1000.0 * now.QuadPart / frequency.QuadPart);
}
jsValue WKE_CALL_TYPE benchmark_click_callback(jsExecState es, void* param) {
  if (!param) return jsDouble(0);
  return jsDouble(static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_click(jsToInt(es, jsArg(es, 0)), jsToInt(es, jsArg(es, 1))));
}
jsValue WKE_CALL_TYPE benchmark_text_callback(jsExecState, void* param) {
  if (!param) return jsBoolean(false);
  static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_text();
  return jsBoolean(true);
}
void WKE_CALL_TYPE record_benchmark_paint(wkeWebView, void*, HDC dc, int, int, int, int) {
  LARGE_INTEGER tick{}, frequency{};
  QueryPerformanceCounter(&tick);
  QueryPerformanceFrequency(&frequency);
  std::lock_guard<std::mutex> lock(paint_samples_mutex);
  bool pixel_changed=false;
  if (!dc || !paint_band.sample(dc,pixel_changed)) ++paint_pixel_failures;
  else if (pixel_changed && paint_pixel_changes.size()<100000) paint_pixel_changes.push_back(1000.0*tick.QuadPart/frequency.QuadPart);
  if (first_vue_ready_qpc_ms > 0 && first_content_paint_qpc_ms == 0) {
    first_content_paint_qpc_ms = 1000.0 * tick.QuadPart / frequency.QuadPart;
  }
  if (paint_samples.size() < 100000) {
    paint_samples.push_back(1000.0 * tick.QuadPart / frequency.QuadPart);
    paint_phases.push_back(paint_phase);
  }
}
jsValue WKE_CALL_TYPE benchmark_motion_callback(jsExecState es, void* param) {
  const int index = jsToInt(es, jsArg(es, 0));
  if (!param || index < 0 || index > 120) return jsBoolean(false);
  { std::ofstream trace(cycle_evidence_path.parent_path() / "motion-steps.log", std::ios::app); trace << index << '\n'; }
  static_cast<soui::sdk::dynamic_miniblink_adapter*>(param)->benchmark_motion(static_cast<unsigned>(index));
  return jsBoolean(true);
}
#endif

#ifdef _WIN32
void WKE_CALL_TYPE record_live_paint(wkeWebView view, void* context, HDC dc, int x, int y, int width, int height) {
  if (context) static_cast<bridge_adapter*>(context)->record_paint();
#ifdef SOUI_BRIDGE_BENCHMARK
  record_benchmark_paint(view, nullptr, dc, x, y, width, height);
#else
  (void)view; (void)dc; (void)x; (void)y; (void)width; (void)height;
#endif
}
#endif

host_result failure(std::string code, std::string message) {
  return {false, std::move(code), std::move(message)};
}

void bridge_diagnostic_trace(const void* adapter, std::string_view phase,
                             std::string_view request_id = {},
                             std::string_view detail = {}) {
#ifdef _WIN32
  char enabled[2]{};
  if (GetEnvironmentVariableA("SOUI_BRIDGE_TRACE", enabled, sizeof(enabled)) != 1 ||
      enabled[0] != '1') return;
  char temporary[MAX_PATH + 1]{};
  const DWORD length = GetTempPathA(MAX_PATH, temporary);
  if (!length || length > MAX_PATH) return;
  static std::mutex trace_mutex;
  std::lock_guard<std::mutex> lock(trace_mutex);
  std::ofstream trace(std::filesystem::path(temporary) /
      ("soui-bridge-trace-" + std::to_string(GetCurrentProcessId()) + ".log"),
      std::ios::app | std::ios::binary);
  trace << GetCurrentThreadId() << ' ' << adapter << ' ' << phase;
  if (!request_id.empty()) trace << " id=" << request_id;
  if (!detail.empty()) trace << ' ' << detail;
  trace << '\n';
#else
  (void)adapter; (void)phase; (void)request_id; (void)detail;
#endif
}

host_result success(std::string message = {}) { return {true, {}, std::move(message)}; }

bool contains_remote_runtime_reference(const std::filesystem::path& path,
                                       const std::string& content) {
  // Every existing rule requires this literal token. Avoid the regex engine
  // altogether when it is absent; matching semantics remain unchanged.
  if (content.find("//") == std::string::npos) return false;
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

  const std::regex* pattern = nullptr;
  if (extension == ".html" || extension == ".htm") {
    static const std::regex html{R"remote(\b(src|href|action|poster|srcset)\s*=\s*["'][^"']*(https?:)?//)remote",std::regex::icase};
    pattern = &html;
  } else if (extension == ".css") {
    static const std::regex css{R"remote((url\(|@import\s+(url\()?)[[:space:]]*["']?[[:space:]]*(https?:)?//)remote",std::regex::icase};
    pattern = &css;
  } else if (extension == ".js" || extension == ".mjs") {
    static const std::regex js{R"remote((fetch|importScripts|WebSocket|EventSource|import)\s*\(\s*["']\s*(https?:)?//|\.open\s*\(\s*["'][A-Z]+["']\s*,\s*["']\s*(https?:)?//|\.(src|href)\s*=\s*["']\s*(https?:)?//|setAttribute\s*\(\s*["'](src|href)["']\s*,\s*["']\s*(https?:)?//)remote",std::regex::icase};
    pattern = &js;
  } else if (extension == ".json") {
    static const std::regex json{R"remote(["'](file|src|href|url)["']\s*:\s*["']\s*(https?:)?//)remote",std::regex::icase};
    pattern = &json;
  }
  return pattern != nullptr && std::regex_search(content, *pattern);
}

bool contains_untrusted_frame_surface(const std::filesystem::path& path,
                                      const std::string& content) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  if (extension == ".html" || extension == ".htm") {
    static const std::regex html{R"(<\s*(iframe|frame|object|embed)\b)",std::regex::icase};
    return std::regex_search(content, html);
  }
  if (extension == ".js" || extension == ".mjs") {
    static const std::regex js{R"(createElement\s*\(\s*["'](iframe|frame|object|embed)["'])",std::regex::icase};
    return std::regex_search(content, js);
  }
  return false;
}

void append_host_smoke_log(const std::filesystem::path& resource_root, const char* line) {
#ifdef SOUI_BRIDGE_BENCHMARK
  LARGE_INTEGER now{}, frequency{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  std::ofstream stages(resource_root.parent_path() / "startup-stages.ndjson", std::ios::app);
  stages.precision(16);
  stages << nlohmann::json{{"stage", line}, {"qpc_ms", 1000.0 * now.QuadPart / frequency.QuadPart}}.dump() << '\n';
#endif
  const auto path = resource_root.parent_path() / "webview-smoke.log";
  if (!std::filesystem::is_regular_file(path)) return;
  std::ofstream output(path, std::ios::app);
  if (output) output << line << '\n';
}

template <typename Bridge>
auto bridge_handle_json(Bridge& bridge, std::string_view request, int)
    -> decltype(std::string{bridge.handle_json(request)}) {
  return std::string{bridge.handle_json(request)};
}

template <typename Bridge>
std::string bridge_handle_json(Bridge&, std::string_view, ...) {
  return R"({"ok":false,"error":{"code":"E_BRIDGE_UNAVAILABLE","message":"The native bridge is not connected."}})";
}

template <typename Bridge>
auto bridge_set_owner_window(Bridge& bridge, void* owner, int)
    -> decltype(bridge.set_owner_window(owner), void()) {
  bridge.set_owner_window(owner);
}

template <typename Bridge>
void bridge_set_owner_window(Bridge&, void*, ...) {}

template <typename Bridge>
auto bridge_set_event_sink_string(Bridge& bridge, std::function<void(std::string)> sink, int)
    -> decltype(bridge.set_event_sink(std::move(sink)), void()) {
  bridge.set_event_sink(std::move(sink));
}

template <typename Bridge>
void bridge_set_event_sink_string(Bridge&, std::function<void(std::string)>, ...) {}

template <typename Bridge>
auto bridge_set_event_sink_view(Bridge& bridge, std::function<void(std::string_view)> sink, int)
    -> decltype(bridge.set_event_sink(std::move(sink)), void()) {
  bridge.set_event_sink(std::move(sink));
}

template <typename Bridge>
void bridge_set_event_sink_view(Bridge&, std::function<void(std::string_view)>, ...) {}

template <typename Bridge>
auto bridge_shutdown_session(Bridge& bridge, const std::string& session_id, int)
    -> decltype(bridge.shutdown_session(session_id), void()) {
  bridge.shutdown_session(session_id);
}

template <typename Bridge>
auto bridge_shutdown_session(Bridge& bridge, const std::string&, long)
    -> decltype(bridge.shutdown_session(), void()) {
  bridge.shutdown_session();
}

template <typename Bridge>
void bridge_shutdown_session(Bridge&, const std::string&, ...) {}

#ifdef _WIN32

struct pending_event {
  dynamic_miniblink_adapter* adapter{nullptr};
  std::string json;
  std::uint64_t context_receipt{0};
  std::uint64_t bridge_generation{0};
};

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

std::filesystem::path canonical_path(const std::filesystem::path& value) {
  std::error_code error;
  const auto result = std::filesystem::weakly_canonical(value, error);
  return error ? value : result;
}

constexpr std::size_t max_bridge_message_bytes = 64U * 1024U;

nlohmann::json bounded_bridge_packet(std::string_view request) {
  if (request.empty() || request.size() > max_bridge_message_bytes) return {};
  try {
    return nlohmann::json::parse(request.begin(), request.end(),
        [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
          if (depth > 32) throw std::invalid_argument("Bridge JSON depth exceeded");
          return true;
        });
  } catch (const std::exception&) { return {}; }
}

std::string packet_string(const nlohmann::json& packet, const char* field) {
  if (!packet.is_object()) return {};
  const auto value = packet.find(field);
  return value != packet.end() && value->is_string() && value->get_ref<const std::string&>().size() <= 512
      ? value->get<std::string>() : std::string{};
}

std::string bridge_error(const std::string& id, const char* code, const char* message) {
  return nlohmann::json{{"requestId", id}, {"ok", false},
      {"error", {{"code", code}, {"message", message}}}}.dump();
}

std::string bridge_resolution_script(const std::string& id, const std::string& response) {
  return "(function(){var p=window.__souiBridgePending;var id=" + json_string_literal(id)
      + ";if(!p||!p[id])return;return p[id].finish(" + json_string_literal(response) + ");})()";
}

std::uint32_t bridge_wait_timeout(const nlohmann::json& packet) {
  const auto timeout = packet.find("timeoutMs");
  if (timeout == packet.end()) return 10000;
  if (!timeout->is_number_unsigned() || timeout->get<std::uint64_t>() < 1 ||
      timeout->get<std::uint64_t>() > 60000) return 0;
  return timeout->get<std::uint32_t>();
}

std::string bridge_request_method(std::string_view request) {
  return packet_string(bounded_bridge_packet(request), "method");
}

// The pinned engine owns process-wide Blink/V8 state and cannot be restarted
// after finalize. All views and bindings therefore share one stable owner.
struct process_runtime_state {
  std::mutex mutex;
  std::unique_ptr<engine_thread> thread;
  HMODULE module{nullptr};
  std::filesystem::path binary;
  std::size_t hosts{0};
  std::atomic<bool> initialized{false};
  std::atomic<bool> retired{false};
  diagnostics::queue terminal_engine_metrics;
  bool terminal_engine_metrics_recorded{false};
  diagnostics::mode renderer_mode{diagnostics::mode::disabled};
  wkeRendererDiagnosticsApi renderer_api{};
  wkeRendererModelDiagnosticsApi renderer_model_api{};
  diagnostics::detail::renderer_model_binding renderer_model_binding;
  wkeCacheDiagnosticsApi cache_api{};
  diagnostics::detail::cache_sample_slot cache_slot;
  diagnostics::detail::cache_identity_binding cache_binding;
  diagnostics::cache_observation cache_completed;
  std::string renderer_module_path;
  bool renderer_module_matches{false};
  std::uint64_t renderer_pid{0};
  std::uint64_t renderer_creation{0};
  diagnostics::detail::renderer_observation_binding renderer_binding;
  bool starting{false};
  bool shutdown_pending{false};
  bool prepare_inflight{false};
  bool prepare_ready{false};
  bool shutdown_failed{false};
  unsigned preparation_status{1};
  std::uint64_t shutdown_generation{0};
  wkeCursorBrokerApi cursor_api{};
  wkeCursorBrokerRealm cursor_realm{};
  // Accessed only on the engine owner, including JS callbacks and destruction.
  std::unordered_map<wkeWebView, dynamic_miniblink_adapter*> views;
  // The process-lifetime owner is intentionally retained on Pending/Failed.
  // Static destruction must not bypass prepare and call an unbounded stop.
};

// Only startup writes this optional table while Runtime.starting protects readers.
void prepare_cache_observer(process_runtime_state& runtime) noexcept {
  runtime.cache_api = {};
  if (runtime.renderer_mode != diagnostics::mode::scalar || !runtime.renderer_module_matches) return;
  try {
    using get_api = const wkeCacheDiagnosticsApi* (__cdecl*)(std::uint32_t);
    const auto get = reinterpret_cast<get_api>(GetProcAddress(runtime.module, "wkeGetCacheDiagnosticsApi"));
    const auto* api = get ? get(WKE_CACHE_ABI_VERSION) : nullptr;
    if (diagnostics::detail::cache_api_supported(api)) runtime.cache_api = *api;
  } catch (...) { runtime.cache_api = {}; }
}

process_runtime_state& process_runtime() {
  static auto* state = new process_runtime_state;
  return *state;
}

// Startup owns these writes while Runtime.starting is true. Readers hold the
// Runtime mutex and refuse starting before accessing any optional API field.
void prepare_renderer_observer(process_runtime_state& runtime,
                               const std::filesystem::path& binary,
                               diagnostics::mode metrics) noexcept {
  runtime.renderer_mode = metrics;
  runtime.renderer_api = {};
  runtime.renderer_model_api = {};
  runtime.renderer_module_matches = false;
  if (metrics != diagnostics::mode::scalar) return;
  try {
    std::wstring loaded(32768, L'\0');
    const auto length = GetModuleFileNameW(runtime.module, loaded.data(), static_cast<DWORD>(loaded.size()));
    if (length == 0 || length >= loaded.size()) return;
    loaded.resize(length);
    const auto actual = canonical_path(std::filesystem::path(loaded));
    runtime.renderer_module_path = actual.u8string();
    runtime.renderer_module_matches = CompareStringOrdinal(actual.c_str(), -1, binary.c_str(), -1, TRUE) == CSTR_EQUAL;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return;
    runtime.renderer_pid = GetCurrentProcessId();
    runtime.renderer_creation = (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    if (metrics != diagnostics::mode::scalar || !runtime.renderer_module_matches) return;
    using get_api = const wkeRendererDiagnosticsApi* (__cdecl*)(std::uint32_t);
    const auto get = reinterpret_cast<get_api>(GetProcAddress(runtime.module, "wkeGetRendererDiagnosticsApi"));
    if (!get) return;
    const auto* api = get(WKE_RENDER_DIAGNOSTICS_ABI_VERSION);
    if (!diagnostics::detail::renderer_api_supported(api)) return;
    runtime.renderer_api = *api;
    using get_model_api = const wkeRendererModelDiagnosticsApi* (__cdecl*)(std::uint32_t);
    const auto get_model = reinterpret_cast<get_model_api>(GetProcAddress(runtime.module, "wkeGetRendererModelDiagnosticsApi"));
    if (!get_model) return;
    const auto* model_api = get_model(2);
    if (diagnostics::detail::renderer_model_api_supported(model_api)) runtime.renderer_model_api = *model_api;
  } catch (...) {
    runtime.renderer_api = {};
    runtime.renderer_model_api = {};
    runtime.renderer_module_matches = false;
  }
}

void read_renderer_observer(process_runtime_state& runtime,
                            diagnostics::renderer_observation& output) {
  output = {};
  if (runtime.starting) { output.state = diagnostics::renderer_status::busy; return; }
  if (runtime.renderer_mode != diagnostics::mode::scalar) {
    output.state = runtime.thread ? diagnostics::renderer_status::disabled : diagnostics::renderer_status::not_started;
    return;
  }
  output.module_path = runtime.renderer_module_path;
  output.module_matches = runtime.renderer_module_matches;
  if (!output.module_matches || !runtime.renderer_api.readProcessSnapshot || runtime.renderer_binding.invalid()) return;
  if (runtime.renderer_binding.terminal_recorded()) {
    output.native = runtime.renderer_binding.terminal();
    output.state = diagnostics::renderer_status::closed;
    output.identity_bound = true;
    output.terminal_snapshot = true;
    return;
  }
  wkeRendererProcessSnapshot candidate{};
  candidate.structSize = sizeof(candidate);
  const auto state = runtime.renderer_api.readProcessSnapshot(&candidate);
  output.native = candidate; // Unknown/busy retains the actual diagnostic envelope.
  if (state == WKE_RENDER_DIAGNOSTICS_BUSY) { output.state = diagnostics::renderer_status::busy; return; }
  if (state == WKE_RENDER_DIAGNOSTICS_NOT_STARTED) { output.state = diagnostics::renderer_status::not_started; return; }
  if (candidate.status != state || !runtime.renderer_binding.observe(candidate, runtime.renderer_pid, runtime.renderer_creation)) return;
  output.native = candidate;
  output.identity_bound = true;
  output.state = state == WKE_RENDER_DIAGNOSTICS_CLOSED ? diagnostics::renderer_status::closed : diagnostics::renderer_status::ready;
  output.terminal_snapshot = runtime.renderer_binding.retain_terminal(candidate, runtime.retired.load(std::memory_order_acquire));
}

void read_renderer_model_observer(process_runtime_state& runtime,
                                  diagnostics::renderer_model_observation& output) {
  output = {};
  if (runtime.starting) { output.state = diagnostics::renderer_status::busy; return; }
  if (runtime.renderer_mode != diagnostics::mode::scalar) {
    output.state = runtime.thread ? diagnostics::renderer_status::disabled : diagnostics::renderer_status::not_started;
    return;
  }
  output.module_path = runtime.renderer_module_path;
  output.module_matches = runtime.renderer_module_matches;
  if (!output.module_matches || !runtime.renderer_model_api.readProcessModel || runtime.renderer_model_binding.invalid()) return;
  if (runtime.renderer_model_binding.terminal_recorded()) {
    output.native = runtime.renderer_model_binding.terminal();
    output.state = diagnostics::renderer_status::closed;
    output.identity_bound = true; output.terminal_snapshot = true; return;
  }
  wkeRendererModelSnapshot candidate{}; candidate.structSize = sizeof(candidate);
  const auto state = runtime.renderer_model_api.readProcessModel(&candidate);
  output.native = candidate;
  if (state == WKE_RENDER_DIAGNOSTICS_BUSY) { output.state = diagnostics::renderer_status::busy; return; }
  if (state == WKE_RENDER_DIAGNOSTICS_NOT_STARTED) { output.state = diagnostics::renderer_status::not_started; return; }
  if (candidate.status != state || !runtime.renderer_model_binding.observe(candidate, runtime.renderer_pid, runtime.renderer_creation)) return;
  output.identity_bound = true;
  output.state = state == WKE_RENDER_DIAGNOSTICS_CLOSED ? diagnostics::renderer_status::closed : diagnostics::renderer_status::ready;
  output.terminal_snapshot = runtime.renderer_model_binding.retain_terminal(candidate, runtime.retired.load(std::memory_order_acquire));
}

wkeCursorBrokerIdentity cursor_identity(interaction_generation generation) noexcept {
  return {sizeof(wkeCursorBrokerIdentity), 0, generation.view, generation.document, generation.geometry};
}

bool valid_cursor_api(const wkeCursorBrokerApi* api) noexcept {
  constexpr unsigned required = WKE_CURSOR_BROKER_CAP_NPAPI_UNAVAILABLE | WKE_CURSOR_BROKER_CAP_DESCRIPTOR_ONLY;
  return api && api->structSize >= sizeof(*api) && api->abiVersion == WKE_CURSOR_BROKER_ABI_VERSION &&
      !api->reserved && (api->capabilities & required) == required && api->beginRealm &&
      api->createEndpoint && api->updateIdentity && api->requestClose && api->inspectDrain &&
      api->destroyEndpoint && api->endRealm;
}

// Teardown may not return while callbacks still reference the adapter. A native
// kernel hang cannot be safely preempted; external supervision records failure.
struct lifecycle_invocation final {
  bool completed{false};
  detail::engine_admission_result admission{detail::engine_admission_result::invalid};
  explicit operator bool() const noexcept { return completed; }
};
host_result engine_admission_failure(detail::engine_admission_result result,const char* message,bool cleanup=false) {
  return failure(result == detail::engine_admission_result::full ? (cleanup ? "E_CURSOR_CLOSE_PENDING" : "E_BUSY")
      : "E_ENGINE_THREAD",message);
}
lifecycle_invocation invoke_lifecycle(engine_thread* thread, engine_thread::task work,
    detail::engine_lane lane=detail::engine_lane::ordinary) {
  using result=detail::engine_admission_result;
  if (!thread || !work) return {};
  if (thread->is_owner_thread()) { work(); return {true,result::accepted}; }
  auto completion = std::make_shared<std::promise<void>>();
  auto future = completion->get_future();
  const auto admitted=detail::engine_admission::post(*thread,lane,[work = std::move(work), completion] {
    try { work(); completion->set_value(); }
    catch (...) { completion->set_exception(std::current_exception()); }
  });
  if (admitted != result::accepted) return {false,admitted};
  try { future.get(); return {true,admitted}; } catch (...) { return {false,admitted}; }
}
host_result acquire_runtime(const host_config& config, engine_thread*& thread, void*& module,
    diagnostics::mode metrics) {
  auto& runtime = process_runtime();
  std::unique_lock<std::mutex> lock(runtime.mutex);
  if (runtime.retired) return failure("E_RUNTIME_SHUTDOWN", "The process runtime has already shut down.");
  if (runtime.starting) return failure("E_RUNTIME_STARTING", "The process runtime initializer still owns this generation.");
  if (runtime.shutdown_pending || runtime.shutdown_failed)
    return failure("E_RUNTIME_SHUTDOWN_PENDING", "Runtime shutdown owns this generation; new hosts are refused.");
  const auto binary = canonical_path(config.miniblink_binary);
  if (!std::filesystem::is_regular_file(binary)) return failure("E_DEPENDENCY_MISSING", "Miniblink binary is missing.");
  if (!binary_matches_architecture(binary, config.architecture)) return failure("E_WRONG_ARCHITECTURE", "Miniblink binary architecture does not match the host.");
  if (runtime.thread && runtime.binary != binary)
    return failure("E_RUNTIME_BINARY", "All windows must use the same process runtime binary.");
  if (!runtime.thread) {
    // The first real process runtime construction selects immutable mode.
    // Later views neither replace nor enable an existing disabled engine.
    auto candidate = std::make_unique<engine_thread>(metrics);
    host_result result = failure("E_DEPENDENCY_INIT", "Miniblink process initialization failed.");
    runtime.starting = true;
    lock.unlock(); // Startup/failure finalizers cannot acquire a mutex held by their waiter.
    bool started = false;
    try {
      started = candidate->start([&runtime, binary, config, metrics, &result] {
      runtime.module = LoadLibraryW(binary.c_str());
      if (!runtime.module) { result = failure("E_DEPENDENCY_LOAD", "Miniblink binary could not be loaded."); return false; }
      prepare_renderer_observer(runtime, binary, metrics);
      prepare_cache_observer(runtime);
      if (!GetProcAddress(runtime.module, "wkeInitializeEx") || !has_runtime_shutdown_api(runtime.module)) {
        result = failure("E_DEPENDENCY_API", "Miniblink initialization or prepared-shutdown API is missing; rebuild the matching kernel."); return false;
      }
      wkeSetWkeDllHandle(runtime.module);
#ifdef SOUI_BRIDGE_BENCHMARK
      if (config.noopt_experiment) {
        using set_flags = void(__cdecl*)(const char*, int);
#ifdef _WIN64
        const char* symbol = "?SetFlagsFromString@V8@v8@@SAXPEBDH@Z";
#else
        const char* symbol = "?SetFlagsFromString@V8@v8@@SAXPBDH@Z";
#endif
        const auto set = reinterpret_cast<set_flags>(GetProcAddress(runtime.module, symbol));
        if (!set) { result = failure("E_DEPENDENCY_API", "V8 diagnostic flag export unavailable."); return false; }
        set("--noopt", 7);
      }
#endif
#ifdef SOUI_SOURCE_KERNEL_HOST
      runtime.initialized = initialize_source_kernel(runtime.module);
#else
      runtime.initialized = wkeInitialize() != 0;
#endif
      if (!runtime.initialized) return false;
      if (wkeGetCursorBrokerApi) {
        const auto* api = wkeGetCursorBrokerApi(WKE_CURSOR_BROKER_ABI_VERSION);
        if (!valid_cursor_api(api)) {
          result = failure("E_CURSOR_BROKER_ABI", "The controlled cursor ABI is incomplete.");
          return false;
        }
        wkeCursorBrokerRealmConfig realm_config{sizeof(realm_config), 64, 64, 0};
        wkeCursorBrokerRealmReceipt receipt{}; receipt.structSize = sizeof(receipt);
        const auto status = api->beginRealm(&realm_config, &receipt);
        constexpr unsigned required = WKE_CURSOR_BROKER_CAP_NPAPI_UNAVAILABLE | WKE_CURSOR_BROKER_CAP_DESCRIPTOR_ONLY;
        if (status != WKE_CURSOR_BROKER_OK || receipt.structSize != sizeof(receipt) ||
            !(receipt.realm.low || receipt.realm.high) || !receipt.realmGeneration ||
            (receipt.capabilities & required) != required) {
          result = failure("E_CURSOR_BROKER_REALM", "A fresh controlled cursor realm could not be established.");
          return false;
        }
        runtime.cursor_api = *api;
        runtime.cursor_realm = receipt.realm;
      }
      return true;
    }, [&runtime] {
      if (runtime.initialized) {
        using finalize_prepared = unsigned (__cdecl*)();
        finalize_prepared finalize = nullptr;
        {
          std::lock_guard<std::mutex> lock(runtime.mutex);
          if (!runtime.prepare_ready) throw std::runtime_error("Worker shutdown was not prepared");
          finalize = reinterpret_cast<finalize_prepared>(GetProcAddress(runtime.module, "wkeFinalizePrepared"));
        }
        if (!finalize || finalize() != 0) {
          std::lock_guard<std::mutex> lock(runtime.mutex);
          runtime.shutdown_failed = true;
          throw std::runtime_error("Prepared kernel finalization failed; runtime owner retained");
        }
        std::lock_guard<std::mutex> lock(runtime.mutex);
        runtime.initialized = false;
        runtime.retired = true;
        runtime.module = nullptr; // Preserve existing loaded module lifetime; no FreeLibrary.
      } else if (runtime.module) {
        FreeLibrary(runtime.module);
        runtime.module = nullptr;
        runtime.renderer_api = {};
        runtime.renderer_model_api = {};
        runtime.renderer_module_matches = false;
      }
    });
    } catch (...) {
      lock.lock();
      runtime.starting = false;
      runtime.shutdown_failed = true;
      runtime.thread = std::move(candidate); // Exception retains any real backing/module owner.
      return failure("E_RUNTIME_STARTUP_FAILED", "Runtime startup threw; owner and module retained.");
    }
    lock.lock();
    runtime.starting = false;
    if (!started) {
      if (runtime.module || runtime.initialized) {
        runtime.shutdown_failed = true;
        runtime.thread = std::move(candidate);
      }
      return result;
    }
    runtime.binary = binary;
    runtime.thread = std::move(candidate);
  }
  ++runtime.hosts;
  thread = runtime.thread.get();
  module = runtime.module;
  return success();
}

void release_runtime() {
  auto& runtime = process_runtime();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.hosts) --runtime.hosts;
}

dynamic_miniblink_adapter* adapter_for_context(jsExecState state) {
  if (!state || !jsGetWebView) return nullptr;
  const auto lookup = detail::context_lookup_revision<>::capture(native_context_lookup_revision);
  if (!detail::context_lookup_revision<>::matches(native_context_lookup_revision, lookup)) return nullptr;
  const auto view = jsGetWebView(state);
  if (!detail::context_lookup_revision<>::matches(native_context_lookup_revision, lookup)) return nullptr;
  const auto& views = process_runtime().views;
  const auto found = views.find(view);
  return found == views.end() ? nullptr : found->second;
}

#endif

}  // namespace

host_result shutdown_miniblink_runtime() {
#ifdef _WIN32
  auto& runtime = process_runtime();
  engine_thread* engine = nullptr;
  std::uint64_t generation = 0;
  bool submit_prepare = false;
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.hosts) return failure("E_RUNTIME_IN_USE", "Destroy all hosts before shutting down the process runtime.");
    if (runtime.starting) return failure("E_RUNTIME_SHUTDOWN_PENDING", "Runtime startup still owns the native initializer.");
    if (runtime.shutdown_failed) return failure(runtime.preparation_status == 3 ? "E_WORKER_SHUTDOWN_UNSUPPORTED" : "E_RUNTIME_SHUTDOWN_FAILED", "Unsupported or failed Worker shutdown; module and owner retained.");
    if (runtime.retired && !runtime.thread) return success("Miniblink process runtime shutdown completed.");
    if (!runtime.thread) return success("No process runtime exists.");
    engine = runtime.thread.get(); // Process-lifetime owner cannot reset while this generation is pending.
    if (engine->is_owner_thread()) return failure("E_RUNTIME_THREAD", "Runtime shutdown must run outside the engine owner.");
    if (!runtime.shutdown_pending) {
      runtime.shutdown_pending = true;
      runtime.cache_slot.retire(); // Late sampling tasks cannot publish across close.
      runtime.cache_completed.state = diagnostics::cache_status::unavailable;
      ++runtime.shutdown_generation; // This process runtime cannot be restarted.
    }
    generation = runtime.shutdown_generation;
    if (!runtime.prepare_ready && !runtime.prepare_inflight) {
      runtime.prepare_inflight = true;
      submit_prepare = true;
    }
  }
  if (submit_prepare) {
    const auto admitted = detail::engine_admission::post(*engine, detail::engine_lane::cleanup, [&runtime, generation] {
      unsigned result = 3; // Absent handshake is unsupported, never successful finalization.
      try {
        using prepare_workers = unsigned (__cdecl*)();
        const auto prepare = reinterpret_cast<prepare_workers>(GetProcAddress(runtime.module, "wkePrepareWorkerShutdown"));
        if (prepare) {
          bool cursor_ready = true;
          if (runtime.cursor_realm.low || runtime.cursor_realm.high) {
            const auto status = runtime.cursor_api.endRealm(runtime.cursor_realm);
            cursor_ready = status == WKE_CURSOR_BROKER_OK;
            if (cursor_ready) runtime.cursor_realm = {};
          }
          result = cursor_ready ? prepare() : 1;
        }
      } catch (...) { result = 2; }
      std::lock_guard<std::mutex> lock(runtime.mutex);
      if (runtime.shutdown_generation != generation) return;
      runtime.prepare_inflight = false;
      runtime.prepare_ready = result == 0;
      runtime.shutdown_failed = result >= 2;
      runtime.preparation_status = result;
    });
    if (admitted != detail::engine_admission_result::accepted) {
      std::lock_guard<std::mutex> lock(runtime.mutex);
      if (runtime.shutdown_generation == generation) runtime.prepare_inflight = false;
    }
    return failure("E_RUNTIME_SHUTDOWN_PENDING", "Worker prepare has not completed; retry this retained shutdown generation.");
  }
  {
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (!runtime.prepare_ready)
      return failure("E_RUNTIME_SHUTDOWN_PENDING", "Worker raw tasks, callback payloads or native backing are not retired.");
  }
  // Both kernel and engine perform only held-HANDLE zero-time polls. No runtime
  // mutex spans callback, provider detach, payload destruction or native join.
  const auto stopped = engine->try_stop();
  if (stopped == engine_thread::stop_result::pending)
    return failure("E_RUNTIME_SHUTDOWN_PENDING", "Prepared finalization/native engine exit is pending.");
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (stopped != engine_thread::stop_result::stopped || !runtime.retired) {
    runtime.shutdown_failed = true;
    return failure("E_RUNTIME_SHUTDOWN_FAILED", "Finalization did not commit retirement; module and owner are retained.");
  }
  // Preserve the first successful terminal sample for this retained owner.
  // Incomplete reads never overwrite the cache and may be retried later.
  if (!runtime.terminal_engine_metrics_recorded) {
    diagnostics::queue candidate;
    const auto sampled = engine->read_runtime_metrics(candidate);
    if (sampled == diagnostics::status::ready || sampled == diagnostics::status::disabled) {
      runtime.terminal_engine_metrics = candidate;
      runtime.terminal_engine_metrics_recorded = true;
    }
  }
  return success("Miniblink process runtime shutdown completed.");
#else
  return failure("E_UNSUPPORTED", "The Miniblink runtime requires Windows.");
#endif
}

host_result wait_for_miniblink_runtime_shutdown(std::chrono::milliseconds timeout) {
#ifdef _WIN32
  struct quit_message {
    bool seen{false};
    int code{0};
    ~quit_message() { if (seen) PostQuitMessage(code); }
  } quit;
  const auto now = [] { return std::chrono::steady_clock::now(); };
  return detail::wait_runtime_shutdown(timeout, shutdown_miniblink_runtime,
      [&](std::chrono::milliseconds quantum) {
        const auto began = now();
        MSG message{};
        for (unsigned count = 0; count != 32 && now() - began < quantum; ++count) {
          if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) break;
          if (message.message == WM_QUIT) {
            if (!quit.seen) { quit.seen = true; quit.code = static_cast<int>(message.wParam); }
          } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
          }
        }
        const auto remaining = quantum - std::chrono::duration_cast<std::chrono::milliseconds>(now() - began);
        if (remaining.count() > 0 && MsgWaitForMultipleObjects(0, nullptr, FALSE,
            static_cast<DWORD>(remaining.count()), QS_ALLINPUT) == WAIT_FAILED)
          return failure("E_RUNTIME_SHUTDOWN_WAIT", "The application shutdown message wait failed.");
        return success();
      }, now);
#else
  return detail::wait_runtime_shutdown(timeout, shutdown_miniblink_runtime,
      [](std::chrono::milliseconds) { return host_result{false, "E_UNSUPPORTED", "Windows is required."}; },
      [] { return std::chrono::steady_clock::now(); });
#endif
}

diagnostics::renderer_status read_miniblink_renderer_metrics(
    diagnostics::renderer_observation& out) noexcept {
  out = {};
#ifdef _WIN32
  try {
    auto& runtime = process_runtime();
    std::unique_lock<std::mutex> lock(runtime.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      out.state = diagnostics::renderer_status::busy;
      return out.state;
    }
    read_renderer_observer(runtime, out);
    return out.state;
  } catch (...) { out = {}; }
#endif
  return out.state;
}

diagnostics::renderer_status read_miniblink_renderer_model_metrics(
    diagnostics::renderer_model_observation& out) noexcept {
  out = {};
#ifdef _WIN32
  try {
    auto& runtime = process_runtime();
    std::unique_lock<std::mutex> lock(runtime.mutex, std::try_to_lock);
    if (!lock.owns_lock()) { out.state = diagnostics::renderer_status::busy; return out.state; }
    read_renderer_model_observer(runtime, out); return out.state;
  } catch (...) { out = {}; }
#endif
  return out.state;
}

diagnostics::cache_status request_miniblink_cache_sample() noexcept {
#ifdef _WIN32
  process_runtime_state* retained = nullptr;
  engine_thread* engine = nullptr;
  HMODULE module = nullptr;
  wkeCacheDiagnosticsApi api{};
  diagnostics::cache_observation envelope;
  std::uint64_t token = 0;
  try {
    auto& runtime = process_runtime();
    retained = &runtime;
    {
      std::unique_lock<std::mutex> lock(runtime.mutex, std::try_to_lock);
      if (!lock.owns_lock() || runtime.starting) return diagnostics::cache_status::busy;
      if (!runtime.thread) return diagnostics::cache_status::not_started;
      if (runtime.renderer_mode != diagnostics::mode::scalar) return diagnostics::cache_status::disabled;
      if (!runtime.initialized || runtime.shutdown_pending || runtime.retired ||
          !diagnostics::detail::cache_api_supported(&runtime.cache_api) || runtime.cache_binding.invalid())
        return diagnostics::cache_status::unavailable;
      if (runtime.cache_slot.inflight()) return diagnostics::cache_status::pending;
      token = runtime.cache_slot.begin();
      if (!token) return diagnostics::cache_status::unavailable;
      engine = runtime.thread.get(); module = runtime.module; api = runtime.cache_api;
      envelope.module_path = runtime.renderer_module_path;
      envelope.module_matches = runtime.renderer_module_matches;
      runtime.cache_completed.state = diagnostics::cache_status::pending;
    }
    const auto admitted = detail::engine_admission::post(*engine, detail::engine_lane::ordinary,
        [&runtime, engine, module, api, token, envelope = std::move(envelope)]() mutable {
      wkeCacheSnapshot candidate{}; candidate.structSize = sizeof(candidate);
      unsigned result = WKE_CACHE_UNAVAILABLE;
      {
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (runtime.starting || runtime.shutdown_pending || runtime.retired || !runtime.initialized ||
            runtime.thread.get() != engine || runtime.module != module) return;
      }
      // No Runtime mutex spans component locks or DLL invocation. This runs on Engine.
      try { result = api.sampleExistingCaches(&candidate); } catch (...) { result = WKE_CACHE_UNAVAILABLE; }
      std::lock_guard<std::mutex> lock(runtime.mutex);
      if (runtime.thread.get() != engine || runtime.module != module || runtime.shutdown_pending ||
          runtime.retired || !runtime.cache_slot.finish(token)) return;
      auto& completed = runtime.cache_completed;
      completed.state = result == WKE_CACHE_BUSY ? diagnostics::cache_status::busy : diagnostics::cache_status::unavailable;
      if (result != WKE_CACHE_READY || candidate.status != result ||
          !runtime.cache_binding.observe(candidate, runtime.renderer_pid, runtime.renderer_creation)) return;
      envelope.native = candidate;
      envelope.identity_bound = true; envelope.has_completed_sample = true;
      envelope.state = diagnostics::cache_status::sampled;
      completed = std::move(envelope); // Fixed fields and std::string move; no allocation during publication.
    });
    if (admitted == detail::engine_admission_result::accepted) return diagnostics::cache_status::pending;
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.cache_slot.finish(token)) runtime.cache_completed.state =
        admitted == detail::engine_admission_result::full ? diagnostics::cache_status::busy : diagnostics::cache_status::unavailable;
    return runtime.cache_completed.state;
  } catch (...) {
    if (retained && token) {
      try {
        std::lock_guard<std::mutex> lock(retained->mutex);
        if (retained->cache_slot.finish(token)) retained->cache_completed.state = diagnostics::cache_status::unavailable;
      } catch (...) {} // A failed Runtime mutex cannot produce a valid completed sample.
    }
  }
#endif
  return diagnostics::cache_status::unavailable;
}

diagnostics::cache_status read_miniblink_cache_metrics(diagnostics::cache_observation& out) noexcept {
  out = {};
#ifdef _WIN32
  try {
    auto& runtime = process_runtime();
    std::unique_lock<std::mutex> lock(runtime.mutex, std::try_to_lock);
    if (!lock.owns_lock() || runtime.starting) { out.state = diagnostics::cache_status::busy; return out.state; }
    if (!runtime.thread) return out.state;
    if (runtime.renderer_mode != diagnostics::mode::scalar) { out.state = diagnostics::cache_status::disabled; return out.state; }
    out = runtime.cache_completed;
    out.inflight = runtime.cache_slot.inflight(); out.runtime_retired = runtime.retired;
    if (!diagnostics::detail::cache_api_supported(&runtime.cache_api)) out.state = diagnostics::cache_status::unavailable;
    return out.state;
  } catch (...) { out = {}; out.state = diagnostics::cache_status::unavailable; }
#endif
  return out.state;
}

diagnostics::status read_miniblink_process_runtime_metrics(
    diagnostics::process_engine_runtime& out) noexcept {
  out = {};
#ifdef _WIN32
  try {
    auto& runtime = process_runtime();
    std::unique_lock<std::mutex> lock(runtime.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      out.engine_fifo.observation.state = diagnostics::status::busy;
      return diagnostics::status::busy;
    }
    out.mounted_hosts = static_cast<std::uint64_t>(runtime.hosts);
    out.initialized = runtime.initialized.load(std::memory_order_acquire);
    out.retired = runtime.retired.load(std::memory_order_acquire);
    if (out.retired && runtime.terminal_engine_metrics_recorded) {
      out.engine_fifo = runtime.terminal_engine_metrics;
      out.terminal_snapshot = true;
      return out.engine_fifo.observation.state;
    }
    if (runtime.thread) return runtime.thread->read_runtime_metrics(out.engine_fifo);
    out.engine_fifo.observation.state = diagnostics::status::not_started;
    return diagnostics::status::not_started;
  } catch (...) {
    out = {};
    out.engine_fifo.observation.state = diagnostics::status::unavailable;
    return diagnostics::status::unavailable;
  }
#else
  out.engine_fifo.observation.state = diagnostics::status::unavailable;
  return diagnostics::status::unavailable;
#endif
}

#ifdef _WIN32
void WKE_CALL_TYPE document_ready_callback(wkeWebView, void* param);
void WKE_CALL_TYPE main_document_ready_callback(wkeWebView, void* param, wkeWebFrameHandle);
bool WKE_CALL_TYPE document_navigation_callback(wkeWebView, void* param, wkeNavigationType, wkeString url);
void WKE_CALL_TYPE script_context_created_callback(wkeWebView view, void* param, wkeWebFrameHandle frame, void*, int, int world);
jsValue WKE_CALL_TYPE native_post_message(jsExecState es, void* param);
jsValue WKE_CALL_TYPE native_window_chrome(jsExecState es, void* param);
#endif

bool dynamic_miniblink_adapter::create_input_window() {
#ifdef _WIN32
  if (owner_window_ == nullptr) return false;
  DWORD owner_process = 0;
  if (!IsWindow(static_cast<HWND>(owner_window_)) ||
      GetWindowThreadProcessId(static_cast<HWND>(owner_window_), &owner_process) != GetCurrentThreadId() ||
      owner_process != GetCurrentProcessId()) return false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_DBLCLKS;
  window_class.lpfnWndProc = &interaction_window_proc;
  window_class.hInstance = instance;
  // Client cursor selection is handled explicitly. A class arrow must not
  // overwrite a queue-local custom cursor after WM_SETCURSOR returns.
  window_class.hCursor = nullptr;
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  window_class.lpszClassName = kInteractionWindowClass;
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  RECT bounds{};
  GetClientRect(static_cast<HWND>(owner_window_), &bounds);
  const int width = std::max(1L, bounds.right - bounds.left);
  const int height = std::max(1L, bounds.bottom - bounds.top);
  const HWND child = CreateWindowExW(
      0, kInteractionWindowClass, L"", WS_CHILD | (startup_window_protected_ ? 0 : WS_VISIBLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, width, height, static_cast<HWND>(owner_window_), nullptr, instance, this);
  if (!child) return false;
  webview_window_ = child;
  window_chrome_.set_viewport_width(width);
  return true;
#else
  return false;
#endif
}

bool dynamic_miniblink_adapter::destroy_input_window() {
  window_chrome_.clear();
  pressed_chrome_ = chrome_action::none;
#ifdef _WIN32
  const HWND child = static_cast<HWND>(webview_window_);
  if (child != nullptr && IsWindow(child) && !DestroyWindow(child) && IsWindow(child))
    return false;
  webview_window_ = nullptr;
#endif
  return true;
}

void dynamic_miniblink_adapter::refresh_wheel_settings() noexcept {
#ifdef _WIN32
  UINT lines = 3;
  UINT characters = 1;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    lines = 3;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &characters, 0))
    characters = 1;
  wheel_scroll_lines_.store(lines, std::memory_order_relaxed);
  wheel_scroll_characters_.store(characters, std::memory_order_relaxed);
#endif
}

struct dynamic_miniblink_adapter::event_dispatch_lifetime {
  std::mutex mutex;
  dynamic_miniblink_adapter* adapter{nullptr};
};

engine_thread* detail::engine_admission::host_engine(dynamic_miniblink_adapter& adapter) noexcept {
  // The immutable construction owner must be checked before plain lifecycle fields.
  if (adapter.metrics_owner_thread_ != std::this_thread::get_id()) return nullptr;
  if (!adapter.engine_thread_ || !adapter.webview_ || !adapter.owner_window_ || !adapter.cursor_broker_)
    return nullptr;
  return adapter.engine_thread_->state() == engine_thread_state::running ? adapter.engine_thread_ : nullptr;
}

dynamic_miniblink_adapter::dynamic_miniblink_adapter()
    : dynamic_miniblink_adapter(diagnostics::mode::disabled) {}
dynamic_miniblink_adapter::dynamic_miniblink_adapter(diagnostics::mode metrics) : metrics_mode_{metrics} {}
diagnostics::status dynamic_miniblink_adapter::read_runtime_metrics(diagnostics::software_host_runtime& out) const noexcept {
  out = {};
  if (metrics_mode_ != diagnostics::mode::scalar) return diagnostics::status::disabled;
  // Only the retained owner may read the mutable lifecycle pointers. No pump,
  // Engine work, receipt callback or application function runs in this getter.
  if (std::this_thread::get_id() != metrics_owner_thread_) {
    out.requests.observation.state = out.bridge_worker_fifo.observation.state =
        out.process_engine_fifo.observation.state = diagnostics::status::wrong_owner_thread;
    return diagnostics::status::wrong_owner_thread;
  }
  try {
    out.context_receipt_before = execution_context_receipt();
    out.bridge_accepting = bridge_accepting_.load(std::memory_order_acquire);
    const auto budget = bridge_budget_;
    out.requests.observation.state = diagnostics::status::not_started;
    out.bridge_worker_fifo.observation.state = diagnostics::status::not_started;
    out.process_engine_fifo.observation.state = diagnostics::status::not_started;
    if (budget) (void)budget->read_runtime_metrics(out.requests);
    if (bridge_thread_) (void)bridge_thread_->read_runtime_metrics(out.bridge_worker_fifo);
    if (engine_thread_) (void)engine_thread_->read_runtime_metrics(out.process_engine_fifo);
    out.context_receipt_after = execution_context_receipt();
    return diagnostics::detail::combine(out.requests.observation.state,
        diagnostics::detail::combine(out.bridge_worker_fifo.observation.state, out.process_engine_fifo.observation.state));
  } catch (...) {
    out = {}; out.requests.observation.state = out.bridge_worker_fifo.observation.state =
        out.process_engine_fifo.observation.state = diagnostics::status::unavailable;
    return diagnostics::status::unavailable;
  }
}
dynamic_miniblink_adapter::~dynamic_miniblink_adapter() {
  // Public lifecycle calls must finish before destroying an adapter. In
  // particular, destruction from its own active native modal frame is invalid.
  if (!unmount().ok) std::terminate();
}

host_result dynamic_miniblink_adapter::mount(const host_config& config) {
#ifdef _WIN32
  if (config.owner_window == nullptr)
    return failure("E_HOST_WINDOW_MISSING", "A SOUI host window is required for the child WebView.");
  if (engine_thread_ != nullptr || webview_window_ != nullptr)
    return failure("E_ALREADY_STARTED", "The Miniblink engine thread is already running.");
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif

  struct startup_admission_guard {
    std::atomic<bool>& accepting;
    bool complete{false};
    ~startup_admission_guard() { if (!complete) accepting.store(false, std::memory_order_release); }
  } admission{bridge_accepting_};
  startup_window_protected_ = true;
  startup_dispatch_refused_ = false;
  startup_refused_message_ = {};
  startup_cancelled_.store(false, std::memory_order_release);
  bridge_accepting_.store(false, std::memory_order_release);
  auto failed_startup = [this](host_result result) {
    bridge_accepting_.store(false, std::memory_order_release);
    (void)unmount(); // An unresolved original teardown retains its callback/HWND owners.
    return result;
  };

  owner_window_ = config.owner_window;
  bridge_adapter_ = config.bridge_adapter;
  event_lifetime_ = std::make_shared<event_dispatch_lifetime>();
  event_lifetime_->adapter = this;
  // SystemParametersInfo can take tens of milliseconds on its first call.
  // Resolve wheel metrics before the user-visible input path is armed.
  refresh_wheel_settings();
  if (!create_input_window()) {
    owner_window_ = nullptr;
    bridge_adapter_ = nullptr;
    return failure("E_WEBVIEW_WINDOW", "SOUI could not create the independent interaction child window.");
  }

  if (startup_cancelled_.load(std::memory_order_acquire) || !startup_window_matches())
    return failed_startup(failure("E_CURSOR_STARTUP_CANCELLED", "The startup window owner is unavailable."));

  RECT client{};
  GetClientRect(static_cast<HWND>(webview_window_), &client);
  const int width = std::max(1L, client.right - client.left);
  const int height = std::max(1L, client.bottom - client.top);
  interaction_generation_.view += 1;
  if (!interaction_generation_.view) interaction_generation_.view = 1;
  interaction_generation_.document = 1;
  interaction_generation_.geometry = 1;
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();
  cursor_close_pending_ = cursor_vendor_destroyed_ = false;
  cursor_transition_pending_ = false;

  const auto acquired = acquire_runtime(config, engine_thread_, module_, metrics_mode_);
  if (!acquired.ok) {
    if (!destroy_input_window())
      return failure("E_INPUT_WINDOW_CLOSE_PENDING", "The failed mount still owns its input window; retry cleanup.");
    owner_window_ = nullptr; bridge_adapter_ = nullptr;
    return acquired;
  }
  auto& runtime = process_runtime();
  if (runtime.cursor_realm.low || runtime.cursor_realm.high) {
    if (!invalidate_main_context()) {
      (void)unmount();
      return failure("E_CONTEXT_GENERATION", "The controlled context receipt is exhausted.");
    }
    cursor_api_ = &runtime.cursor_api;
    try { cursor_broker_ = detail::cursor_broker_endpoint::create(
        static_cast<HWND>(webview_window_), cursor_identity(interaction_generation_)); }
    catch (const std::bad_alloc&) { /* report through the ordinary host result */ }
    if (!cursor_broker_) {
      (void)unmount();
      return failure("E_CURSOR_BROKER_STORAGE", "The bounded cursor storage could not be created.");
    }
  }
  // Arm the worker before loading any page on the shared engine owner.
  bridge_generation_.store(1, std::memory_order_release);
  bridge_budget_ = std::make_shared<detail::bridge_request_budget>(metrics_mode_);
  bridge_thread_ = std::make_unique<engine_thread>(metrics_mode_);
  if (!bridge_thread_->start(
          [this] {
            bridge_com_initialized_ =
                SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            return bridge_com_initialized_;
          },
          [this] {
            if (bridge_com_initialized_) CoUninitialize();
            bridge_com_initialized_ = false;
          })) {
    bridge_thread_.reset();
    (void)unmount();
    return failure("E_BRIDGE_THREAD", "The asynchronous native bridge worker could not start.");
  }
  // The gate stays closed until complete setup and actual controlled registration.

  host_result startup = failure("E_DEPENDENCY_INIT", "Miniblink engine startup did not run.");
  const host_config engine_config = config;
  const auto invoked = invoke_lifecycle(engine_thread_,
      [this, engine_config, width, height, &startup] {
        startup = mount_on_engine(engine_config, width, height);
      });
  if (!invoked) startup=engine_admission_failure(invoked.admission,"The Engine did not admit startup.");
  if (!invoked || !startup.ok) return failed_startup(std::move(startup));
  if (cursor_broker_) {
    const auto registered = wait_for_cursor_registration();
    if (!registered.ok) return failed_startup(registered);
    startup = failure("E_CURSOR_STARTUP_FINALIZE", "The controlled startup setup did not finish.");
    const auto finalized = invoke_lifecycle(engine_thread_,
        [this, engine_config, width, height, &startup] {
          startup = finish_mount_on_engine(engine_config, width, height);
        });
    if (!finalized) startup=engine_admission_failure(finalized.admission,"The Engine did not admit startup finalization.");
    if (!finalized || !startup.ok) return failed_startup(std::move(startup));
  }
  if (startup_cancelled_.load(std::memory_order_acquire) || !startup_window_matches())
    return failed_startup(failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled before showing the child."));

  try {
    input_mailbox_ = std::make_shared<detail::input_mailbox>([this](const input_packet* packet) {
      if (packet) dispatch_input_on_engine(*packet);
      else cancel_input_on_engine();
    });
  } catch (...) { return failed_startup(failure("E_INPUT_CAPACITY", "The bounded input mailbox could not be allocated.")); }
  if (!detail::engine_admission::register_input(*engine_thread_, input_mailbox_))
    return failed_startup(failure("E_INPUT_CAPACITY", "The process already owns the maximum 32 input Hosts."));
  interaction_router_.activate(interaction_generation_);
  ShowWindow(static_cast<HWND>(webview_window_), SW_SHOW);
  if (startup_cancelled_.load(std::memory_order_acquire) || !startup_window_matches())
    return failed_startup(failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled while showing the child."));
#ifdef SOUI_TEST_KIT
  if (soui::test_kit::enabled()) {
    test_agent_ = std::make_unique<soui::test_kit::pipe_agent>(
        [this](const soui::test_kit::json& request) { return test_kit_request(request); },
        static_cast<HWND>(owner_window_), config.architecture);
  } else
#endif
  SetFocus(static_cast<HWND>(webview_window_));
  if (startup_cancelled_.load(std::memory_order_acquire) || !startup_window_matches())
    return failed_startup(failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled while focusing the child."));
  startup_window_protected_ = false;
  admission.complete = true;
  return success("SOUI interaction window mounted on the shared Miniblink engine thread.");
#else
  (void)config;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::mount_on_engine(const host_config& config, int width, int height) {
#ifdef _WIN32
  if (config.owner_window == nullptr) {
    return failure("E_HOST_WINDOW_MISSING", "A SOUI host window is required for the child WebView.");
  }
  if (config.miniblink_binary.empty()) {
    return failure("E_DEPENDENCY_MISSING", "Miniblink binary path is required.");
  }

#ifdef SOUI_BRIDGE_BENCHMARK
  if(config.noopt_experiment) {
    const auto opt = reinterpret_cast<const bool*>(GetProcAddress(static_cast<HMODULE>(module_),"?FLAG_opt@internal@v8@@3_NA"));
    std::ofstream check(config.benchmark_output_directory/"v8-noopt.json");
    check<<nlohmann::json{{"requested",true},{"readable",opt!=nullptr},{"optEnabled",opt?*opt:true}}.dump();
  }
#endif
  if (wkeCreateWebView == nullptr || wkeDestroyWebView == nullptr || wkeSetHandle == nullptr ||
      wkeGetInteractionApi == nullptr || wkeLoadFileW == nullptr ||
      wkeOnNavigation == nullptr || wkeOnDidCreateScriptContext == nullptr || wkeIsMainFrame == nullptr || wkeGetString == nullptr ||
      wkeSetNavigationToNewWindowEnable == nullptr ||
      wkeSetContextMenuEnabled == nullptr ||
      (cursor_broker_ ? wkeOnDocumentReady2 == nullptr : wkeOnDocumentReady == nullptr) || wkeJsBindFunction == nullptr ||
      wkeResize == nullptr || wkeFireMouseEvent == nullptr || wkeFireMouseWheelEvent == nullptr ||
      wkeFireKeyDownEvent == nullptr || wkeFireKeyUpEvent == nullptr || wkeFireKeyPressEvent == nullptr ||
      wkeRunJS == nullptr || wkeIsProcessingUserGesture == nullptr || jsGetWebView == nullptr ||
      wkeGetURL == nullptr || wkeGlobalExec == nullptr || jsIsString == nullptr || jsBoolean == nullptr ||
      (config.managed_frame_timing && wkeSetDebugConfig == nullptr)) {
    return failure("E_DEPENDENCY_API", "The installed Miniblink package lacks the required WebView API.");
  }

  managed_frame_timing_ = config.managed_frame_timing;
  host_managed_pump_ = managed_frame_timing_;
  char other_thread_paint_option[2] = {};
  experimental_other_thread_paint_ =
      GetEnvironmentVariableA("SOUI_PAINT_CALLBACK_OTHER_THREAD", other_thread_paint_option,
                              sizeof(other_thread_paint_option)) == 1 &&
      other_thread_paint_option[0] == '1';
  char compositor_rate_option[2] = {};
  experimental_compositor_rate_ =
      GetEnvironmentVariableA("SOUI_COMPOSITOR_FRAME_RATE", compositor_rate_option,
                              sizeof(compositor_rate_option)) == 1 &&
      compositor_rate_option[0] == '1';
  configured_wake_interval_ = -1;
  configured_draw_min_interval_ = -1;
  configured_main_frame_rate_ = -1;
#ifdef SOUI_SOURCE_KERNEL_HOST
  char main_frame_option[2] = {};
  experimental_main_frame_scheduler_ = GetEnvironmentVariableA("SOUI_SOURCE_MAIN_FRAME_SCHEDULER", main_frame_option, sizeof(main_frame_option)) == 1 && main_frame_option[0] == '1';
#endif
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_dataset_ = config.benchmark_dataset;
#endif
  resource_root_ = canonical_path(config.resource_root);
  gpu_trace_active_ = false;
  gpu_trace_path_.clear();
  char gpu_trace_option[MAX_PATH] = {};
  const DWORD gpu_trace_length = GetEnvironmentVariableA("SOUI_GPU_PRESENT_TRACE", gpu_trace_option,
                                                         sizeof(gpu_trace_option));
  if (gpu_trace_length > 0 && gpu_trace_length < sizeof(gpu_trace_option))
    gpu_trace_path_ = std::filesystem::path{gpu_trace_option};
#ifdef SOUI_BRIDGE_BENCHMARK
  cycle_evidence_path = resource_root_.parent_path() / "task-cycles.ndjson";
  gallery_checkpoint_path = config.benchmark_output_directory.empty() ? std::filesystem::path{} : config.benchmark_output_directory / "gallery-checkpoints.ndjson";
  gallery_checkpoint_count = 0;
#endif
  wkeWebView view = nullptr;
  if (cursor_broker_) {
    auto& runtime = process_runtime();
    wkeCursorBrokerEndpointConfig endpoint_config{};
    endpoint_config.structSize = sizeof(endpoint_config);
    endpoint_config.ownerWindow = static_cast<HWND>(webview_window_);
    endpoint_config.identity = cursor_identity(interaction_generation_);
    endpoint_config.hooks = cursor_broker_->hooks();
    wkeCursorBrokerEndpointReceipt receipt{}; receipt.structSize = sizeof(receipt);
    const auto status = runtime.cursor_api.createEndpoint(runtime.cursor_realm, &endpoint_config, &receipt);
    const bool bound = cursor_broker_->bind(receipt);
    if (status != WKE_CURSOR_BROKER_OK || !bound)
      return failure("E_CURSOR_ENDPOINT", "The controlled view could not be created and bound.");
    view = receipt.webView;
  } else view = wkeCreateWebView();
  if (view == nullptr) {
    return failure("E_WEBVIEW_CREATE", "Miniblink could not create a child WebView.");
  }
  webview_ = view;
#ifdef SOUI_BRIDGE_BENCHMARK
  append_host_smoke_log(resource_root_, "webview-created");
#endif
  if (!cursor_broker_) wkeSetHandle(view, static_cast<HWND>(webview_window_));

  const auto* interaction_api = wkeGetInteractionApi(WKE_INTERACTION_ABI_VERSION);
  if (interaction_api == nullptr || interaction_api->structSize < sizeof(wkeInteractionApi)
      || interaction_api->abiVersion != WKE_INTERACTION_ABI_VERSION
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_DIRECT_WHEEL) == 0
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_CLICK_BARRIER) == 0
      || (interaction_api->capabilities & WKE_INTERACTION_CAP_DIRECT_PRESENTATION) == 0
      || interaction_api->createEndpoint == nullptr || interaction_api->submitWheel == nullptr
      || interaction_api->configurePresentation == nullptr
      || interaction_api->captureScrollBarrier == nullptr
      || interaction_api->synchronizeScrollBarrier == nullptr
      || interaction_api->invalidateEndpoint == nullptr || interaction_api->releaseEndpoint == nullptr) {
    if (!cursor_broker_) { wkeDestroyWebView(view); webview_ = nullptr; }
    return failure("E_INDEPENDENT_INTERACTION_UNAVAILABLE",
                   "The Miniblink binary does not expose the required independent interaction ABI.");
  }
  interaction_api_ = interaction_api;
  interaction_endpoint_ = interaction_api->createEndpoint(view, interaction_generation_.view);
  if (interaction_endpoint_ == nullptr) {
    if (!cursor_broker_) { wkeDestroyWebView(view); webview_ = nullptr; }
    return failure("E_INTERACTION_ENDPOINT", "Miniblink could not create the compositor interaction endpoint.");
  }
  if (!interaction_api->configurePresentation(interaction_endpoint_,
          WKE_INTERACTION_PRESENT_DIRECT_HWND)) {
    interaction_api->releaseEndpoint(interaction_endpoint_);
    interaction_endpoint_ = nullptr;
    if (!cursor_broker_) { wkeDestroyWebView(view); webview_ = nullptr; }
    return failure("E_INDEPENDENT_PRESENTATION",
                   "Miniblink could not enable compositor-owned window presentation.");
  }

  if (cursor_broker_)
    return success("Controlled endpoint primed without the initial navigation.");
  return finish_mount_on_engine(config, width, height);
#else
  (void)config; (void)width; (void)height;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::finish_mount_on_engine(const host_config& config, int width, int height) {
#ifdef _WIN32
  struct startup_admission_guard {
    std::atomic<bool>& accepting;
    bool complete{false};
    ~startup_admission_guard() { if (!complete) accepting.store(false, std::memory_order_release); }
  } admission{bridge_accepting_};
  const auto view = static_cast<wkeWebView>(webview_);
  if (!view) return failure("E_WEBVIEW_CREATE", "The startup view is unavailable.");
  wkeSetNavigationToNewWindowEnable(view, false);
  wkeSetContextMenuEnabled(view, false);
  if (cursor_broker_) wkeOnDocumentReady2(view, &main_document_ready_callback, this);
  else wkeOnDocumentReady(view, &document_ready_callback, this);
  wkeOnNavigation(view, &document_navigation_callback, this);
  wkeOnDidCreateScriptContext(view, &script_context_created_callback, this);
  if (managed_frame_timing_) {
    // This framework owns an explicit standard profile, not an inferred vendor default.
    wkeSetDebugConfig(view, "wakeMinInterval", "5");
    configured_wake_interval_ = 5;
  }
  if (experimental_other_thread_paint_ && wkeSetDebugConfig)
    wkeSetDebugConfig(view, "paintcallbackInOtherThread", "1");
  char direct_gpu_present_option[2] = {};
  if (GetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", direct_gpu_present_option,
                              sizeof(direct_gpu_present_option)) == 1 &&
      direct_gpu_present_option[0] == '1' && wkeSetDebugConfig)
    wkeSetDebugConfig(view, "gpuPresent", "1");
#ifdef SOUI_BRIDGE_BENCHMARK
  if (config.engine_timing_experiment && wkeSetDebugConfig) {
    // Isolated-process experiment only. The original values cannot be queried
    // reliably from these DLLs, so this is intentionally not a runtime toggle.
    if(config.engine_timing_experiment & 1) wkeSetDebugConfig(view, "wakeMinInterval", "0");
    if(config.engine_timing_experiment & 2) wkeSetDebugConfig(view, "drawMinInterval", "0");
  }
#endif
  if (wkeOnPaintUpdated) wkeOnPaintUpdated(view, &record_live_paint, bridge_adapter_);
  process_runtime().views.emplace(view, this);
  wkeJsBindFunction("__souiNativePostMessage", &native_post_message, nullptr, 1);
  wkeJsBindFunction("__souiNativeWindowChrome", &native_window_chrome, nullptr, 1);
#ifdef SOUI_BRIDGE_BENCHMARK
  wkeJsBindFunction("__souiBenchmarkMarkPhase", &mark_benchmark_phase, nullptr, 1);
  wkeJsBindFunction("__souiBenchmarkWriteCycle", &write_benchmark_cycle, nullptr, 1);
  wkeJsBindFunction("__souiBenchmarkMotion", &benchmark_motion_callback, this, 1);
  wkeJsBindFunction("__souiBenchmarkClick", &benchmark_click_callback, this, 2);
  wkeJsBindFunction("__souiBenchmarkText", &benchmark_text_callback, this, 0);
  wkeJsBindFunction("__souiBenchmarkGalleryCheckpoint", &gallery_checkpoint, nullptr, 2);
#endif
  if (bridge_adapter_ != nullptr) {
    auto& bridge = *static_cast<bridge_adapter*>(bridge_adapter_);
    bridge_set_owner_window(bridge, owner_window_, 0);
    if (!install_bridge_event_sink(execution_context_receipt()))
      return failure("E_BRIDGE_SINK", "The native event sink could not be installed.");
  }

  // React is the product surface.  The original Vue reference remains a
  // deliberate compatibility entry (`--entry=index.html`) but must not be
  // the default, otherwise the commercial React shell, 4K player and full
  // component catalog are never exercised by a normal F5/demo launch.
  std::filesystem::path entry = resource_root_ / "react.html";
  if (!std::filesystem::is_regular_file(entry))
    entry = resource_root_ / "index.html";
  char entry_option[MAX_PATH] = {};
  const DWORD entry_length = GetEnvironmentVariableA("SOUI_ENTRY_FILE", entry_option, sizeof(entry_option));
  if (entry_length > 0 && entry_length < sizeof(entry_option)) {
    const auto candidate = canonical_path(resource_root_ / std::filesystem::path{entry_option});
    const auto root = canonical_path(resource_root_);
    const auto root_text = root.wstring();
    const auto candidate_text = candidate.wstring();
    if (candidate_text.rfind(root_text, 0) == 0 && std::filesystem::is_regular_file(candidate))
      entry = candidate;
  }
  const auto index = entry.wstring();
  if (cursor_broker_) {
    const auto completion = cursor_broker_->ole_registration_completion();
    if (!completion.observed || !completion.shape_valid || completion.cancelled ||
        !completion.endpoint_matches || !completion.identity_matches || !cursor_broker_->input_allowed())
      return failure("E_CURSOR_STARTUP_IDENTITY", "The confirmed registration identity changed during setup.");
    const auto* api = static_cast<const wkeCursorBrokerApi*>(cursor_api_);
    wkeCursorBrokerDrainReceipt drain{}; drain.structSize = sizeof(drain);
    const auto token = cursor_broker_->token();
    const auto status = api->inspectDrain(token, &drain);
    if (status != WKE_CURSOR_BROKER_OK || drain.structSize != sizeof(drain) ||
        drain.endpoint.low != token.low || drain.endpoint.high != token.high ||
        drain.closing || drain.oleRegistered != 1 || drain.queuedUiTasks || drain.runningUiTasks ||
        drain.nativeScopes || drain.engineCallbacks)
      return failure("E_CURSOR_STARTUP_REGISTRATION", "The controlled registration is no longer quiet.");
  }
  {
    // Serialize cancellation and the last admission commit; no native call holds this lock.
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (startup_cancelled_.load(std::memory_order_acquire))
      return failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled before the initial navigation.");
    bridge_accepting_.store(true, std::memory_order_release);
  }
  wkeLoadFileW(view, index.c_str());
#ifdef SOUI_BRIDGE_BENCHMARK
  append_host_smoke_log(resource_root_, "local-page-requested");
#endif
  wkeResize(view, width, height);
  if (startup_cancelled_.load(std::memory_order_acquire))
    return failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled during the initial navigation.");
  admission.complete = true;
  return success("Miniblink WebView bound to the SOUI-owned interaction window.");
#else
  (void)config;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

bool dynamic_miniblink_adapter::startup_window_matches() const noexcept {
#ifdef _WIN32
  const auto child = static_cast<HWND>(webview_window_);
  const auto owner = static_cast<HWND>(owner_window_);
  DWORD child_process = 0, owner_process = 0;
  return child && owner && IsWindow(child) && IsWindow(owner) && GetParent(child) == owner &&
      GetWindowThreadProcessId(child, &child_process) == GetCurrentThreadId() &&
      GetWindowThreadProcessId(owner, &owner_process) == GetCurrentThreadId() &&
      child_process == GetCurrentProcessId() && owner_process == GetCurrentProcessId() &&
      GetWindowLongPtrW(child, GWLP_USERDATA) == reinterpret_cast<LONG_PTR>(this);
#else
  return false;
#endif
}

host_result dynamic_miniblink_adapter::wait_for_cursor_registration() {
#ifdef _WIN32
  if (!cursor_broker_ || !engine_thread_ || !valid_cursor_api(static_cast<const wkeCursorBrokerApi*>(cursor_api_)))
    return failure("E_CURSOR_STARTUP_REGISTRATION", "The controlled startup owner is unavailable.");
  constexpr UINT_PTR task_timer = 0x15324547U; // Actual controlled vendor SetTimer contract.
  constexpr ULONGLONG admission_ms = 5000;
  const auto started = GetTickCount64();
  const auto expected = cursor_broker_->identity();
  const auto token = cursor_broker_->token();
  const auto* api = static_cast<const wkeCursorBrokerApi*>(cursor_api_);
  auto same_expected = [expected](const wkeCursorBrokerIdentity& value) noexcept {
    return value.structSize == sizeof(value) && !value.reserved &&
        value.viewGeneration == expected.viewGeneration &&
        value.documentGeneration == expected.documentGeneration &&
        value.geometryGeneration == expected.geometryGeneration;
  };
  auto cancelled = [this] {
    return startup_cancelled_.load(std::memory_order_acquire) || !startup_window_matches();
  };
  auto refused_timer = [this](const MSG* removed) {
    startup_dispatch_refused_ = true;
    if (removed) {
      startup_refused_message_ = {removed->hwnd, removed->message, removed->wParam, removed->lParam,
          removed->time, removed->pt.x, removed->pt.y, true};
    }
    bridge_accepting_.store(false, std::memory_order_release);
    return removed
        ? failure("E_CURSOR_STARTUP_TIMER_CONSUMED", "A removed startup message was retained without dispatch; retain its owners.")
        : failure("E_CURSOR_STARTUP_TIMER", "The fixed startup timer source was rejected; retain its owners.");
  };
  auto timed_out = [started, admission_ms] { return GetTickCount64() - started >= admission_ms; };
  for (;;) {
    if (cancelled())
      return failure("E_CURSOR_STARTUP_CANCELLED", "The startup window was closed or lost.");
    if (timed_out())
      return failure("E_CURSOR_STARTUP_FENCE_TIMEOUT", "The controlled registration was not confirmed before admission closed.");
    const auto completion = cursor_broker_->ole_registration_completion();
    if (completion.observed) {
      if (!completion.shape_valid || completion.cancelled || !completion.endpoint_matches ||
          !completion.identity_matches || !same_expected(completion.receipt.identity) ||
          !cursor_broker_->input_allowed())
        return failure("E_CURSOR_STARTUP_IDENTITY", "The queued registration was cancelled or its identity changed.");
      wkeCursorBrokerDrainReceipt drain{}; drain.structSize = sizeof(drain);
      unsigned status = WKE_CURSOR_BROKER_FAILED;
      // The shared engine is never parked waiting for this owner; captured locals remain held until return.
      const auto queried=invoke_lifecycle(engine_thread_, [api, token, &drain, &status] { status = api->inspectDrain(token, &drain); });
      if (!queried) return engine_admission_failure(queried.admission,"The controlled registration query did not finish.");
      if (cancelled())
        return failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled during the registration query.");
      if (timed_out())
        return failure("E_CURSOR_STARTUP_FENCE_TIMEOUT", "The registration query returned after admission closed.");
      const auto current = cursor_broker_->ole_registration_completion();
      if (!current.observed || !current.shape_valid || current.cancelled ||
          !current.endpoint_matches || !current.identity_matches ||
          !same_expected(cursor_broker_->identity()) || !cursor_broker_->input_allowed())
        return failure("E_CURSOR_STARTUP_IDENTITY", "The confirmed startup identity changed.");
      if ((status != WKE_CURSOR_BROKER_OK && status != WKE_CURSOR_BROKER_BUSY) ||
          drain.structSize != sizeof(drain) || drain.endpoint.low != token.low ||
          drain.endpoint.high != token.high || drain.closing || drain.oleRegistered != 1)
        return failure("E_CURSOR_STARTUP_REGISTRATION", "The actual OLE registration was not valid.");
      // Completion runs before vendor running--. Only a later actual quiet query completes this fence.
      if (status == WKE_CURSOR_BROKER_OK && !drain.queuedUiTasks && !drain.runningUiTasks &&
          !drain.nativeScopes && !drain.engineCallbacks)
        return success("The actual controlled OLE registration is confirmed and quiet.");
      Sleep(1);
      continue;
    }

    MSG peeked{};
    const auto child = static_cast<HWND>(webview_window_);
    if (!PeekMessageW(&peeked, child, WM_TIMER, WM_TIMER, PM_NOREMOVE)) {
      Sleep(1);
      continue;
    }
    if (cancelled())
      return failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled while observing its timer.");
    MEMORY_BASIC_INFORMATION memory{};
    const auto procedure = reinterpret_cast<const void*>(peeked.lParam);
    const bool executable = VirtualQuery(procedure, &memory, sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE && memory.AllocationBase == module_ &&
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
        !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS));
    if (peeked.hwnd != child || peeked.message != WM_TIMER || peeked.wParam != task_timer ||
        !peeked.lParam || !executable || (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
      return refused_timer(nullptr); // Leave this unmatched message queued; never pass it to broad cleanup.
    if (timed_out())
      return failure("E_CURSOR_STARTUP_FENCE_TIMEOUT", "The startup timer arrived after admission closed.");
    MSG actual{};
    if (!PeekMessageW(&actual, child, WM_TIMER, WM_TIMER, PM_REMOVE))
      return refused_timer(nullptr);
    if (actual.hwnd != peeked.hwnd || actual.message != peeked.message || actual.wParam != peeked.wParam ||
        actual.lParam != peeked.lParam || actual.time != peeked.time ||
        actual.pt.x != peeked.pt.x || actual.pt.y != peeked.pt.y)
      return refused_timer(&actual); // Sent-message reentrancy can change the item removed: retain that exact MSG.
    if (cancelled())
      return failure("E_CURSOR_STARTUP_CANCELLED", "Startup was cancelled before its actual timer dispatch.");
    if (timed_out())
      return failure("E_CURSOR_STARTUP_FENCE_TIMEOUT", "The startup timer dispatch was not admitted before the deadline.");
    DispatchMessageW(&actual); // Actual verified OS-delivered timer; no manual TIMERPROC invocation.
  }
#else
  return failure("E_UNSUPPORTED", "The controlled startup fence requires Windows.");
#endif
}

host_result dynamic_miniblink_adapter::unmount() {
#ifdef _WIN32
  if (unmount_running_) return failure("E_BUSY", "WebView teardown is already in progress.");
  lifecycle_operation_guard unmount_guard(unmount_running_);
  input_focus_requested_.store(false, std::memory_order_release);
  bridge_accepting_.store(false, std::memory_order_release);
  if (execution_context_receipt()) (void)invalidate_main_context();
  else (void)detail::context_lookup_revision<>::advance(native_context_lookup_revision);
  // Finish native callbacks that could already be inside admission before
  // another thread stops and destroys the worker they access.
  if (engine_thread_ != nullptr) {
    const auto barrier=invoke_lifecycle(engine_thread_, [] {},detail::engine_lane::cleanup);
    if (!barrier) return engine_admission_failure(barrier.admission,"The engine could not stop admitting bridge requests.",true);
  }
#ifdef SOUI_TEST_KIT
  // Join the controller worker before invalidating its engine/adapter lifetime.
  test_agent_.reset();
#endif
  interaction_router_.deactivate();
  if (input_mailbox_ && engine_thread_) {
    const auto mailbox = input_mailbox_;
    mailbox->quiesce();
    const auto closed = invoke_lifecycle(engine_thread_, [this, mailbox] {
      cancel_input_on_engine();
      detail::engine_admission::unregister_input(*engine_thread_, mailbox);
    }, detail::engine_lane::cleanup);
    if (!closed) return engine_admission_failure(closed.admission, "Input owner retirement is pending.", true);
    input_mailbox_.reset();
  }
  wheel_smoothing_.reset();
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
  if (event_lifetime_) {
    std::lock_guard<std::mutex> lock(event_lifetime_->mutex);
    event_lifetime_->adapter = nullptr;
  }
  if (bridge_adapter_) bridge_adapter_->set_event_sink({});
  auto drain_events = [this] {
    if (owner_window_ == nullptr) return;
    MSG message{};
    while (PeekMessageW(&message, static_cast<HWND>(owner_window_), bridge_event_message,
                        bridge_event_message, PM_REMOVE) != FALSE) {
      delete static_cast<pending_event*>(reinterpret_cast<void*>(message.lParam));
    }
  };
  drain_events();
  if (bridge_thread_ != nullptr) {
    bridge_thread_->stop(false);
    bridge_thread_.reset();
  }
  if (engine_thread_ != nullptr) {
    const auto teardown=invoke_lifecycle(engine_thread_, [this] { unmount_on_engine(); },detail::engine_lane::cleanup);
    if (!teardown) return engine_admission_failure(teardown.admission,"The engine could not complete WebView teardown.",true);
    const auto cursor_closed = close_cursor_endpoint();
    if (!cursor_closed.ok) return cursor_closed;
    release_runtime();
    engine_thread_ = nullptr;
  }
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif
  drain_events();
  if (!destroy_input_window())
    return failure("E_INPUT_WINDOW_CLOSE_PENDING", "The input window is still alive; keep the Host and retry cleanup.");
  if (bridge_adapter_ != nullptr)
    bridge_set_owner_window(*static_cast<bridge_adapter*>(bridge_adapter_), nullptr, 0);
  owner_window_ = nullptr;
  bridge_adapter_ = nullptr;
  startup_window_protected_ = false; // Only after actual owned endpoint and child cleanup.
#endif
  document_ready_ = false;
  application_ready_ = false;
  detail::main_context_gate<>::reset_after_teardown(cursor_context_receipt_);
  return success("Miniblink WebView and endpoint unmounted; process runtime remains available.");
}

host_result dynamic_miniblink_adapter::close_cursor_endpoint() {
#ifdef _WIN32
  if (!cursor_broker_) { cursor_api_ = nullptr; return success(); }
  if (!cursor_broker_->begin_close())
    return failure("E_THREAD_AFFINITY", "Cursor teardown requires the window owner thread.");
  const auto token = cursor_broker_->token();
  if (!(token.low || token.high)) {
    // No vendor callback ownership was handed off and no controlled view was
    // admitted. This storage can be released even if another view is modal.
    cursor_broker_.reset(); cursor_api_ = nullptr; return success();
  }
  const auto* api = static_cast<const wkeCursorBrokerApi*>(cursor_api_);
  if (!valid_cursor_api(api) || !engine_thread_)
    return failure("E_CURSOR_BROKER_ABI", "The controlled cursor owner is unavailable.");
  if (!cursor_vendor_destroyed_) {
    unsigned requested = WKE_CURSOR_BROKER_FAILED;
    const auto request=invoke_lifecycle(engine_thread_, [this, api, token, &requested] {
      if (webview_) process_runtime().views.erase(static_cast<wkeWebView>(webview_));
      requested = api->requestClose(token);
    },detail::engine_lane::cleanup);
    if (!request) return failure(request.admission == detail::engine_admission_result::full ? "E_CURSOR_CLOSE_PENDING" : "E_CURSOR_CLOSE_FAILED",
        "The controlled endpoint did not accept teardown.");
    if (requested != WKE_CURSOR_BROKER_OK && requested != WKE_CURSOR_BROKER_FAILED)
      return failure("E_CURSOR_CLOSE_FAILED", "The controlled endpoint did not accept teardown.");
    cursor_close_pending_ = true;
    if (startup_dispatch_refused_)
      return failure("E_CURSOR_CLOSE_PENDING", "A startup timer was refused; retain the Host, module and borrowed owners.");
    if (cursor_broker_->native_scopes())
      return failure("E_CURSOR_CLOSE_PENDING", "Keep the host alive until its native modal call returns.");

    // Outer teardown preserves callback storage and the HWND while dispatching
    // only this owned child's UI tasks. A reentrant modal close returns above;
    // it cannot wait for its own stack frame to leave. There is no timeout that
    // invents completion, and no detach or FreeLibrary after an unfinished call.
    {
      struct close_result { unsigned status{WKE_CURSOR_BROKER_FAILED}; bool destroyed{false}; bool valid{false}; };
      auto result = std::make_shared<close_result>();
      auto completion = std::make_shared<std::promise<void>>();
      auto future = completion->get_future();
      const auto drain_admission=detail::engine_admission::post(*engine_thread_,detail::engine_lane::cleanup,[this, api, token, result, completion] {
            try {
              wkeCursorBrokerDrainReceipt receipt{}; receipt.structSize = sizeof(receipt);
              result->status = api->inspectDrain(token, &receipt);
              result->valid = receipt.structSize == sizeof(receipt) && receipt.endpoint.low == token.low &&
                  receipt.endpoint.high == token.high && receipt.closing == 1 && receipt.closeSequence &&
                  receipt.drained <= 1 && receipt.oleRegistered <= 1;
              const bool drained = result->valid && receipt.drained == 1 && !receipt.queuedUiTasks &&
                  !receipt.runningUiTasks && !receipt.nativeScopes && !receipt.engineCallbacks && !receipt.oleRegistered;
              if (drained && (result->status == WKE_CURSOR_BROKER_OK || result->status == WKE_CURSOR_BROKER_FAILED)) {
                const auto close_sequence = receipt.closeSequence;
                result->status = api->destroyEndpoint(token, &receipt);
                result->destroyed = result->status == WKE_CURSOR_BROKER_OK && receipt.structSize == sizeof(receipt) &&
                    receipt.endpoint.low == token.low && receipt.endpoint.high == token.high &&
                    receipt.closing == 1 && receipt.closeSequence == close_sequence && receipt.drained == 1 &&
                    !receipt.queuedUiTasks && !receipt.runningUiTasks && !receipt.nativeScopes &&
                    !receipt.engineCallbacks && !receipt.oleRegistered;
                if (result->destroyed) webview_ = nullptr;
              }
              completion->set_value();
            } catch (...) { completion->set_exception(std::current_exception()); }
          });
      if (drain_admission != detail::engine_admission_result::accepted)
        return engine_admission_failure(drain_admission,"The controlled owner did not accept the drain query.",true);
      auto dispatch_child = [this] {
        MSG message{};
        for (unsigned n = 0; n < 64 && PeekMessageW(&message, static_cast<HWND>(webview_window_),
                                                    0, 0, PM_REMOVE); ++n) {
          TranslateMessage(&message); DispatchMessageW(&message);
        }
      };
      while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        dispatch_child();
        (void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
      }
      try { future.get(); } catch (...) { return failure("E_CURSOR_CLOSE_FAILED", "The controlled drain query failed."); }
      if (!result->valid || (result->status != WKE_CURSOR_BROKER_OK &&
          result->status != WKE_CURSOR_BROKER_FAILED && result->status != WKE_CURSOR_BROKER_BUSY))
        return failure("E_CURSOR_CLOSE_FAILED", "The controlled drain receipt is incomplete.");
      if (result->destroyed) cursor_vendor_destroyed_ = true;
      else {
        dispatch_child();
        (void)SetTimer(static_cast<HWND>(webview_window_),
            reinterpret_cast<UINT_PTR>(&cursor_vendor_destroyed_), 50, nullptr);
        return failure("E_CURSOR_CLOSE_PENDING", "The controlled native resources and callbacks have not drained.");
      }
    }
  }
  if (!cursor_broker_->finish_close()) {
    // Retry only on this owned child. A failed native cleanup keeps the actual
    // ownership and blocks release; the timer is not evidence of completion.
    (void)SetTimer(static_cast<HWND>(webview_window_),
        reinterpret_cast<UINT_PTR>(&cursor_vendor_destroyed_), 50, nullptr);
    return failure("E_CURSOR_CLOSE_PENDING", "Cursor resource retirement has not completed its native proof.");
  }
  (void)KillTimer(static_cast<HWND>(webview_window_), reinterpret_cast<UINT_PTR>(&cursor_vendor_destroyed_));
  cursor_broker_.reset(); cursor_api_ = nullptr;
  cursor_close_pending_ = false; cursor_transition_pending_ = false;
#endif
  return success();
}

void dynamic_miniblink_adapter::unmount_on_engine() {
#ifdef _WIN32
  append_host_smoke_log(resource_root_, "adapter-unmount-start");
  std::string closing_session;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    closing_session.swap(session_id_);
  }
  if (bridge_adapter_ != nullptr && !closing_session.empty())
    bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
  append_host_smoke_log(resource_root_, "adapter-bridge-shutdown");
  const auto* interaction_api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (interaction_api != nullptr && interaction_endpoint_ != nullptr) {
    interaction_api->invalidateEndpoint(interaction_endpoint_);
    interaction_api->releaseEndpoint(interaction_endpoint_);
    interaction_endpoint_ = nullptr;
    interaction_api_ = nullptr;
  }
  append_host_smoke_log(resource_root_, "adapter-events-drained");
  if (webview_ != nullptr && wkeDestroyWebView != nullptr) {
    process_runtime().views.erase(static_cast<wkeWebView>(webview_));
    if (!cursor_broker_) { wkeDestroyWebView(static_cast<wkeWebView>(webview_)); webview_ = nullptr; }
  }
  append_host_smoke_log(resource_root_, webview_ ? "adapter-cursor-close-pending" : "adapter-webview-destroyed");
  module_ = nullptr; // borrowed module; only application exit finalizes it
  resource_root_.clear();
  gpu_trace_path_.clear();
  gpu_trace_active_ = false;
#endif
  document_ready_ = false;
  application_ready_ = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    probe_report_.clear();
  }
}

host_result dynamic_miniblink_adapter::show(bool visible) {
#ifdef _WIN32
  if (webview_window_ == nullptr || !IsWindow(static_cast<HWND>(webview_window_))) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  ShowWindow(static_cast<HWND>(webview_window_), visible ? SW_SHOW : SW_HIDE);
  return success(visible ? "WebView shown." : "WebView hidden without cancelling native tasks.");
#else
  (void)visible;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::reload() {
  window_chrome_.clear();
#ifdef _WIN32
  if (engine_thread_ == nullptr) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  std::uint64_t reload_receipt = 0;
  if (cursor_broker_) {
    // Revoke old binders and queued work before either owner can progress the
    // accepted transition. A failed request remains pending, never restored.
    if (!invalidate_main_context())
      return failure("E_CONTEXT_GENERATION", "The controlled context receipt is exhausted.");
    if (!cursor_broker_->request_document_change())
      return failure("E_CURSOR_CLOSING", "The controlled view is no longer accepting document changes.");
    // A context that finished between the first invalidation and the broker's
    // request flag must not complete this reload. Unarmed broker transitions
    // reject context ACKs; reserve a fresh receipt after setting that flag.
    reload_receipt = invalidate_main_context();
    if (!reload_receipt)
      return failure("E_CONTEXT_GENERATION", "The controlled context receipt is exhausted.");
    interaction_router_.deactivate();
  } else {
    interaction_generation_.document += 1;
    if (!interaction_generation_.document) interaction_generation_.document = 1;
    interaction_router_.activate(interaction_generation_);
  }
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
#ifdef SOUI_BRIDGE_BENCHMARK
  benchmark_result_slot_.reset();
#endif
  host_result result = failure("E_ENGINE_THREAD", "The reload did not reach the Miniblink owner thread.");
  const auto invoked = invoke_lifecycle(engine_thread_, [this, reload_receipt, &result] {
    const auto current = [this, reload_receipt] {
      return reload_receipt ? detail::main_context_gate<>::pending(cursor_context_receipt_, reload_receipt)
                            : execution_context_receipt() == 0;
    };
    if (!current()) { result = failure("E_CONTEXT_PENDING", "The reload was superseded by a context transition."); return; }
    if (webview_ == nullptr || wkeReload == nullptr) {
      result = failure("E_NOT_STARTED", "The child WebView has not been created.");
      return;
    }
    if (wkeRunJS != nullptr) {
      static constexpr const char* prepare_reload =
          "(function(){try{if(typeof window.__souiBeforeReload==='function')"
          "window.__souiBeforeReload();}catch(e){}})();";
      (void)wkeRunJS(static_cast<wkeWebView>(webview_), prepare_reload);
    }
    if (!current()) { result = failure("E_CONTEXT_PENDING", "The reload was superseded during preparation."); return; }
    std::string closing_session;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current()) { result = failure("E_CONTEXT_PENDING", "The reload was superseded during preparation."); return; }
      closing_session.swap(session_id_);
    }
    if (bridge_adapter_ != nullptr && !closing_session.empty())
      bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
    if (!current()) { result = failure("E_CONTEXT_PENDING", "The reload was superseded during cleanup."); return; }
    document_ready_ = false;
    application_ready_ = false;
    if (cursor_broker_ && !cursor_broker_->arm_document_change()) {
      result = failure("E_CURSOR_CLOSING", "The actual reload could not acquire its document transition.");
      return;
    }
    if (!current()) { result = failure("E_CONTEXT_PENDING", "The reload was superseded before execution."); return; }
    wkeReload(static_cast<wkeWebView>(webview_));
    result = success("WebView reload requested on the engine thread.");
  });
  return invoked ? result : engine_admission_failure(invoked.admission,"The Miniblink owner thread did not accept reload.");
#else
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
host_result dynamic_miniblink_adapter::request_memory_recovery() {
#ifdef _WIN32
  if (engine_thread_ == nullptr)
    return failure("E_NOT_STARTED", "The Miniblink engine thread is not running.");
  return detail::invoke_diagnostic_result(*engine_thread_, [] {
    using gc_timer = void(__cdecl*)(void*, long);
    const auto module = GetModuleHandleW(L"node.dll");
    const auto gc = reinterpret_cast<gc_timer>(GetProcAddress(module, "wkeGC"));
    const auto resource_gc = reinterpret_cast<gc_timer>(GetProcAddress(module, "wkeSetResourceGc"));
    if (!gc || !resource_gc) {
      return failure("E_GC_API_MISSING", "The Miniblink recovery timer APIs are unavailable.");
    }
    gc(nullptr, 1);
    resource_gc(nullptr, 1);
    return success("Miniblink recovery timers requested on the engine owner thread.");
  }, failure("E_ENGINE_THREAD", "The memory recovery request timed out on the Miniblink owner thread."));
#else
  return failure("E_UNSUPPORTED", "Memory recovery diagnostics require Windows.");
#endif
}
#endif

host_result dynamic_miniblink_adapter::resize(int width, int height) {
#ifdef _WIN32
  if (engine_thread_ == nullptr || webview_window_ == nullptr || width <= 0 || height <= 0) {
    return failure("E_INVALID_ARGUMENT", "A live WebView and positive dimensions are required.");
  }
  window_chrome_.set_viewport_width(width);
  if (cursor_broker_) {
    if (!cursor_broker_->advance_geometry())
      return failure("E_CURSOR_GENERATION", "The controlled geometry generation could not advance.");
    cursor_transition_pending_ = true;
    sync_cursor_input_generation();
  } else {
    interaction_generation_.geometry += 1;
    if (!interaction_generation_.geometry) interaction_generation_.geometry = 1;
    interaction_router_.activate(interaction_generation_);
  }
  next_input_sequence_ = 0;
  wheel_smoothing_.reset();
  MoveWindow(static_cast<HWND>(webview_window_), 0, 0, width, height, TRUE);
  const auto admitted=detail::engine_admission::post(*engine_thread_,detail::engine_lane::ordinary,[this, width, height] {
        if (cursor_broker_) (void)update_cursor_identity_on_engine();
        if (webview_ != nullptr && wkeResize != nullptr)
          wkeResize(static_cast<wkeWebView>(webview_), width, height);
      });
  if (admitted != detail::engine_admission_result::accepted)
    return engine_admission_failure(admitted,"The Miniblink owner thread rejected resize.");
  return success("SOUI window resized; engine resize queued.");
#else
  (void)width;
  (void)height;
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

host_result dynamic_miniblink_adapter::focus() {
#ifdef _WIN32
  if (engine_thread_ == nullptr || webview_window_ == nullptr) {
    return failure("E_NOT_STARTED", "The child WebView has not been created.");
  }
  SetFocus(static_cast<HWND>(webview_window_));
  const auto admitted=detail::engine_admission::post(*engine_thread_,detail::engine_lane::ordinary,[this] {
        if (webview_ != nullptr && wkeSetFocus != nullptr)
          wkeSetFocus(static_cast<wkeWebView>(webview_));
      });
  if (admitted != detail::engine_admission_result::accepted)
    return engine_admission_failure(admitted,"The Miniblink owner thread rejected focus.");
  return success("SOUI input window focused; engine focus queued.");
#else
  return failure("E_UNSUPPORTED", "The SOUI/Miniblink adapter requires a Windows host.");
#endif
}

void dynamic_miniblink_adapter::pump() {
#ifdef _WIN32
  if (engine_thread_ == nullptr || pump_pending_.exchange(true)) return;
  if (!engine_thread_->post([this] {
        pump_on_engine();
        pump_pending_ = false;
      })) {
    pump_pending_ = false;
  }
#endif
}

bool dynamic_miniblink_adapter::update_cursor_identity_on_engine() {
#ifdef _WIN32
  if (!cursor_broker_) return true;
  const auto receipt = execution_context_receipt();
  const auto view = webview_;
  const auto* broker = cursor_broker_.get();
  const auto* api = static_cast<const wkeCursorBrokerApi*>(cursor_api_);
  const auto identity = cursor_broker_->identity();
  if (!valid_cursor_api(api)) { cursor_transition_pending_ = true; return false; }
  const auto status = api->updateIdentity(cursor_broker_->token(), &identity);
  if (webview_ != view || cursor_broker_.get() != broker || execution_context_receipt() != receipt) {
    cursor_transition_pending_ = true;
    return false;
  }
  const auto latest = cursor_broker_->identity();
  const bool current = status == WKE_CURSOR_BROKER_OK &&
      identity.viewGeneration == latest.viewGeneration && identity.documentGeneration == latest.documentGeneration &&
      identity.geometryGeneration == latest.geometryGeneration;
  cursor_transition_pending_ = !current;
  // Every engine input/pump rechecks the full tuple, so a concurrent UI resize
  // cannot lose its pending update through this diagnostic flag.
  return current;
#else
  return true;
#endif
}

bool dynamic_miniblink_adapter::advance_cursor_document_on_engine() {
#ifdef _WIN32
  if (!cursor_broker_) return true;
  if (!cursor_broker_->advance_document()) { cursor_transition_pending_ = true; return false; }
  return update_cursor_identity_on_engine();
#endif
  return true;
}

void dynamic_miniblink_adapter::sync_cursor_input_generation() noexcept {
#ifdef _WIN32
  if (!cursor_broker_) return;
  const auto identity = cursor_broker_->identity();
  const interaction_generation current{identity.viewGeneration, identity.documentGeneration, identity.geometryGeneration};
  if (!(interaction_generation_ == current)) {
    interaction_generation_ = current;
    next_input_sequence_ = 0;
    wheel_smoothing_.reset();
  }
  const auto receipt = execution_context_receipt();
  if (accepts_execution_context_receipt(receipt) && cursor_broker_->input_allowed() &&
      accepts_execution_context_receipt(receipt)) {
    if (!(interaction_router_.generation() == current)) interaction_router_.activate(current);
  }
  else interaction_router_.deactivate();
#endif
}

void dynamic_miniblink_adapter::pump_on_engine() {
#ifdef _WIN32
  if (cursor_broker_) (void)update_cursor_identity_on_engine();
  if (webview_ != nullptr && wkeUpdate != nullptr) {
    bool gpu_active = false;
    if (bridge_adapter_ != nullptr && webview_window_ != nullptr) {
      char active_option[2] = {};
      const bool process_active = GetEnvironmentVariableA("SOUI_GPU_ACTIVE", active_option,
                                                          sizeof(active_option)) == 1 && active_option[0] == '1';
      if (!gpu_trace_active_ && !gpu_trace_path_.empty()) {
        std::ifstream trace(gpu_trace_path_, std::ios::in | std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(trace)), std::istreambuf_iterator<char>());
        if (content.find("swapped=1") != std::string::npos)
          gpu_trace_active_ = true;
      }
      gpu_active = gpu_trace_active_ ||
          GetPropA(static_cast<HWND>(webview_window_), "SOUI_GPU_ACTIVE") != nullptr || process_active;
      static_cast<bridge_adapter*>(bridge_adapter_)->set_gpu_active(gpu_active);
      const auto present_property = GetPropA(static_cast<HWND>(webview_window_), "SOUI_GPU_PRESENT_COUNT");
      std::uint64_t present_count = present_property
          ? static_cast<std::uint64_t>(reinterpret_cast<ULONG_PTR>(present_property))
          : 0;
      if (!present_property) {
        char present_option[32] = {};
        const DWORD present_length = GetEnvironmentVariableA("SOUI_GPU_PRESENT_COUNT",
                                                               present_option, sizeof(present_option));
        if (present_length && present_length < sizeof(present_option))
          present_count = _strtoui64(present_option, nullptr, 10);
      }
      static_cast<bridge_adapter*>(bridge_adapter_)->set_present_count(present_count);
    }
    const bool high_rate = bridge_adapter_ &&
        static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() > 60;
#ifdef SOUI_SOURCE_KERNEL_HOST
    if (managed_frame_timing_ && wkeSetDebugConfig) {
      const int rate = experimental_main_frame_scheduler_ ?
          (bridge_adapter_ ? static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() : 60) : 0;
      if (configured_main_frame_rate_ != rate) {
        const auto value = std::to_string(rate);
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "mainFrameRate", value.c_str());
        configured_main_frame_rate_ = rate;
      }
    }
#endif
    if (managed_frame_timing_ && wkeSetDebugConfig) {
      const int desired = high_rate ? 0 : 5;
      if (configured_wake_interval_ != desired) {
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "wakeMinInterval", high_rate ? "0" : "5");
        configured_wake_interval_ = desired;
      }
    }
    if (wkeSetDebugConfig) {
      const int rate = bridge_adapter_ ? static_cast<bridge_adapter*>(bridge_adapter_)->active_frame_rate() : 60;
      // The GPU path is paced by the display clock and can draw as soon as a
      // dirty frame is ready. CPU-safe fallback follows the same selected
      // integer display-limited target so the reported draw budget matches the actual
      // cadence; the engine may still miss a pulse when the CPU cannot keep up.
      char cpu_high_option[2] = {};
      const bool cpu_high_refresh = GetEnvironmentVariableA("SOUI_CPU_HIGH_REFRESH", cpu_high_option,
                                                            sizeof(cpu_high_option)) == 1 && cpu_high_option[0] == '1';
      char cpu_interval_override[16] = {};
      const DWORD cpu_interval_length = GetEnvironmentVariableA("SOUI_CPU_DRAW_INTERVAL_MS",
                                                                 cpu_interval_override, sizeof(cpu_interval_override));
      const int interval = experimental_compositor_rate_
          ? (rate > 0 ? (1000 + rate - 1) / rate : 17)
          : (gpu_active ? 0 : (cpu_interval_length > 0 && cpu_interval_length < sizeof(cpu_interval_override)
                                ? std::clamp(std::atoi(cpu_interval_override), 0, 1000)
                                : ((cpu_high_refresh || rate > 60) && rate > 60
                                    ? (1000 + rate - 1) / rate
                                    : 17)));
      if (configured_draw_min_interval_ != interval) {
        const auto value = std::to_string(interval);
        wkeSetDebugConfig(static_cast<wkeWebView>(webview_), "drawMinInterval", value.c_str());
        configured_draw_min_interval_ = interval;
      }
      if (bridge_adapter_ != nullptr)
        static_cast<bridge_adapter*>(bridge_adapter_)->set_draw_interval(
            configured_draw_min_interval_ < 0 ? 0U : static_cast<unsigned>(configured_draw_min_interval_));
    }
    wkeUpdate();
    if (high_rate && wkeWake) {
      wkeWake(static_cast<wkeWebView>(webview_));
    }
    if (wkeRepaintIfNeeded != nullptr &&
        (wkeIsDirty == nullptr || wkeIsDirty(static_cast<wkeWebView>(webview_))))
      wkeRepaintIfNeeded(static_cast<wkeWebView>(webview_));
  }
#endif
}

input_packet dynamic_miniblink_adapter::make_input_packet(input_kind kind) noexcept {
  sync_cursor_input_generation();
  input_packet packet;
  packet.kind = kind;
  packet.generation = interaction_generation_;
  packet.sequence = next_input_sequence_.fetch_add(1) + 1;
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  packet.timestamp_seconds = std::chrono::duration<double>(now).count();
  return packet;
}

bool dynamic_miniblink_adapter::submit_wheel(const input_packet& packet,
                                             scroll_sample& sample) {
#ifdef _WIN32
  const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (api == nullptr || interaction_endpoint_ == nullptr || api->submitWheel == nullptr)
    return false;
  // Ctrl+wheel is page zoom, not scrolling. Keep it on Blink's ordinary
  // input path. Windows wheel deltas also have the opposite sign from scroll
  // offsets and are expressed in WHEEL_DELTA ticks rather than CSS pixels.
  if ((packet.modifiers & MK_CONTROL) != 0)
    return false;
  static constexpr double scrollbar_pixels_per_line = 100.0 / 3.0;
  const double wheel_ticks = packet.delta_y / static_cast<double>(WHEEL_DELTA);
  const std::uint32_t wheel_scroll_lines =
      wheel_scroll_lines_.load(std::memory_order_relaxed);
  const std::uint32_t wheel_scroll_characters =
      wheel_scroll_characters_.load(std::memory_order_relaxed);
  double compositor_delta_x = 0.0;
  double compositor_delta_y = 0.0;
  if ((packet.modifiers & MK_SHIFT) != 0) {
    compositor_delta_x = -wheel_ticks
        * static_cast<double>(wheel_scroll_characters)
        * scrollbar_pixels_per_line;
  } else {
    compositor_delta_y = -wheel_ticks
        * (wheel_scroll_lines == WHEEL_PAGESCROLL
               ? 1.0
               : static_cast<double>(wheel_scroll_lines)
                   * scrollbar_pixels_per_line);
  }
  wkeInteractionWheelInput input{};
  input.structSize = sizeof(input);
  input.flags = packet.modifiers;
  input.viewGeneration = packet.generation.view;
  input.documentGeneration = packet.generation.document;
  input.geometryVersion = packet.generation.geometry;
  input.inputSequence = packet.sequence;
  input.gestureId = packet.gesture;
  input.monotonicTime = packet.timestamp_seconds;
  input.x = packet.x;
  input.y = packet.y;
  input.deltaX = compositor_delta_x;
  input.deltaY = compositor_delta_y;
  input.smooth = wheel_smoothing_.should_smooth(packet.timestamp_seconds) ? 1 : 0;
  wkeInteractionScrollSample output{};
  output.structSize = sizeof(output);
  if (!api->submitWheel(interaction_endpoint_, &input, &output) || !output.accepted)
    return false;
  sample.scene_version = output.sceneVersion;
  sample.scroll_sequence = output.inputSequence;
  sample.offset_x = output.offsetX;
  sample.offset_y = output.offsetY;
  return true;
#else
  (void)packet;
  (void)sample;
  return false;
#endif
}

bool dynamic_miniblink_adapter::capture_scroll_barrier(
    const input_packet& packet, scroll_sample& sample) {
#ifdef _WIN32
  const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
  if (api == nullptr || interaction_endpoint_ == nullptr
      || api->captureScrollBarrier == nullptr)
    return false;

  wkeInteractionBarrierRequest request{};
  request.structSize = sizeof(request);
  request.flags = packet.modifiers;
  request.viewGeneration = packet.generation.view;
  request.documentGeneration = packet.generation.document;
  request.geometryVersion = packet.generation.geometry;
  request.x = packet.x;
  request.y = packet.y;
  wkeInteractionScrollBarrier output{};
  output.structSize = sizeof(output);
  if (!api->captureScrollBarrier(interaction_endpoint_, &request, &output)
      || !output.valid)
    return false;

  sample.scene_version = output.sceneVersion;
  sample.view_id = output.viewId;
  sample.node_id = output.nodeId;
  sample.node_generation = output.nodeGeneration;
  sample.scroll_epoch = output.scrollEpoch;
  sample.sample_sequence = output.sampleSequence;
  sample.scroll_sequence = output.inputSequence;
  sample.consumed_total_x = output.consumedTotalX;
  sample.consumed_total_y = output.consumedTotalY;
  sample.offset_x = output.offsetX;
  sample.offset_y = output.offsetY;
  return true;
#else
  (void)packet;
  (void)sample;
  return false;
#endif
}

bool dynamic_miniblink_adapter::post_input(const input_packet& packet) {
  return post_input_typed(packet) == input_admission_result::accepted;
}

input_admission_result dynamic_miniblink_adapter::post_input_typed(const input_packet& packet) {
  if (!engine_thread_ || !input_mailbox_) return input_admission_result::stopped;
  const auto mailbox = std::static_pointer_cast<detail::input_mailbox>(input_mailbox_);
  const auto result = mailbox->post(packet);
  if (mailbox->pending()) detail::engine_admission::notify_input(*engine_thread_);
  return result;
}

void dynamic_miniblink_adapter::cancel_input_on_engine() {
#ifdef _WIN32
  if (!webview_) return;
  const auto view = static_cast<wkeWebView>(webview_);
  for (std::size_t key = 0; key < input_keys_on_engine_.size(); ++key) {
    if (!input_keys_on_engine_[key]) continue;
    input_keys_on_engine_[key] = false;
    if (wkeFireKeyUpEvent) (void)wkeFireKeyUpEvent(view, static_cast<unsigned>(key), 0, false);
    if (webview_ != view) return;
  }
  if (input_pointer_on_engine_) {
    input_pointer_on_engine_ = false;
    if (wkeFireMouseEvent) (void)wkeFireMouseEvent(view, WM_LBUTTONUP, input_pointer_x_, input_pointer_y_, 0);
    if (webview_ != view) return;
  }
  if (ime_composing_ && !ime_committed_ && wkeCancelComposition) (void)wkeCancelComposition(view);
  ime_composing_ = ime_committed_ = false;
  if (webview_ != view) return;
  if (wkeKillFocus) wkeKillFocus(view);
  if (webview_ == view && input_focus_requested_.load(std::memory_order_acquire) && wkeSetFocus) wkeSetFocus(view);
#endif
}

void dynamic_miniblink_adapter::dispatch_input_on_engine(input_packet packet) {
#ifdef _WIN32
  const auto context_receipt = execution_context_receipt();
  if (!accepts_execution_context_receipt(context_receipt)) return;
  if (cursor_broker_) {
    if (!cursor_broker_->input_allowed() || !update_cursor_identity_on_engine() ||
        !accepts_execution_context_receipt(context_receipt) || !cursor_broker_->input_allowed()) return;
    const auto identity = cursor_broker_->identity();
    if (!(packet.generation == interaction_generation{identity.viewGeneration,
          identity.documentGeneration, identity.geometryGeneration})) return;
  }
  if (webview_ == nullptr || !(packet.generation == interaction_router_.generation())) return;
  const auto view = static_cast<wkeWebView>(webview_);
  switch (packet.kind) {
    case input_kind::wheel:
      if (wkeFireMouseWheelEvent != nullptr)
        (void)wkeFireMouseWheelEvent(view, packet.x, packet.y,
                                     static_cast<int>(packet.delta_y), packet.modifiers);
      break;
    case input_kind::pointer_move:
    case input_kind::pointer_down:
    case input_kind::pointer_up: {
      if (wkeFireMouseEvent == nullptr) break;
      if (packet.kind == input_kind::pointer_down
          && packet.barrier.scene_version != 0) {
        const auto* api = static_cast<const wkeInteractionApi*>(interaction_api_);
        if (api == nullptr || interaction_endpoint_ == nullptr
            || api->synchronizeScrollBarrier == nullptr)
          break;
        wkeInteractionScrollBarrier barrier{};
        barrier.structSize = sizeof(barrier);
        barrier.valid = 1;
        barrier.viewGeneration = packet.generation.view;
        barrier.sceneVersion = packet.barrier.scene_version;
        barrier.viewId = packet.barrier.view_id;
        barrier.nodeId = packet.barrier.node_id;
        barrier.nodeGeneration = packet.barrier.node_generation;
        barrier.scrollEpoch = packet.barrier.scroll_epoch;
        barrier.sampleSequence = packet.barrier.sample_sequence;
        barrier.inputSequence = packet.barrier.scroll_sequence;
        barrier.consumedTotalX = packet.barrier.consumed_total_x;
        barrier.consumedTotalY = packet.barrier.consumed_total_y;
        barrier.offsetX = packet.barrier.offset_x;
        barrier.offsetY = packet.barrier.offset_y;
        if (!api->synchronizeScrollBarrier(interaction_endpoint_, &barrier))
          break;
        if (webview_ != view || !accepts_execution_context_receipt(context_receipt)) break;
      }
      unsigned int message = WM_MOUSEMOVE;
      input_pointer_x_ = packet.x; input_pointer_y_ = packet.y;
      if (packet.kind == input_kind::pointer_down) input_pointer_on_engine_ = true;
      if (packet.kind == input_kind::pointer_up) input_pointer_on_engine_ = false;
      if (packet.kind == input_kind::pointer_down)
        message = packet.double_click ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
      if (packet.kind == input_kind::pointer_up) message = WM_LBUTTONUP;
      (void)wkeFireMouseEvent(view, message, packet.x, packet.y, packet.modifiers);
      break;
    }
    case input_kind::key_down:
      if (packet.key_code < input_keys_on_engine_.size()) input_keys_on_engine_[packet.key_code] = true;
      if (wkeFireKeyDownEvent != nullptr)
        (void)wkeFireKeyDownEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::key_up:
      if (packet.key_code < input_keys_on_engine_.size()) input_keys_on_engine_[packet.key_code] = false;
      if (wkeFireKeyUpEvent != nullptr)
        (void)wkeFireKeyUpEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::character:
      if (wkeFireKeyPressEvent != nullptr)
        (void)wkeFireKeyPressEvent(view, packet.key_code, packet.modifiers, packet.system_key);
      break;
    case input_kind::focus:
      if (wkeSetFocus != nullptr) wkeSetFocus(view);
      break;
    case input_kind::blur:
      if (wkeKillFocus != nullptr) wkeKillFocus(view);
      break;
    case input_kind::ime_commit:
      if (wkeConfirmComposition != nullptr) {
        const std::wstring text(packet.text.begin(), packet.text.end());
        (void)wkeConfirmComposition(view, text.c_str());
      } else if (wkeFireKeyPressEvent != nullptr) {
        // Older packaged kernels do not expose the composition extension.
        // Preserve committed text input while source-kernel packages use the
        // full marked-text path above.
        for (char16_t character : packet.text) {
          if (webview_ != view || !accepts_execution_context_receipt(context_receipt)) break;
          (void)wkeFireKeyPressEvent(view,
                                    static_cast<unsigned int>(character),
                                    packet.modifiers, false);
        }
      }
      ime_composing_ = false;
      ime_committed_ = true;
      break;
    case input_kind::ime_start:
      ime_composing_ = true;
      ime_committed_ = false;
      if (wkeGetCaretRect != nullptr && webview_window_ != nullptr) {
        const wkeRect caret = wkeGetCaretRect(view);
        ime_caret_x_.store(caret.x, std::memory_order_release);
        ime_caret_y_.store(caret.y + caret.h, std::memory_order_release);
        PostMessageW(static_cast<HWND>(webview_window_), ime_position_message,
                     static_cast<WPARAM>(caret.x),
                     static_cast<LPARAM>(caret.y + caret.h));
      }
      break;
    case input_kind::ime_update:
      if (wkeSetComposition != nullptr) {
        const std::wstring text(packet.text.begin(), packet.text.end());
        (void)wkeSetComposition(view, text.c_str(), packet.selection_start,
                                packet.selection_end);
        ime_composing_ = true;
      }
      break;
    case input_kind::ime_end:
      if (ime_composing_ && !ime_committed_ && wkeCancelComposition != nullptr)
        (void)wkeCancelComposition(view);
      ime_composing_ = false;
      ime_committed_ = false;
      break;
    case input_kind::capture_lost:
      if (input_pointer_on_engine_ && wkeFireMouseEvent)
        (void)wkeFireMouseEvent(view, WM_LBUTTONUP, input_pointer_x_, input_pointer_y_, 0);
      input_pointer_on_engine_ = false;
      break;
    case input_kind::cancel:
      cancel_input_on_engine();
      break;
  }
#else
  (void)packet;
#endif
}

bool dynamic_miniblink_adapter::handle_native_window_message(
    unsigned int message, std::uintptr_t wparam_value, std::intptr_t lparam_value,
    std::intptr_t& result) {
#ifdef _WIN32
  const WPARAM wparam = static_cast<WPARAM>(wparam_value);
  const LPARAM lparam = static_cast<LPARAM>(lparam_value);
  const HWND child = static_cast<HWND>(webview_window_);
  auto route = [this](input_packet packet) {
    const auto routed = interaction_router_.route(packet, *this, *this);
    // Rejection is an explicit cancellation/recovery outcome, never success.
    if (routed.disposition == route_disposition::rejected_sink) {
      if (webview_window_ && GetCapture() == static_cast<HWND>(webview_window_)) ReleaseCapture();
      wheel_smoothing_.reset();
    }
  };
  auto pointer_packet = [this, lparam](input_kind kind, WPARAM flags) {
    input_packet packet = make_input_packet(kind);
    packet.x = GET_X_LPARAM(lparam);
    packet.y = GET_Y_LPARAM(lparam);
    packet.modifiers = static_cast<std::uint32_t>(flags);
    return packet;
  };

  auto chrome_hit = [&] {
    RECT bounds{};
    GetClientRect(child, &bounds);
    return window_chrome_.hit(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), bounds.right);
  };
  const auto owner = static_cast<HWND>(owner_window_);

  switch (message) {
    case detail::cursor_descriptor_message:
      // Consume the coalesced wake even during pending close. Otherwise the
      // real outer native leave could be unable to post its final retry.
      if (cursor_broker_) (void)cursor_broker_->process_descriptor();
      if (cursor_close_pending_ && !unmount_running_) (void)unmount();
      result = 0;
      return true;
    case WM_TIMER:
      if (wparam == reinterpret_cast<UINT_PTR>(&cursor_vendor_destroyed_) && cursor_close_pending_) {
        (void)KillTimer(child, wparam);
        if (!unmount_running_) (void)unmount();
        result = 0;
        return true;
      }
      break;
    case WM_CLOSE:
      if (startup_window_protected_) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        startup_cancelled_.store(true, std::memory_order_release);
        bridge_accepting_.store(false, std::memory_order_release);
        result = 0;
        return true;
      }
      if (unmount_running_ || cursor_close_pending_) { result = 0; return true; }
      break;
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT && reinterpret_cast<HWND>(wparam) == child) {
        if (cursor_broker_) (void)cursor_broker_->select();
        else (void)SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        result = TRUE;
        return true;
      }
      break;
    case WM_SETTINGCHANGE:
      refresh_wheel_settings();
      break;
    case WM_NCHITTEST: {
      // The full-client child must not swallow SOUI's native resize border.
      const LRESULT hit = SendMessageW(owner, WM_NCHITTEST, wparam, lparam);
      if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT) {
        result = HTTRANSPARENT;
        return true;
      }
      break;
    }
    case WM_CAPTURECHANGED:
      route(make_input_packet(input_kind::capture_lost));
      pressed_chrome_ = chrome_action::none;
      break;
    case WM_CANCELMODE:
      route(make_input_packet(input_kind::cancel));
      pressed_chrome_ = chrome_action::none;
      break;
    case ime_position_message: {
      const HIMC context = ImmGetContext(child);
      if (context != nullptr) {
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT | CFS_FORCE_POSITION;
        form.ptCurrentPos.x = static_cast<LONG>(wparam);
        form.ptCurrentPos.y = static_cast<LONG>(lparam);
        (void)ImmSetCompositionWindow(context, &form);
        ImmReleaseContext(child, context);
      }
      result = 0;
      return true;
    }
    case WM_ERASEBKGND:
      result = 1;
      return true;
    case WM_SETFOCUS:
      input_focus_requested_.store(true, std::memory_order_release);
      route(make_input_packet(input_kind::focus));
      result = 0;
      return true;
    case WM_KILLFOCUS:
      input_focus_requested_.store(false, std::memory_order_release);
      route(make_input_packet(input_kind::blur));
      result = 0;
      return true;
    case WM_MOUSEMOVE:
      route(pointer_packet(input_kind::pointer_move, wparam));
      result = 0;
      return true;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
      const auto hit = chrome_hit();
      if (hit == chrome_action::drag) {
        pressed_chrome_ = chrome_action::none;
        (void)soui::runtime::execute_window_command(owner,
            message == WM_LBUTTONDBLCLK ? soui::runtime::window_command::toggle_maximize
                                        : soui::runtime::window_command::begin_drag);
        result = 0;
        return true;
      }
      if (hit != chrome_action::none) {
        pressed_chrome_ = hit;
        SetCapture(child);
        result = 0;
        return true;
      }
      SetFocus(child);
      SetCapture(child);
      auto packet = pointer_packet(input_kind::pointer_down, wparam);
      packet.double_click = message == WM_LBUTTONDBLCLK;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_LBUTTONUP: {
      if (pressed_chrome_ != chrome_action::none) {
        const auto pressed = pressed_chrome_;
        pressed_chrome_ = chrome_action::none;
        if (GetCapture() == child) ReleaseCapture();
        if (pressed == chrome_hit()) {
          if (pressed == chrome_action::close) PostMessageW(owner, WM_CLOSE, 0, 0);
          else (void)soui::runtime::execute_window_command(owner,
              pressed == chrome_action::minimize ? soui::runtime::window_command::minimize
                                                 : soui::runtime::window_command::toggle_maximize);
        }
        result = 0;
        return true;
      }
      route(pointer_packet(input_kind::pointer_up, wparam));
      if (GetCapture() == child) ReleaseCapture();
      result = 0;
      return true;
    }
    case WM_MOUSEWHEEL: {
      input_packet packet = make_input_packet(input_kind::wheel);
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(child, &point);
      packet.x = point.x;
      packet.y = point.y;
      packet.delta_y = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam));
      packet.modifiers = static_cast<std::uint32_t>(GET_KEYSTATE_WPARAM(wparam));
      packet.gesture = 1;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      input_packet packet = make_input_packet(input_kind::key_down);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      packet.modifiers = static_cast<std::uint32_t>(HIWORD(lparam));
      packet.system_key = message == WM_SYSKEYDOWN;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      input_packet packet = make_input_packet(input_kind::key_up);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      packet.modifiers = static_cast<std::uint32_t>(HIWORD(lparam));
      packet.system_key = message == WM_SYSKEYUP;
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_CHAR: {
      input_packet packet = make_input_packet(input_kind::character);
      packet.key_code = static_cast<std::uint32_t>(wparam);
      route(std::move(packet));
      result = 0;
      return true;
    }
    case WM_IME_STARTCOMPOSITION:
      route(make_input_packet(input_kind::ime_start));
      result = 0;
      return true;
    case WM_IME_COMPOSITION: {
      const HIMC context = ImmGetContext(child);
      if (context != nullptr) {
        bool text_rejected = false;
        auto copy_text = [context, &text_rejected](DWORD selector) {
          std::u16string text;
          const LONG bytes = ImmGetCompositionStringW(context, selector, nullptr, 0);
          if (bytes > 0) {
            if (bytes > static_cast<LONG>(detail::input_mailbox::event_byte_limit)
                || bytes % sizeof(char16_t)) { text_rejected = true; return text; }
            try { text.resize(static_cast<std::size_t>(bytes) / sizeof(char16_t)); }
            catch (...) { text_rejected = true; return text; }
            const LONG copied = ImmGetCompositionStringW(context, selector, text.data(), bytes);
            if (copied != bytes) { text_rejected = true; text.clear(); }
          }
          return text;
        };
        if ((lparam & GCS_COMPSTR) != 0) {
          input_packet packet = make_input_packet(input_kind::ime_update);
          packet.text = copy_text(GCS_COMPSTR);
          if (text_rejected) {
            route(make_input_packet(input_kind::cancel));
            ImmReleaseContext(child, context);
            result = 0; return true;
          }
          const LONG cursor = ImmGetCompositionStringW(
              context, GCS_CURSORPOS, nullptr, 0);
          packet.selection_start = cursor >= 0 ? static_cast<int>(cursor)
                                               : static_cast<int>(packet.text.size());
          packet.selection_end = packet.selection_start;
          route(std::move(packet));
        }
        if ((lparam & GCS_RESULTSTR) != 0) {
          input_packet packet = make_input_packet(input_kind::ime_commit);
          packet.text = copy_text(GCS_RESULTSTR);
          if (text_rejected) {
            route(make_input_packet(input_kind::cancel));
            ImmReleaseContext(child, context);
            result = 0; return true;
          }
          route(std::move(packet));
        }
        ImmReleaseContext(child, context);
      }
      result = 0;
      return true;
    }
    case WM_IME_ENDCOMPOSITION:
      route(make_input_packet(input_kind::ime_end));
      result = 0;
      return true;
    case WM_IME_REQUEST:
      if (wparam == IMR_QUERYCHARPOSITION && lparam != 0) {
        auto* position = reinterpret_cast<IMECHARPOSITION*>(lparam);
        POINT caret{ime_caret_x_.load(std::memory_order_acquire),
                    ime_caret_y_.load(std::memory_order_acquire)};
        ClientToScreen(child, &caret);
        position->pt = caret;
        position->cLineHeight = 18;
        GetClientRect(child, &position->rcDocument);
        POINT document_origin{position->rcDocument.left,
                              position->rcDocument.top};
        ClientToScreen(child, &document_origin);
        OffsetRect(&position->rcDocument, document_origin.x,
                   document_origin.y);
        result = 1;
        return true;
      }
      return false;
    default:
      return false;
  }
  return false;
#else
  (void)message;
  (void)wparam_value;
  (void)lparam_value;
  (void)result;
  return false;
#endif
}

bool dynamic_miniblink_adapter::document_ready() const noexcept {
  const auto receipt = execution_context_receipt();
  return accepts_execution_context_receipt(receipt) && document_ready_.load(std::memory_order_acquire) &&
      document_ready_receipt_.load(std::memory_order_acquire) == receipt &&
      accepts_execution_context_receipt(receipt);
}

#ifdef SOUI_TEST_KIT
soui::test_kit::json dynamic_miniblink_adapter::test_kit_request(const soui::test_kit::json& request) {
  using soui::test_kit::json;
  using soui::test_kit::failed;
  const auto context_receipt = execution_context_receipt();
  const auto generation = bridge_generation_.load(std::memory_order_acquire);
  if (!accepts_execution_context_receipt(context_receipt)) return failed("NOT_READY");
  const auto method = request.value("method", std::string{});
  if (method == "memory.sample") {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return failed("MEMORY_SAMPLE");
    const auto timestamp = GetTickCount64();
    if (!accepts_execution_context_receipt(context_receipt) ||
        generation != bridge_generation_.load(std::memory_order_acquire)) return failed("STALE_DOCUMENT");
    return {{"ok", true}, {"result", {{"privateBytes", memory.PrivateUsage}, {"workingSetBytes", memory.WorkingSetSize}, {"clock", "GetTickCount64"}, {"timestampMs", timestamp}}}};
  }
  if (method != "snapshot" && method != "dom.query" && method != "dom.activate" && method != "dom.fill") return failed("UNSUPPORTED");
  // Shared ownership prevents a timed-out callback writing into a dead stack.
  auto promise = std::make_shared<std::promise<json>>();
  auto result = promise->get_future();
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  if (!accepts_execution_context_receipt(context_receipt) ||
      generation != bridge_generation_.load(std::memory_order_acquire)) return failed("STALE_DOCUMENT");
  if (!engine_thread_ || !engine_thread_->post([this, request, method, promise, cancelled, context_receipt, generation] {
    if (*cancelled) return;
    try {
      if (!accepts_execution_context_receipt(context_receipt) ||
          generation != bridge_generation_.load(std::memory_order_acquire)) {
        promise->set_value(failed("STALE_DOCUMENT")); return;
      }
      if (!webview_ || !document_ready() || !wkeGlobalExec) { promise->set_value(failed("NOT_READY")); return; }
      const auto view = static_cast<wkeWebView>(webview_);
      const auto current = [this, view, context_receipt, generation, &cancelled] {
        return !cancelled->load(std::memory_order_acquire) && webview_ == view &&
            accepts_execution_context_receipt(context_receipt) &&
            generation == bridge_generation_.load(std::memory_order_acquire) && document_ready();
      };
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      const auto url = wkeGetURL(view);
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      if (!url || !soui::runtime::is_trusted_entry_url(url, resource_root_)) { promise->set_value(failed("UNTRUSTED_DOCUMENT")); return; }
      if (method == "dom.activate" || method == "dom.fill") {
        const auto& params = request["params"];
        if (!params.contains("generation") || !params["generation"].is_number_unsigned() || params["generation"].get<std::uint64_t>() != generation) {
          promise->set_value(failed("STALE_DOCUMENT")); return;
        }
      }
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      const auto es = wkeGlobalExec(view);
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      if (!es) { promise->set_value(failed("NOT_READY")); return; }
      std::string script;
      if (method == "snapshot") {
        script = "return JSON.stringify({ok:true,result:{title:document.title,readyState:document.readyState,text:(document.body?document.body.textContent:'').slice(0,8192)}});";
      } else {
        // Only the fixed agent is evaluated; selectors and values are JSON data.
        json command = {{"method", method}, {"params", request["params"]}};
        script = "return JSON.stringify(" + std::string(soui::test_kit::dom_script) + "(" + command.dump(-1, ' ', true) + "));";
      }
      // Keep the original admission receipt. A getter/DOM callback may reload
      // or create another context, even when the WebView address is unchanged.
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      const auto value = jsEval(es, script.c_str());
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      const auto raw = jsToTempString(es, value);
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      auto response = raw ? json::parse(raw, nullptr, false) : json();
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      if (!response.is_object() || !response.contains("ok")) { promise->set_value(failed("DOM_RESPONSE")); return; }
      if (response.value("ok", false)) {
        response["result"]["generation"] = generation;
        if (method == "snapshot") {
          response["result"]["documentReady"] = document_ready();
          response["result"]["applicationReady"] = document_ready() && application_ready_.load(std::memory_order_acquire) &&
              application_ready_receipt_.load(std::memory_order_acquire) == context_receipt;
          response["result"]["architecture"] = sizeof(void*) == 8 ? "x64" : "x86";
          if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
          const auto version = wkeGetVersionString ? wkeGetVersionString() : "unknown";
          if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
          response["result"]["kernelVersion"] = version ? version : "unknown";
          response["result"]["domAgentSha256"] = soui::test_kit::dom_script_sha256;
        }
      }
      if (!current()) { promise->set_value(failed("STALE_DOCUMENT")); return; }
      promise->set_value(std::move(response));
    } catch (...) { promise->set_value(failed("ENGINE_ERROR")); }
  })) return failed("ENGINE_STOPPED");
  if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
    *cancelled = true;
    return failed("ENGINE_TIMEOUT");
  }
  auto response = result.get();
  if (!accepts_execution_context_receipt(context_receipt) ||
      generation != bridge_generation_.load(std::memory_order_acquire)) return failed("STALE_DOCUMENT");
  return response;
}
#endif

bool dynamic_miniblink_adapter::application_ready() {
  const auto receipt = execution_context_receipt();
  if (!accepts_execution_context_receipt(receipt)) return false;
#if defined(SOUI_BRIDGE_BENCHMARK) || defined(SOUI_PERFORMANCE_DIAGNOSTICS)
  // Isolated benchmark and diagnostic pages may intentionally have no native
  // bridge dependency. Their renderer-side marker keeps the bounded smoke
  // lifecycle usable without changing ordinary product readiness semantics.
  if (document_ready() && !application_ready_ && engine_thread_ != nullptr) {
    (void)engine_thread_->invoke([this, receipt] {
      if (!accepts_execution_context_receipt(receipt) || webview_ == nullptr || wkeGlobalExec == nullptr) return;
      const auto view = static_cast<wkeWebView>(webview_);
      const auto es = wkeGlobalExec(view);
      if (es == nullptr || webview_ != view || !accepts_execution_context_receipt(receipt)) return;
      const auto value = jsEval(es, "return window.__souiBenchmarkReady === true ? 'ready' : ''; ");
      if (webview_ != view || !accepts_execution_context_receipt(receipt)) return;
      const auto marker = jsToString(es, value);
      if (webview_ == view && accepts_execution_context_receipt(receipt) &&
          marker != nullptr && std::string_view{marker} == "ready") {
        application_ready_receipt_.store(receipt, std::memory_order_release);
        application_ready_ = true;
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (accepts_execution_context_receipt(receipt)) probe_report_ = "benchmark=ready(no-native-service)";
      }
    });
  }
#endif
  return document_ready() && application_ready_.load(std::memory_order_acquire) &&
      application_ready_receipt_.load(std::memory_order_acquire) == receipt &&
      accepts_execution_context_receipt(receipt);
}

std::string dynamic_miniblink_adapter::application_diagnostics() {
  const auto receipt = execution_context_receipt();
  const bool bridge_ready = accepts_execution_context_receipt(receipt) && application_ready_ &&
      application_ready_receipt_.load(std::memory_order_acquire) == receipt;
  std::string diagnostics = std::string{"document="} + (document_ready() ? "ready" : "waiting") +
         ",bridge=" + (bridge_ready ? "ready" : "waiting") +
         ",draw_interval_ms=" + std::to_string(configured_draw_min_interval_.load());
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!probe_report_.empty()) diagnostics += "," + probe_report_;
  }
  char script_diagnostics[2] = {};
  if (engine_thread_ != nullptr &&
      GetEnvironmentVariableA("SOUI_DIAGNOSTIC_SCRIPT", script_diagnostics,
                              sizeof(script_diagnostics)) == 1 &&
      script_diagnostics[0] == '1') {
    const auto script_result = std::make_shared<std::string>();
    const bool collected = engine_thread_->invoke([this, script_result] {
      // Diagnostic observation only: no ACK, retry or admission mutation.
      const auto observed_receipt = execution_context_receipt();
      nlohmann::json observation{
          {"contextReceipt", std::to_string(observed_receipt)},
          {"contextAccepted", accepts_execution_context_receipt(observed_receipt)},
          {"documentReadyReceipt", std::to_string(document_ready_receipt_.load(std::memory_order_acquire))},
          {"documentReadyFlag", document_ready_.load(std::memory_order_acquire)},
          {"applicationReadyReceipt", std::to_string(application_ready_receipt_.load(std::memory_order_acquire))},
          {"applicationReadyFlag", application_ready_.load(std::memory_order_acquire)},
          {"controlledBroker", cursor_broker_ != nullptr},
          {"coherentAtomicSnapshot", false}};
      if (cursor_broker_) {
        const auto identity = cursor_broker_->identity();
        observation["broker"] = {{"view", std::to_string(identity.viewGeneration)},
            {"document", std::to_string(identity.documentGeneration)},
            {"geometry", std::to_string(identity.geometryGeneration)}};
        observation["brokerInputAllowed"] = cursor_broker_->input_allowed();
        const auto admission = cursor_broker_->native_admission_diagnostics();
        observation["nativeAdmission"] = {
            {"enabled", admission.enabled},
            {"firstRejection", admission.reason != detail::cursor_native_rejection::none},
            {"reason", detail::cursor_native_rejection_name(admission.reason)},
            {"kind", admission.kind},
            {"sdk", {{"view", std::to_string(admission.sdk.viewGeneration)},
                {"document", std::to_string(admission.sdk.documentGeneration)},
                {"geometry", std::to_string(admission.sdk.geometryGeneration)}}},
            {"receipt", {{"view", std::to_string(admission.receipt.viewGeneration)},
                {"document", std::to_string(admission.receipt.documentGeneration)},
                {"geometry", std::to_string(admission.receipt.geometryGeneration)}}},
            {"pending", admission.document_pending}, {"closing", admission.closing}, {"nonAtomic", true}};
        const auto registration = cursor_broker_->ole_registration_completion();
        observation["registrationCompletion"] = {
            {"observed", registration.observed}, {"shapeValid", registration.shape_valid},
            {"cancelled", registration.cancelled}, {"endpointMatches", registration.endpoint_matches},
            {"identityMatches", registration.identity_matches}, {"kind", registration.receipt.kind},
            {"taskSequence", std::to_string(registration.receipt.taskSequence)},
            {"scopeSequence", std::to_string(registration.receipt.scopeSequence)},
            {"identity", {{"view", std::to_string(registration.receipt.identity.viewGeneration)},
                {"document", std::to_string(registration.receipt.identity.documentGeneration)},
                {"geometry", std::to_string(registration.receipt.identity.geometryGeneration)}}}, {"nonAtomic", true}};
        // Real pre-close ledger observation only; FAILED classifies sticky vendor failure.
        const auto* api = static_cast<const wkeCursorBrokerApi*>(cursor_api_);
        const auto token = cursor_broker_->token();
        wkeCursorBrokerDrainReceipt drain{}; drain.structSize = sizeof(drain);
        const bool inspected = valid_cursor_api(api) && (token.low || token.high);
        const unsigned int status = inspected ? api->inspectDrain(token, &drain) : WKE_CURSOR_BROKER_UNAVAILABLE;
        const bool valid = inspected && drain.structSize == sizeof(drain) &&
            drain.endpoint.low == token.low && drain.endpoint.high == token.high &&
            drain.closing <= 1 && drain.drained <= 1 && drain.oleRegistered <= 1 &&
            (status == WKE_CURSOR_BROKER_OK || status == WKE_CURSOR_BROKER_FAILED);
        observation["vendorDrain"] = {{"inspected", inspected}, {"valid", valid},
            {"status", status}, {"vendorFailed", valid ? nlohmann::json(status == WKE_CURSOR_BROKER_FAILED) : nlohmann::json(nullptr)},
            {"nonAtomic", true}};
        if (valid) {
          observation["vendorDrain"]["closing"] = drain.closing;
          observation["vendorDrain"]["drained"] = drain.drained;
          observation["vendorDrain"]["queued"] = drain.queuedUiTasks;
          observation["vendorDrain"]["running"] = drain.runningUiTasks;
          observation["vendorDrain"]["nativeScopes"] = drain.nativeScopes;
          observation["vendorDrain"]["engineCallbacks"] = drain.engineCallbacks;
          observation["vendorDrain"]["oleRegistered"] = drain.oleRegistered;
        }
      }
      *script_result = observation.dump();
      if (webview_ == nullptr || wkeGlobalExec == nullptr) return;
      const auto es = wkeGlobalExec(static_cast<wkeWebView>(webview_));
      if (es == nullptr) return;
      const auto value = jsEval(es,
          "return JSON.stringify({readyState:document.readyState,nativePort:typeof window.__souiNativePostMessage,"
          "souiNativePort:typeof (window.SouiNative&&window.SouiNative.postMessage),"
          "probeType:typeof window.__souiComponentCertificate,"
          "probeStatus:window.__souiComponentCertificate&&window.__souiComponentCertificate.status==='failed'?'failed':'',"
          "probeCode:window.__souiComponentCertificate&&window.__souiComponentCertificate.code==="
          "'bootstrap-or-report-failed'?'bootstrap-or-report-failed':''});");
      const auto raw = jsToTempString(es, value);
      if (raw != nullptr) {
        auto page = nlohmann::json::parse(raw, nullptr, false);
        if (!page.is_discarded()) observation["page"] = std::move(page);
      }
      observation["contextReceiptAfter"] = std::to_string(execution_context_receipt());
      *script_result = observation.dump();
    });
    if (collected && !script_result->empty()) diagnostics += ",script=" + *script_result;
  }
  return diagnostics;
}

void dynamic_miniblink_adapter::set_session_id(std::string session_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  // A fast page may have bootstrapped before the UI thread finishes start().
  if (session_id_.empty()) {
    session_id_ = std::move(session_id);
    session_context_receipt_ = execution_context_receipt();
  }
}

#ifdef SOUI_BRIDGE_BENCHMARK
std::string dynamic_miniblink_adapter::benchmark_result() {
  if (engine_thread_ == nullptr) return {};
  return benchmark_result_slot_.poll(engine_thread_,
      [this] { return collect_benchmark_result_on_engine(); });
}

std::string dynamic_miniblink_adapter::collect_benchmark_result_on_engine() {
  if (engine_thread_ == nullptr || !engine_thread_->is_owner_thread()) return {};
  if (!webview_) return {};
  const auto es = wkeGlobalExec(static_cast<wkeWebView>(webview_));
  const auto value = jsEval(es, "return window.__souiPerformanceResult ? JSON.stringify(window.__souiPerformanceResult) : ''; ");
  const auto text = jsToString(es, value);
  if (!text || !*text) return {};
  auto report = nlohmann::json::parse(text);
  {
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    report["nativePaintQpcMs"] = paint_samples;
    report["managedFrameTiming"] = managed_frame_timing_;
    report["hostManagedPump"] = host_managed_pump_;
    report["dirtyGatedRepaint"] = host_managed_pump_;
    report["otherThreadPaintExperiment"] = experimental_other_thread_paint_;
    report["compositorRateExperiment"] = experimental_compositor_rate_;
    report["configuredWakeInterval"] = configured_wake_interval_.load();
    report["configuredDrawMinInterval"] = configured_draw_min_interval_.load();
    report["nativePixelChangeQpcMs"] = paint_pixel_changes;
    report["pixelSampleFailures"] = paint_pixel_failures;
    report["pixelSampleScope"] = "RGB hash changes on full-width memory bitmap scanline y=220; not screen presentation";
    if (first_vue_ready_qpc_ms > 0 && first_content_paint_qpc_ms == 0) return {};
    report["vueReadyQpcMs"] = first_vue_ready_qpc_ms;
    report["firstPaintAfterVueQpcMs"] = first_content_paint_qpc_ms;
    report["nativePaintPhases"] = paint_phases;
    report["nativePaintScope"] = "Miniblink paint-updated callback timestamps; not GPU presentation timestamps";
  }
  return report.dump();
}
void dynamic_miniblink_adapter::benchmark_motion(unsigned index) {
  if (!owner_window_ || !webview_) return;
  const auto window = static_cast<HWND>(owner_window_);
  if (!benchmark_width_) {
    RECT bounds{}; GetWindowRect(window, &bounds);
    benchmark_x_ = bounds.left; benchmark_y_ = bounds.top;
    benchmark_width_ = bounds.right - bounds.left; benchmark_height_ = bounds.bottom - bounds.top;
  }
  if (index < 40) wkeSetZoomFactor(static_cast<wkeWebView>(webview_), 1.0F + (index % 20) * 0.01F);
  else if (index < 80) SetWindowPos(window, nullptr, 0, 0, benchmark_width_ + (index % 10) * 2,
                                 benchmark_height_ + (index % 10), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  else if (index < 120) SetWindowPos(window, nullptr, benchmark_x_ + (index % 20) * 2,
                                    benchmark_y_ + (index % 20), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  else {
    wkeSetZoomFactor(static_cast<wkeWebView>(webview_), 1.0F);
    SetWindowPos(window, nullptr, benchmark_x_, benchmark_y_, benchmark_width_, benchmark_height_, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}
double dynamic_miniblink_adapter::benchmark_click(int x, int y) {
  if (!webview_window_ || x < 0 || y < 0 || x > 8191 || y > 8191) return 0;
  LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
  const auto position = MAKELPARAM(x, y);
  PostMessageW(static_cast<HWND>(webview_window_), WM_LBUTTONDOWN, MK_LBUTTON, position);
  PostMessageW(static_cast<HWND>(webview_window_), WM_LBUTTONUP, 0, position);
  return 1000.0 * now.QuadPart / frequency.QuadPart;
}
void dynamic_miniblink_adapter::benchmark_text() {
  if (!webview_window_) return;
  for (const WPARAM character : {WPARAM{0x4e2d}, WPARAM{0x6587}, WPARAM{0x41}})
    PostMessageW(static_cast<HWND>(webview_window_), WM_CHAR, character, 1);
}
std::string soui_miniblink_host::benchmark_result() {
  auto* adapter = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return adapter ? adapter->benchmark_result() : std::string{};
}
int soui_miniblink_host::configured_wake_interval() const noexcept {
  const auto* adapter = dynamic_cast<const dynamic_miniblink_adapter*>(adapter_.get());
  return adapter ? adapter->configured_wake_interval() : -1;
}
void soui_miniblink_host::benchmark_gallery(bool all_components) {
  if (auto* adapter=dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get())) adapter->benchmark_gallery(all_components);
}
void dynamic_miniblink_adapter::benchmark_gallery(bool all_components) {
  if (engine_thread_ != nullptr && !engine_thread_->is_owner_thread()) {
    (void)engine_thread_->post(
        [this, all_components] { benchmark_gallery(all_components); });
    return;
  }
  if (!webview_ || !wkeRunJS) return;
  const std::string source=std::string("window.__galleryAll=")+(all_components?"true;":"false;")+R"JS(
  (function(){
    var rows=[], started=performance.now(), deadline=started+15000;
    var mark=window.__souiBenchmarkMarkPhase;
    function finish(){window.__souiPerformanceResult={status:rows.some(function(r){return r.state==='failed';})?'failed':'measured',gallery:rows,scope:'DOM-triggered selection to component DOM readiness; not physical input or screen presentation'};}
    function wait(name,index,start){
      var area=document.querySelector('.gallery-example');
      var error=document.querySelector('.gallery-error');
      var active=document.querySelector('.gallery-list button.active small');
      if(active && active.textContent===name && error){
        rows.push({name:name,state:error.textContent.indexOf('隔离')>=0?'isolated':'failed',error:error.textContent,ms:performance.now()-start});next(index+1);return;
      }
      if(active && active.textContent===name && area && area.getAttribute('aria-busy')!=='true' && area.children.length){
        var bounds=area.getBoundingClientRect();
        rows.push({name:name,state:'rendered',ms:performance.now()-start,elements:area.querySelectorAll('*').length,width:bounds.width,height:bounds.height});
        if(window.__galleryAll && window.__souiBenchmarkGalleryCheckpoint)window.__souiBenchmarkGalleryCheckpoint(name,true);
        next(index+1);return;
      }
      if(performance.now()>deadline){rows.push({name:name,state:'failed',error:'Readiness deadline'});finish();return;}
      setTimeout(function(){wait(name,index,start);},5);
    }
    function next(index){
      if(!window.__galleryAll){finish();return;}
      setTimeout(function(){selectNext(index);},350);
    }
    function selectNext(index){
      var buttons=document.querySelectorAll('.gallery-list button');
      if(index>=buttons.length){finish();return;}
      var name=buttons[index].querySelector('small').textContent;
      if(window.__souiBenchmarkGalleryCheckpoint)window.__souiBenchmarkGalleryCheckpoint(name,false);
      deadline=performance.now()+5000;
      var start=performance.now();buttons[index].click();
      setTimeout(function(){wait(name,index,start);},10);
    }
    var buttons=document.querySelectorAll('.nav-item');
    for(var i=0;i<buttons.length;i++)if(buttons[i].textContent.indexOf('全部组件')>=0){
      if(mark)mark('input');
      buttons[i].click();wait('Button',0,started);return;
    }
    rows.push({state:'failed',error:'Gallery navigation missing'});finish();
  })();
  )JS";
  wkeRunJS(static_cast<wkeWebView>(webview_),source.c_str());
}
#endif

void dynamic_miniblink_adapter::notify_document_ready() { on_document_ready(); }

bool dynamic_miniblink_adapter::allow_document_navigation(std::string_view url) const noexcept {
  return soui::runtime::is_trusted_entry_url(url, resource_root_);
}

bool dynamic_miniblink_adapter::notify_main_context_created() {
  std::uint64_t completed_receipt = 0;
  return notify_main_context_created(completed_receipt);
}

std::uint64_t dynamic_miniblink_adapter::execution_context_receipt() const noexcept {
  return detail::main_context_gate<>::capture(cursor_context_receipt_);
}

bool dynamic_miniblink_adapter::accepts_execution_context_receipt(std::uint64_t receipt) const noexcept {
  return detail::main_context_gate<>::accepts(cursor_context_receipt_, receipt);
}

std::uint64_t dynamic_miniblink_adapter::invalidate_main_context() noexcept {
  const auto pending = detail::main_context_gate<>::invalidate(cursor_context_receipt_);
  // Revoke this Host first. Once the global revision changes, a newly begun
  // lookup must not still capture this Host's old completed receipt.
  const bool lookup_available = detail::context_lookup_revision<>::advance(native_context_lookup_revision);
  document_ready_.store(false, std::memory_order_release);
  application_ready_.store(false, std::memory_order_release);
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
  return lookup_available ? pending : 0;
}

bool dynamic_miniblink_adapter::notify_main_context_created(std::uint64_t& completed_receipt) {
  return notify_main_context_created(webview_, completed_receipt);
}

bool dynamic_miniblink_adapter::notify_main_context_created(void* originating_view,
    std::uint64_t& completed_receipt) {
  completed_receipt = 0;
  if (!originating_view || originating_view != webview_) return false;
  const bool controlled = execution_context_receipt() != 0;
  const auto pending = controlled ? invalidate_main_context() : 0;
  if (controlled && !pending) return false;
  const auto still_pending = [this, controlled, pending] {
    return controlled ? detail::main_context_gate<>::pending(cursor_context_receipt_, pending)
                      : execution_context_receipt() == 0;
  };
#ifdef _WIN32
  const auto view = webview_;
  const auto* broker = cursor_broker_.get();
#endif
  document_ready_ = false;
  application_ready_ = false;
  try {
  if (!still_pending() || !advance_cursor_document_on_engine() || !still_pending()) return false;
#ifdef _WIN32
  if (webview_ != view || cursor_broker_.get() != broker || (controlled && !broker)) return false;
  const auto context_identity = cursor_broker_ ? cursor_broker_->identity() : wkeCursorBrokerIdentity{};
#endif
  window_chrome_.clear();
  bridge_generation_.fetch_add(1, std::memory_order_acq_rel);
  std::string closing_session;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!still_pending()) return false;
    closing_session.swap(session_id_);
  }
  if (bridge_adapter_ != nullptr && !closing_session.empty())
    bridge_shutdown_session(*static_cast<bridge_adapter*>(bridge_adapter_), closing_session, 0);
  if (!still_pending()) return false;
#ifdef _WIN32
  if (webview_ != view || cursor_broker_.get() != broker) return false;
#endif
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!still_pending()) return false;
    probe_report_.clear();
  }
#ifdef _WIN32
  if (cursor_broker_) {
    if (!still_pending() || !cursor_broker_->input_allowed() || !update_cursor_identity_on_engine() ||
        !still_pending() || webview_ != view || cursor_broker_.get() != broker ||
        !cursor_broker_->input_allowed()) return false;
    const auto latest = cursor_broker_->identity();
    if (latest.viewGeneration != context_identity.viewGeneration ||
        latest.documentGeneration != context_identity.documentGeneration ||
        latest.geometryGeneration != context_identity.geometryGeneration || !still_pending()) return false;
    // Bind the future completed receipt while admission is still closed. A
    // setter may call out or fail; neither can authorize an unfinished context.
    if (!install_bridge_event_sink(pending + 1) || !still_pending() || webview_ != view ||
        cursor_broker_.get() != broker || !cursor_broker_->input_allowed()) return false;
    const auto before_commit = cursor_broker_->identity();
    if (before_commit.viewGeneration != latest.viewGeneration ||
        before_commit.documentGeneration != latest.documentGeneration ||
        before_commit.geometryGeneration != latest.geometryGeneration || !still_pending()) return false;
    if (!detail::main_context_gate<>::complete(cursor_context_receipt_, pending)) return false;
    completed_receipt = pending + 1;
    const auto final_identity = cursor_broker_->identity();
    if (!accepts_execution_context_receipt(completed_receipt) || !cursor_broker_->input_allowed() ||
        final_identity.viewGeneration != latest.viewGeneration ||
        final_identity.documentGeneration != latest.documentGeneration ||
        final_identity.geometryGeneration != latest.geometryGeneration) {
      (void)detail::main_context_gate<>::revoke_completed(cursor_context_receipt_, completed_receipt);
      return false;
    }
    interaction_router_.activate({latest.viewGeneration, latest.documentGeneration, latest.geometryGeneration});
    return accepts_execution_context_receipt(completed_receipt);
  }
#endif
  return !controlled && still_pending();
  } catch (...) {
    // An opaque cleanup/ACK failure never authorizes this context. A fresh
    // actual context or completed teardown is required to recover it.
    return false;
  }
}

std::string dynamic_miniblink_adapter::handle_message(std::string_view request,
                                                      bool native_user_gesture) {
  return handle_message(request, native_user_gesture, execution_context_receipt());
}

std::string dynamic_miniblink_adapter::handle_message(std::string_view request,
    bool native_user_gesture, std::uint64_t context_receipt) {
  if (!bridge_accepting_.load(std::memory_order_acquire) || !accepts_execution_context_receipt(context_receipt))
    return bridge_error({}, "E_CONTEXT_PENDING", "The native context is not current and completed.");
  if (bridge_adapter_ == nullptr) {
    return R"({"ok":false,"error":{"code":"E_BRIDGE_UNAVAILABLE","message":"The native bridge is not connected."}})";
  }
  const std::string method = bridge_request_method(request);
  if (!bridge_accepting_.load(std::memory_order_acquire) || !accepts_execution_context_receipt(context_receipt))
    return bridge_error({}, "E_CONTEXT_PENDING", "The native context is no longer current.");
  auto response = static_cast<bridge_adapter*>(bridge_adapter_)->handle_json(request, native_user_gesture);
  // Already-started native effects are not rolled back. Obsolete replies and
  // their Host readiness/session side effects are discarded instead.
  if (!bridge_accepting_.load(std::memory_order_acquire) || !accepts_execution_context_receipt(context_receipt))
    return bridge_error({}, "E_CONTEXT_PENDING", "The native context changed during execution.");
  if (method == "bridge.bootstrap" &&
      response.find("\"ok\":true") != std::string::npos) {
    const std::regex session_pattern(R"session("sessionId"\s*:\s*"([^"]+)")session");
    std::smatch match;
    if (std::regex_search(response, match, session_pattern) && match.size() > 1U) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!accepts_execution_context_receipt(context_receipt))
          return bridge_error({}, "E_CONTEXT_PENDING", "The native context changed during execution.");
        session_id_ = match[1].str();
        session_context_receipt_ = context_receipt;
      }
#ifdef SOUI_BRIDGE_BENCHMARK
      if (!benchmark_dataset_.empty()) {
        auto packet = nlohmann::json::parse(response);
        const auto& result = packet.at("result");
        const soui::runtime::scan_request_context context{result.at("sessionId").get<std::string>(),
          result.at("origin").get<std::string>(), result.at("token").get<std::string>()};
        const auto benchmark_root_token = static_cast<bridge_adapter*>(bridge_adapter_)->register_root(context, benchmark_dataset_);
        if (!accepts_execution_context_receipt(context_receipt))
          return bridge_error({}, "E_CONTEXT_PENDING", "The native context changed during execution.");
        packet["result"]["benchmarkRootToken"] = benchmark_root_token;
        response = packet.dump();
      }
#endif
    }
  }
  if (method == "bridge.probe.report" &&
      response.find("\"ok\":true") != std::string::npos) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!accepts_execution_context_receipt(context_receipt))
        return bridge_error({}, "E_CONTEXT_PENDING", "The native context changed during execution.");
      probe_report_ = "probe=script/json/event/error/local/proxy/promise/symbol/reflect/vue/reactivity/dom/bridge";
    }
    application_ready_receipt_.store(context_receipt, std::memory_order_release);
    application_ready_ = true;
#ifdef SOUI_BRIDGE_BENCHMARK
    std::lock_guard<std::mutex> lock(paint_samples_mutex);
    if (first_vue_ready_qpc_ms == 0) {
      LARGE_INTEGER now{}, frequency{};
      QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
      first_vue_ready_qpc_ms = 1000.0 * now.QuadPart / frequency.QuadPart;
    }
#endif
  }
  return response;
}

bool dynamic_miniblink_adapter::enqueue_bridge_message(
    std::string request, bool native_user_gesture) {
  return try_enqueue_bridge_message(std::move(request), native_user_gesture).ok;
}

host_result dynamic_miniblink_adapter::try_enqueue_bridge_message(
    std::string request, bool native_user_gesture) {
  return try_enqueue_bridge_message(std::move(request), native_user_gesture, execution_context_receipt());
}

host_result dynamic_miniblink_adapter::try_enqueue_bridge_message(
    std::string request, bool native_user_gesture, std::uint64_t context_receipt) {
  if (!accepts_execution_context_receipt(context_receipt))
    return failure("E_CONTEXT_PENDING", "The native context is not current and completed.");
  if (!bridge_accepting_.load(std::memory_order_acquire))
    return failure("E_DISPOSED", "The Host is no longer accepting native requests.");
  if (bridge_thread_ == nullptr || engine_thread_ == nullptr || !bridge_budget_)
    return failure("E_BRIDGE_QUEUE", "The asynchronous native bridge queue is unavailable.");
  if (request.empty() || request.size() > max_bridge_message_bytes)
    return failure("E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds 64 KiB.");
  const auto packet = bounded_bridge_packet(request);
  const auto request_id = packet_string(packet, "requestId");
  const auto method = packet_string(packet, "method");
  if (request_id.empty() || method.empty())
    return failure("E_INVALID_PARAMS", "Expected bounded requestId, method and JSON envelope.");
  const auto timeout = bridge_wait_timeout(packet);
  if (!timeout) return failure("E_INVALID_PARAMS", "timeoutMs must be between 1 and 60000.");
  const std::uint64_t generation =
      bridge_generation_.load(std::memory_order_acquire);
  detail::bridge_request_budget::rejection rejected;
  using request_lane = detail::bridge_request_budget::lane;
  const auto lane = method == "bridge.session.renew" ? request_lane::renewal :
      method == "task.unsubscribe" || method == "task.cancel" ? request_lane::control : request_lane::ordinary;
  const auto budget = bridge_budget_; // Same lease owner retained through the actual queue.
  const auto lease = budget->acquire(request_id, generation,
      lane, rejected);
  if (!lease) return rejected == detail::bridge_request_budget::rejection::duplicate
      ? failure("E_DUPLICATE_REQUEST", "This document already has an unfinished native request with this identity.")
      : failure("E_BUSY", "The native bridge request capacity is exhausted.");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  if (!accepts_execution_context_receipt(context_receipt)) {
    budget->record_drop(detail::bridge_request_budget::drop::stale_context);
    return failure("E_CONTEXT_PENDING", "The native context changed before admission.");
  }
  struct reply_owner final { detail::engine_reply_ticket ticket; std::string response; };
  const auto reply=std::make_shared<reply_owner>();
  // Prepare every closure before taking an Engine credit and before any business call.
  engine_thread::task resolver=[this,request_id,generation,context_receipt,lease,reply] {
    bridge_diagnostic_trace(this,"engine.resolve-begin",request_id);
    lease->release(); // Preserve capacity-before-JS-reentrancy ordering.
    resolve_bridge_response(request_id,reply->response,generation,context_receipt);
    bridge_diagnostic_trace(this,"engine.resolve-end",request_id);
  };
  engine_thread::task worker=[this, request=std::move(request), request_id,
      native_user_gesture,generation,context_receipt,deadline,lease,budget,reply,
      resolver=std::move(resolver)]() mutable {
    bridge_diagnostic_trace(this,"worker.begin",request_id);
    if (generation != bridge_generation_.load(std::memory_order_acquire) ||
        !accepts_execution_context_receipt(context_receipt)) {
      budget->record_drop(detail::bridge_request_budget::drop::stale_context);
      bridge_diagnostic_trace(this,"worker.drop-generation",request_id); return;
    }
    auto& response=reply->response;
    if (std::chrono::steady_clock::now() >= deadline) {
      budget->record_drop(detail::bridge_request_budget::drop::queue_expired);
      response=bridge_error(request_id,"E_TIMEOUT","The request expired in the native queue before execution.");
    } else try {
      response=handle_message(request,native_user_gesture,context_receipt);
      if (response.size() > max_bridge_message_bytes)
        response=bridge_error(request_id,"E_LIMIT_MESSAGE_SIZE","Native response exceeds 64 KiB.");
    } catch (...) { response=bridge_error(request_id,"E_NATIVE_EXCEPTION","The native handler failed."); }
    bridge_diagnostic_trace(this,"worker.response",request_id,"bytes="+std::to_string(response.size()));
    if (generation != bridge_generation_.load(std::memory_order_acquire) ||
        !accepts_execution_context_receipt(context_receipt) || engine_thread_ == nullptr) {
      budget->record_drop(detail::bridge_request_budget::drop::stale_context);
      bridge_diagnostic_trace(this,"worker.drop-before-engine",request_id); return;
    }
    const auto posted=detail::engine_admission::submit_reply(*engine_thread_,reply->ticket,std::move(resolver));
    if (posted != detail::engine_admission_result::accepted) budget->record_drop(detail::bridge_request_budget::drop::engine_post);
    bridge_diagnostic_trace(this,posted == detail::engine_admission_result::accepted ? "worker.engine-posted" : "worker.engine-post-failed",request_id);
  };
  const auto engine_lane=lane == request_lane::renewal ? detail::engine_lane::renewal :
      lane == request_lane::control ? detail::engine_lane::control : detail::engine_lane::ordinary;
  const auto reserved=detail::engine_admission::reserve_reply(*engine_thread_,engine_lane,reply->ticket);
  if (reserved != detail::engine_admission_result::accepted) {
    budget->record_drop(detail::bridge_request_budget::drop::engine_post);
    return engine_admission_failure(reserved,"The Engine did not admit reply capacity before execution.");
  }
  if (!accepts_execution_context_receipt(context_receipt)) {
    budget->record_drop(detail::bridge_request_budget::drop::stale_context);
    return failure("E_CONTEXT_PENDING","The native context changed before worker admission.");
  }
  bridge_diagnostic_trace(this,"queue.admitted",request_id,method);
  const auto queued=detail::engine_admission::post(*bridge_thread_,engine_lane,std::move(worker));
  if (queued != detail::engine_admission_result::accepted) budget->record_drop(detail::bridge_request_budget::drop::worker_post);
  bridge_diagnostic_trace(this,queued == detail::engine_admission_result::accepted ? "queue.posted" : "queue.post-failed",request_id);
  return queued == detail::engine_admission_result::accepted ? success("Native request admitted.") :
      failure(queued == detail::engine_admission_result::full ? "E_BUSY" : "E_BRIDGE_QUEUE",
          "The asynchronous native bridge queue did not admit the request.");
}

void dynamic_miniblink_adapter::resolve_bridge_response(
    std::string request_id, std::string response, std::uint64_t generation,
    std::uint64_t context_receipt) {
#ifdef _WIN32
  if (generation != bridge_generation_.load(std::memory_order_acquire)
      || !accepts_execution_context_receipt(context_receipt) || webview_ == nullptr || wkeRunJS == nullptr) {
    bridge_diagnostic_trace(this, "resolver.drop", request_id);
    return;
  }
  const auto script = bridge_resolution_script(request_id, response);
  if (!accepts_execution_context_receipt(context_receipt) ||
      generation != bridge_generation_.load(std::memory_order_acquire)) return;
  const auto view = static_cast<wkeWebView>(webview_);
  const auto result = wkeRunJS(view, script.c_str());
  if (webview_ != view || !accepts_execution_context_receipt(context_receipt) ||
      generation != bridge_generation_.load(std::memory_order_acquire)) {
    bridge_diagnostic_trace(this, "resolver.context-changed", request_id);
    return;
  }
  bridge_diagnostic_trace(this, "resolver.ran", request_id,
      "result=" + std::to_string(static_cast<long long>(result)));
#else
  (void)request_id;
  (void)response;
  (void)generation;
  (void)context_receipt;
#endif
}

bool dynamic_miniblink_adapter::processing_user_gesture() const noexcept {
#ifdef _WIN32
  const auto receipt = execution_context_receipt();
  const auto view = static_cast<wkeWebView>(webview_);
  if (!accepts_execution_context_receipt(receipt) || !view || !wkeIsProcessingUserGesture) return false;
  const auto gesture = wkeIsProcessingUserGesture(view) != FALSE;
  return webview_ == view && accepts_execution_context_receipt(receipt) && gesture;
#else
  return false;
#endif
}

bool dynamic_miniblink_adapter::owns_execution_context(void* execution_state) const noexcept {
  return owns_execution_context(execution_state, execution_context_receipt());
}

bool dynamic_miniblink_adapter::owns_execution_context(void* execution_state,
    std::uint64_t context_receipt) const noexcept {
#ifdef _WIN32
  if (!accepts_execution_context_receipt(context_receipt) ||
      execution_state == nullptr || webview_ == nullptr || jsGetWebView == nullptr) {
    return false;
  }
  const auto state = static_cast<jsExecState>(execution_state);
  const auto view = static_cast<wkeWebView>(webview_);
  const auto* broker = cursor_broker_.get();
  const auto identity = broker ? broker->identity() : wkeCursorBrokerIdentity{};
  if (broker && !broker->input_allowed()) return false;
  const auto lookup = detail::context_lookup_revision<>::capture(native_context_lookup_revision);
  if (!bridge_accepting_.load(std::memory_order_acquire) ||
      !detail::context_lookup_revision<>::matches(native_context_lookup_revision, lookup) ||
      jsGetWebView(state) != view || !detail::context_lookup_revision<>::matches(native_context_lookup_revision, lookup) ||
      webview_ != view || cursor_broker_.get() != broker ||
      !accepts_execution_context_receipt(context_receipt)) return false;
  if (broker) {
    const auto latest = broker->identity();
    if (!broker->input_allowed() || identity.viewGeneration != latest.viewGeneration ||
        identity.documentGeneration != latest.documentGeneration ||
        identity.geometryGeneration != latest.geometryGeneration) return false;
  }
  return accepts_execution_context_receipt(context_receipt);
#else
  (void)execution_state;
  (void)context_receipt;
  return false;
#endif
}

void dynamic_miniblink_adapter::on_document_ready() {
#ifdef _WIN32
  // Main-frame readiness is a business notification, not a new document ACK.
  // Only the main-world context callback can advance the controlled epoch.
  const auto receipt = execution_context_receipt();
  if (!accepts_execution_context_receipt(receipt) ||
      (cursor_broker_ && !cursor_broker_->input_allowed()) || !install_js_bindings(receipt) ||
      !accepts_execution_context_receipt(receipt)) return;
  document_ready_receipt_.store(receipt, std::memory_order_release);
  document_ready_ = true;
  dispatch_event(R"({"kind":"event","event":"host.ready","data":{"localResources":true}})", receipt);
#endif
}

bool dynamic_miniblink_adapter::update_window_chrome(void* state, std::string_view raw) {
#ifdef _WIN32
  // Native decoration only; no alternative privileged business Bridge.
  // Miniblink creates a fresh temporary exec-state wrapper on each call to
  // wkeGlobalExec; pointer equality does not identify a JavaScript context.
  const auto receipt = execution_context_receipt();
  if (raw.size() > 8192 || !owns_execution_context(state, receipt)) return false;
  try {
    // The incoming text may be a JS temporary invalidated by an opaque call.
    const std::string request(raw);
    const auto view = static_cast<wkeWebView>(webview_);
    const auto top = jsEval(static_cast<jsExecState>(state), "return window === window.top;");
    if (!accepts_execution_context_receipt(receipt) || webview_ != view) return false;
    const bool is_top = jsToBoolean(static_cast<jsExecState>(state), top);
    if (!is_top || !accepts_execution_context_receipt(receipt) || webview_ != view) return false;
    const auto url = wkeGetURL(view);
    if (!url || !accepts_execution_context_receipt(receipt) || webview_ != view ||
        !soui::runtime::is_trusted_entry_url(url, resource_root_)) return false;
    const auto packet = nlohmann::json::parse(request, nullptr, false);
    if (!packet.is_object() || !packet.contains("width") || !packet["width"].is_number_integer() ||
        packet["width"].get<double>() <= 0 || packet["width"].get<double>() > 32768 ||
        !packet.contains("regions") || !packet["regions"].is_array() ||
        packet["regions"].size() > window_chrome::capacity) return false;
    std::array<chrome_region, window_chrome::capacity> regions{};
    std::size_t count = 0;
    for (const auto& item : packet["regions"]) {
      if (!item.is_array() || item.size() != 5 || !item[0].is_string()) return false;
      for (int i = 1; i < 5; ++i)
        if (!item[i].is_number_integer() || item[i].get<double>() < 0 ||
            item[i].get<double>() > 32768) return false;
      const auto name = item[0].get<std::string>();
      chrome_action action = chrome_action::none;
      if (name == "drag") action = chrome_action::drag;
      else if (name == "minimize") action = chrome_action::minimize;
      else if (name == "maximize") action = chrome_action::maximize;
      else if (name == "close") action = chrome_action::close;
      regions[count++] = {action, item[1].get<int>(), item[2].get<int>(), item[3].get<int>(), item[4].get<int>()};
    }
    return accepts_execution_context_receipt(receipt) && webview_ == view &&
        window_chrome_.publish(packet["width"].get<int>(), regions, count);
  } catch (...) { return false; }
#else
  (void)state;
  (void)raw;
  return false;
#endif
}

bool dynamic_miniblink_adapter::install_js_bindings(std::uint64_t context_receipt) {
#ifdef _WIN32
  if (!accepts_execution_context_receipt(context_receipt) || webview_ == nullptr || wkeRunJS == nullptr) return false;
  const auto view = static_cast<wkeWebView>(webview_);
  const auto run = [this, view, context_receipt](const char* source) {
    if (webview_ != view || !accepts_execution_context_receipt(context_receipt)) return false;
    (void)wkeRunJS(view, source);
    return webview_ == view && accepts_execution_context_receipt(context_receipt);
  };
  static constexpr const char* bootstrap =
      "(function(){"
      "var n=window.SouiNative=window.SouiNative||{};"
      "n.postMessage=window.__souiNativePostMessage;"
      "n.onMessage=n.onMessage||function(e){if(n._listeners){n._listeners.slice().forEach(function(f){f(e);});}};"
      "n.onEvent=n.onEvent||function(f){n._listeners=n._listeners||[];n._listeners.push(f);return function(){"
      "var i=n._listeners.indexOf(f);if(i>=0)n._listeners.splice(i,1);};};"
      "n.ready=n.ready||function(){};"
      "})();";
  if (!run(bootstrap) || !run(window_chrome_script)) return false;
  char initial_page[32] = {};
  const DWORD initial_page_length = GetEnvironmentVariableA("SOUI_INITIAL_PAGE", initial_page,
                                                            sizeof(initial_page));
  if (initial_page_length > 0 && initial_page_length < sizeof(initial_page)) {
    const auto page_literal = json_string_literal(std::string{initial_page, initial_page_length});
    const auto page_script = "window.__souiInitialPage=" + page_literal + ";";
    if (!run(page_script.c_str())) return false;
  }
  char react_mode[2] = {};
  if (GetEnvironmentVariableA("SOUI_REACT_MODE", react_mode, sizeof(react_mode)) == 1 && react_mode[0] == '1')
    if (!run("window.__souiReactMode=true;")) return false;
  return webview_ == view && accepts_execution_context_receipt(context_receipt);
#else
  (void)context_receipt;
  return false;
#endif
}

void dynamic_miniblink_adapter::dispatch_event(const std::string& event_json) {
  dispatch_event(event_json, execution_context_receipt());
}

bool dynamic_miniblink_adapter::install_bridge_event_sink(std::uint64_t context_receipt) {
  if (!bridge_adapter_) return true;
  try {
    const std::weak_ptr<event_dispatch_lifetime> weak = event_lifetime_;
    bridge_adapter_->set_event_sink([weak, context_receipt](std::string_view event) {
      if (const auto lifetime = weak.lock()) {
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        if (lifetime->adapter) lifetime->adapter->dispatch_bridge_event(std::string{event}, context_receipt);
      }
    });
    return true;
  } catch (...) { return false; }
}

void dynamic_miniblink_adapter::dispatch_bridge_event(const std::string& event_json,
    std::uint64_t receipt) {
  if (!accepts_execution_context_receipt(receipt)) return;
  if (receipt) {
    try {
      if (event_json.empty() || event_json.size() > max_bridge_message_bytes) return;
      const auto packet = bounded_bridge_packet(event_json);
      const auto session = packet_string(packet, "sessionId");
      // Task events carry the actual subscription session. A late event may
      // not borrow the current document just because it reaches a shared sink.
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (session.empty() || session != session_id_ || session_context_receipt_ != receipt ||
          !accepts_execution_context_receipt(receipt)) return;
    } catch (...) { return; }
  }
  // Neither posting nor opaque callbacks run while the session lock is held.
  dispatch_event(event_json, receipt);
}

void dynamic_miniblink_adapter::dispatch_event(const std::string& event_json,
    std::uint64_t context_receipt) {
#ifdef _WIN32
  if (!document_ready() || !accepts_execution_context_receipt(context_receipt) || engine_thread_ == nullptr) return;
  const auto lifetime = event_lifetime_;
  const auto generation = bridge_generation_.load(std::memory_order_acquire);
  const auto posted=detail::engine_admission::post(*engine_thread_,detail::engine_lane::ordinary,[lifetime, generation, context_receipt, event_json] {
    dynamic_miniblink_adapter* adapter = nullptr;
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      adapter = lifetime->adapter;
    }
    // Teardown itself is serialized on this same owner thread. Drop events
    // queued by a detached sink or an obsolete document without touching it.
    if (adapter && generation == adapter->bridge_generation_.load(std::memory_order_acquire) &&
        adapter->accepts_execution_context_receipt(context_receipt))
      adapter->dispatch_event_now(event_json, context_receipt);
  });
  if (posted != detail::engine_admission_result::accepted)
    bridge_diagnostic_trace(this,"event.engine-post-failed");
#else
  (void)event_json;
  (void)context_receipt;
#endif
}

void dynamic_miniblink_adapter::dispatch_event_now(const std::string& event_json) {
  dispatch_event_now(event_json, execution_context_receipt());
}

void dynamic_miniblink_adapter::dispatch_event_now(const std::string& event_json,
    std::uint64_t context_receipt) {
#ifdef _WIN32
  if (!document_ready() || !accepts_execution_context_receipt(context_receipt) ||
      document_ready_receipt_.load(std::memory_order_acquire) != context_receipt ||
      webview_ == nullptr || wkeRunJS == nullptr) return;
  const auto literal = json_string_literal(event_json);
  const auto script = "(function(){var e=JSON.parse(" + literal + ");"
                      "if(window.__souiNativeDispatchEvent){window.__souiNativeDispatchEvent(e);}"
                      "else if(window.SouiNative&&window.SouiNative.onMessage){window.SouiNative.onMessage(e);}})();";
  const auto view = static_cast<wkeWebView>(webview_);
  if (!accepts_execution_context_receipt(context_receipt)) return;
  (void)wkeRunJS(view, script.c_str());
  // Delivery already entered JS is not rollback. Do not adopt a nested
  // context, run further delivery, or interpret it as this receipt's success.
  if (webview_ != view || !accepts_execution_context_receipt(context_receipt)) return;
#else
  (void)event_json;
  (void)context_receipt;
#endif
}

void dynamic_miniblink_adapter::deliver_posted_event(void* payload) {
#ifdef _WIN32
  std::unique_ptr<pending_event> event(static_cast<pending_event*>(payload));
  if (event == nullptr || event->adapter != this) return;
  if (!accepts_execution_context_receipt(event->context_receipt) ||
      (event->context_receipt && event->bridge_generation != bridge_generation_.load(std::memory_order_acquire))) return;
  dispatch_event_now(event->json, event->context_receipt);
#else
  (void)payload;
#endif
}

#ifdef _WIN32
bool WKE_CALL_TYPE document_navigation_callback(wkeWebView, void* param, wkeNavigationType, wkeString url) {
  if (!param || !url || !wkeGetString) return false;
  const auto value = wkeGetString(url);
  return value && static_cast<dynamic_miniblink_adapter*>(param)->allow_document_navigation(value);
}

void WKE_CALL_TYPE script_context_created_callback(wkeWebView view, void* param, wkeWebFrameHandle frame, void*, int, int world) {
  if (param && world == 0) {
    auto* adapter = static_cast<dynamic_miniblink_adapter*>(param);
    const auto entry_receipt = adapter->execution_context_receipt();
    if (!wkeIsMainFrame(view, frame) || adapter->execution_context_receipt() != entry_receipt) return;
    std::uint64_t completed_receipt = 0;
    if (!adapter->notify_main_context_created(view, completed_receipt)) return;
    const auto current = [adapter, completed_receipt] {
      return adapter->accepts_execution_context_receipt(completed_receipt);
    };
    if (!current()) return;
    char react_mode[2] = {};
    if (GetEnvironmentVariableA("SOUI_REACT_MODE", react_mode, sizeof(react_mode)) == 1 &&
        react_mode[0] == '1' && wkeRunJS != nullptr)
      (void)wkeRunJS(view, "window.__souiReactMode=true;");
    if (!current()) return;
    // Publish the requested route before the bundled module evaluates.  The
    // document-ready callback remains a fallback for older Miniblink builds,
    // but setting it here removes a race where React mounted the home page
    // before the initial route was visible.
    char initial_page[32] = {};
    const DWORD initial_page_length = GetEnvironmentVariableA("SOUI_INITIAL_PAGE", initial_page,
                                                               sizeof(initial_page));
    if (initial_page_length > 0 && initial_page_length < sizeof(initial_page) && wkeRunJS != nullptr && current()) {
      const auto page_literal = json_string_literal(std::string{initial_page, initial_page_length});
      const auto page_script = "window.__souiInitialPage=" + page_literal + ";";
      (void)wkeRunJS(view, page_script.c_str());
    }
  }
}

void WKE_CALL_TYPE document_ready_callback(wkeWebView, void* param) {
  if (param != nullptr) static_cast<dynamic_miniblink_adapter*>(param)->notify_document_ready();
}

void WKE_CALL_TYPE main_document_ready_callback(wkeWebView view, void* param, wkeWebFrameHandle frame) {
  if (!param) return;
  auto* adapter = static_cast<dynamic_miniblink_adapter*>(param);
  const auto receipt = adapter->execution_context_receipt();
  if (adapter->accepts_execution_context_receipt(receipt) && wkeIsMainFrame(view, frame) &&
      adapter->accepts_execution_context_receipt(receipt)) adapter->notify_document_ready();
}

jsValue WKE_CALL_TYPE native_window_chrome(jsExecState es, void*) {
  auto* adapter = adapter_for_context(es);
  if (!adapter || !es) return jsBoolean(false);
  const auto receipt = adapter->execution_context_receipt();
  if (!adapter->accepts_execution_context_receipt(receipt) || jsArgCount(es) != 1 ||
      !jsIsString(jsArg(es, 0)) || !adapter->accepts_execution_context_receipt(receipt)) return jsBoolean(false);
  const char* raw = jsToTempString(es, jsArg(es, 0));
  if (!raw || !adapter->accepts_execution_context_receipt(receipt)) return jsBoolean(false);
  // Copy before owns_execution_context's opaque getter invalidates JS temps.
  try {
    const std::string_view raw_request(raw);
    if (raw_request.size() > 8192) return jsBoolean(false);
    const std::string request(raw_request);
    return jsBoolean(adapter->owns_execution_context(es, receipt) &&
        adapter->accepts_execution_context_receipt(receipt) && adapter->update_window_chrome(es, request));
  } catch (...) { return jsBoolean(false); }
}

jsValue WKE_CALL_TYPE native_post_message(jsExecState es, void*) {
  auto* adapter = adapter_for_context(es);
  if (adapter == nullptr || es == nullptr) return jsUndefined();
  const auto context_receipt = adapter->execution_context_receipt();
  if (!adapter->accepts_execution_context_receipt(context_receipt))
    return jsString(es, R"({"requestId":"","ok":false,"error":{"code":"E_CONTEXT_PENDING","message":"The native context is not current and completed."}})");
  if (jsArgCount(es) != 1 || !jsIsString(jsArg(es, 0)) ||
      !adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();
  const auto raw = jsToTempString(es, jsArg(es, 0));
  if (raw == nullptr || !adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();
  std::string request_id;
  try {
    const std::string_view raw_request(raw);
    if (raw_request.empty() || raw_request.size() > max_bridge_message_bytes) {
      const auto response = bridge_error({}, "E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds 64 KiB.");
      return jsString(es, response.c_str());
    }
    const std::string request(raw_request); // jsEval can invalidate Miniblink's temporary string.
    if (!adapter->owns_execution_context(es, context_receipt))
      return jsString(es, R"({"requestId":"","ok":false,"error":{"code":"E_UNAUTHORIZED","message":"Bridge calls are restricted to the trusted WebView context."}})");
    const auto packet = bounded_bridge_packet(request);
    request_id = packet_string(packet, "requestId");
    const auto method = packet_string(packet, "method");
    if (request_id.empty() || method.empty()) {
      const auto response = bridge_error(request_id, "E_INVALID_PARAMS", "Expected bounded requestId, method and JSON envelope.");
      return jsString(es, response.c_str());
    }
    if (method == "bridge.bootstrap") {
      bridge_diagnostic_trace(adapter, "binding.bootstrap", request_id);
      const bool gesture = adapter->processing_user_gesture();
      if (!adapter->owns_execution_context(es, context_receipt))
        return jsString(es, bridge_error(request_id, "E_CONTEXT_PENDING", "The native context changed before execution.").c_str());
      const std::string response = adapter->handle_message(request, gesture, context_receipt);
      if (!adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();
      return jsString(es, response.c_str());
    }
    const auto timeout = bridge_wait_timeout(packet);
    if (!timeout) {
      const auto response = bridge_error(request_id, "E_INVALID_PARAMS", "timeoutMs must be between 1 and 60000.");
      return jsString(es, response.c_str());
    }
    const bool native_user_gesture = adapter->processing_user_gesture();
    if (!adapter->owns_execution_context(es, context_receipt)) return jsUndefined();
    // Reserve the document resolver before admitting any side effects. Both
    // creation and response delivery run on this same engine owner.
    const auto create = std::string{"return "} + detail::create_bridge_pending_script
        + "(" + json_string_literal(request_id) + "," + std::to_string(timeout) + ","
        + (method == "bridge.session.renew" ? "true" : "false") + ");";
    const auto waiting = jsEval(es, create.c_str());
    if (!adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();
    const bool waiting_is_string = jsIsString(waiting);
    if (!adapter->owns_execution_context(es, context_receipt)) return jsUndefined();
    bridge_diagnostic_trace(adapter, "binding.resolver-created", request_id,
        "value=" + std::to_string(static_cast<long long>(waiting)) +
        " string=" + (waiting_is_string ? std::string{"1"} : std::string{"0"}));
    if (waiting_is_string) return waiting;
    host_result admitted;
    try { admitted = adapter->try_enqueue_bridge_message(std::string(request), native_user_gesture, context_receipt); }
    catch (...) { admitted = failure("E_NATIVE_EXCEPTION", "The native bridge could not admit this request."); }
    bridge_diagnostic_trace(adapter, admitted.ok ? "binding.admitted" : "binding.rejected",
        request_id, admitted.code);
    if (!admitted.ok) {
      const auto response = bridge_error(request_id, admitted.code.c_str(), admitted.message.c_str());
      const auto resolve = "return " + bridge_resolution_script(request_id, response) + ";";
      if (!adapter->owns_execution_context(es, context_receipt)) return jsUndefined();
      return jsEval(es, resolve.c_str());
    }
    return adapter->accepts_execution_context_receipt(context_receipt) ? waiting : jsUndefined();
  } catch (...) {
    if (!adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();
    const auto response = bridge_error(request_id, "E_NATIVE_EXCEPTION", "The native bridge could not read this request.");
    return jsString(es, response.c_str());
  }
}

#endif

soui_miniblink_host::soui_miniblink_host(std::unique_ptr<webview_host_adapter> adapter)
    : adapter_(std::move(adapter)) {}

soui_miniblink_host::~soui_miniblink_host() {
  if (adapter_cleanup_pending_ && !destroy().ok) std::terminate();
}

host_result soui_miniblink_host::start(const host_config& config) {
  if (lifecycle_operation_) return failure("E_BUSY", "A host lifecycle operation is already in progress.");
  if (session_ != nullptr && !session_->terminal()) return failure("E_ALREADY_STARTED", "Host is already running.");
  if (adapter_cleanup_pending_) return failure("E_CLEANUP_PENDING", "Release the previous mount attempt before starting again.");
  if (!adapter_) return failure("E_HOST_ADAPTER_MISSING", "A renderer host adapter is required.");
  const auto architecture = validate_architecture(config);
  if (!architecture.ok) return architecture;
  const auto resources = validate_local_resources(config.resource_root);
  if (!resources.ok) return resources;
  auto next_session = std::make_unique<soui::runtime::webview_session>();
  const lifecycle_operation_guard operation(lifecycle_operation_);
  // A failed mount can hand off a native callback before it reports failure.
  // Keep its cleanup owner even when no public session can be published.
  adapter_cleanup_pending_ = true;
  const auto mounted = adapter_->mount(config);
  if (!mounted.ok) return mounted;
  session_ = std::move(next_session);
  adapter_->set_session_id(session_->session_id());
  (void)session_->mark_ready();
  return mounted;
}

host_result soui_miniblink_host::hide() {
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  const auto hidden = adapter_->show(false);
  if (!hidden.ok) return hidden;
  (void)session_->hide();
  return hidden;
}

host_result soui_miniblink_host::show() {
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  const auto shown = adapter_->show(true);
  if (!shown.ok) return shown;
  if (session_->state() == soui::runtime::lifecycle_state::hidden) (void)session_->mark_ready();
  return shown;
}

host_result soui_miniblink_host::reload() {
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  const auto reloaded = adapter_->reload();
  if (!reloaded.ok) return reloaded;
  (void)session_->reload();
  adapter_->set_session_id(session_->session_id());
  return reloaded;
}

host_result soui_miniblink_host::close() {
  if (adapter_cleanup_pending_ && (session_ == nullptr || session_->terminal()))
    return destroy();
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  const auto unmounted = adapter_->unmount();
  if (!unmounted.ok) return unmounted;
  adapter_cleanup_pending_ = false;
  (void)session_->close();
  return unmounted;
}

host_result soui_miniblink_host::destroy() {
  if (lifecycle_operation_) return failure("E_BUSY", "A host lifecycle operation is already in progress.");
  if (!adapter_cleanup_pending_) {
    // close() publishes the terminal state; destroy() also releases its event
    // subscriptions. Keep that cleanup contract without unmounting twice.
    if (session_) (void)session_->destroy();
    return success("WebView was already closed or destroyed.");
  }
  const lifecycle_operation_guard operation(lifecycle_operation_);
  const auto unmounted = adapter_->unmount();
  if (!unmounted.ok) return unmounted;
  adapter_cleanup_pending_ = false;
  if (session_) (void)session_->destroy();
  return unmounted;
}

host_result soui_miniblink_host::resize(int width, int height) {
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  return adapter_->resize(width, height);
}

host_result soui_miniblink_host::focus() {
  const auto active = require_running();
  if (!active.ok) return active;
  const lifecycle_operation_guard operation(lifecycle_operation_);
  return adapter_->focus();
}

void soui_miniblink_host::pump() {
  if (running()) adapter_->pump();
}

void soui_miniblink_host::deliver_posted_event(void* payload) {
  // Late payloads must still be consumed by their originating adapter.
  if (adapter_) adapter_->deliver_posted_event(payload);
}

bool soui_miniblink_host::document_ready() const noexcept {
  return running() && adapter_->document_ready();
}

bool soui_miniblink_host::application_ready() {
  return running() && adapter_->application_ready();
}

std::string soui_miniblink_host::application_diagnostics() {
  return running() ? adapter_->application_diagnostics() : std::string{};
}

bool soui_miniblink_host::running() const noexcept {
  return session_ != nullptr && !session_->terminal() && !lifecycle_operation_;
}

host_result soui_miniblink_host::require_running() const {
  if (lifecycle_operation_) return failure("E_BUSY", "A host lifecycle operation is already in progress.");
  if (session_ == nullptr) return failure("E_NOT_STARTED", "Host has not started.");
  if (session_->terminal()) return failure("E_INVALID_LIFECYCLE", "The WebView session is already closed or destroyed.");
  return success();
}

#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
host_result soui_miniblink_host::request_memory_recovery() {
  const auto active = require_running();
  if (!active.ok) return active;
  auto* dynamic = dynamic_cast<dynamic_miniblink_adapter*>(adapter_.get());
  return dynamic == nullptr
      ? failure("E_HOST_ADAPTER_MISSING", "The dynamic Miniblink adapter is unavailable.")
      : dynamic->request_memory_recovery();
}
#endif

const soui::runtime::webview_session* soui_miniblink_host::session() const noexcept { return session_.get(); }

host_result soui_miniblink_host::validate_local_resources(const std::filesystem::path& root) {
  std::error_code error;
  auto readable_root = root;
#ifdef _WIN32
  if (!root.empty()) {
    readable_root = std::filesystem::absolute(root, error).lexically_normal();
    if (error) return failure("E_RESOURCE_MISSING", "Local Web resource root could not be resolved.");
    const auto native = readable_root.make_preferred().native();
    // The CRT stream and directory traversal both need the extended spelling.
    // This also works on supported Windows versions without a long-path opt-in.
    if (native.rfind(L"\\\\?\\", 0) != 0 && native.rfind(L"\\\\.\\", 0) != 0) {
      readable_root = native.rfind(L"\\\\", 0) == 0
          ? L"\\\\?\\UNC\\" + native.substr(2) : L"\\\\?\\" + native;
    }
  }
#endif
  if (root.empty() || !std::filesystem::is_directory(readable_root, error) || error) {
    return failure("E_RESOURCE_MISSING", "Local Web resource root does not exist.");
  }
  const auto index = readable_root / "index.html";
  if (!std::filesystem::is_regular_file(index, error) || error) {
    return failure("E_RESOURCE_MISSING", "Local Web resource root must contain index.html.");
  }
  for (const auto& file : std::filesystem::recursive_directory_iterator(readable_root, error)) {
    if (error) break;
    if (!file.is_regular_file(error)) continue;
    std::ifstream input(file.path(), std::ios::binary);
    if (!input) return failure("E_RESOURCE_READ", "A local Web resource could not be read.");
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contains_untrusted_frame_surface(file.path(), content)) {
      return failure("E_UNTRUSTED_FRAME", "Embedded frame surfaces are forbidden in local Web resources.");
    }
    if (contains_remote_runtime_reference(file.path(), content)) {
      return failure("E_REMOTE_RESOURCE", "Remote Web runtime URLs are forbidden.");
    }
  }
  if (error) return failure("E_RESOURCE_READ", "Local Web resources could not be enumerated.");
  return success();
}

bool binary_matches_architecture(const std::filesystem::path& binary,
                                 std::string_view architecture) {
  if (architecture != "x86" && architecture != "x64") return false;
  std::ifstream input(binary, std::ios::binary);
  if (!input) return false;

  std::uint16_t dos_signature = 0;
  input.read(reinterpret_cast<char*>(&dos_signature), sizeof(dos_signature));
  if (!input || dos_signature != 0x5A4DU) return false;
  input.seekg(0x3C, std::ios::beg);
  std::uint32_t pe_offset = 0;
  input.read(reinterpret_cast<char*>(&pe_offset), sizeof(pe_offset));
  if (!input || pe_offset > 64U * 1024U * 1024U) return false;
  input.seekg(static_cast<std::streamoff>(pe_offset), std::ios::beg);
  std::uint32_t pe_signature = 0;
  std::uint16_t machine = 0;
  input.read(reinterpret_cast<char*>(&pe_signature), sizeof(pe_signature));
  input.read(reinterpret_cast<char*>(&machine), sizeof(machine));
  if (!input || pe_signature != 0x00004550U) return false;
  return architecture == "x64" ? machine == 0x8664U : machine == 0x014CU;
}

host_result soui_miniblink_host::validate_architecture(const host_config& config) {
  if (config.architecture != "x86" && config.architecture != "x64") {
    return failure("E_INVALID_ARCHITECTURE", "Architecture must be x86 or x64.");
  }
  return success();
}

}  // namespace soui::sdk
